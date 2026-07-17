PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

SRCDIR   := src
BUILDDIR := build

# Flags
CFLAGS  := -O2 -Wall -Wno-unused-function -D_BSD_SOURCE -std=gnu11 \
           -ffunction-sections -fdata-sections \
           -I$(SRCDIR) $(if $(INCDIR),-I$(INCDIR)) -DSQLITE_THREADSAFE=1

# Linker: gc-sections removes dead code/data; -S strips debug symbols from ELF
LDFLAGS := $(if $(LIBDIR),-L$(LIBDIR)) -Wl,--allow-multiple-definition \
           -Wl,--gc-sections -Wl,-S

# Libraries
LIBS := \
    -lkernel_sys        \
    -lSceSystemService  \
    -lSceUserService    \
    -lSceAppInstUtil    \
    -lSceSysmodule

# Sources
SRCS := \
    main.c        \
    globals.c     \
    logging.c     \
    utils.c       \
    config.c      \
    fan.c         \
    kernel.c      \
    install.c     \
    sentinel.c    \
    webserver.c   \
    image_mount.c \
    sqlite3.c

OBJS := $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

# Targets
all: $(BUILDDIR) $(BUILDDIR)/voidshell.elf

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/voidshell.elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Built: $@"

# SQLite: compile for size, suppress warnings, omit every feature VoidShell doesn't use.
# VoidShell only calls sqlite3_open_v2 / sqlite3_exec / sqlite3_free with simple
# SELECT + UPDATE queries — no triggers, views, JSON, window funcs, virtual tables, etc.
# Safe OMIT flags for the amalgamation (no parser grammar changes needed).
# Flags that affect parser grammar (TRIGGER, ALTERTABLE, ANALYZE, ATTACH,
# VIRTUALTABLE) are intentionally excluded — they cause link errors in the
# pre-generated Lemon parser inside the amalgamation.
SQLITE_OMIT := \
    -DSQLITE_OMIT_LOAD_EXTENSION    \
    -DSQLITE_OMIT_DEPRECATED        \
    -DSQLITE_OMIT_SHARED_CACHE      \
    -DSQLITE_OMIT_PROGRESS_CALLBACK \
    -DSQLITE_OMIT_TRACE             \
    -DSQLITE_OMIT_COMPLETE          \
    -DSQLITE_OMIT_EXPLAIN           \
    -DSQLITE_OMIT_INTEGRITY_CHECK   \
    -DSQLITE_OMIT_AUTHORIZATION     \
    -DSQLITE_OMIT_WAL               \
    -DSQLITE_OMIT_JSON              \
    -DSQLITE_OMIT_AUTOINCREMENT     \
    -DSQLITE_DEFAULT_MEMSTATUS=0

$(BUILDDIR)/sqlite3.o: $(SRCDIR)/sqlite3.c
	$(CC) -Os -Wno-everything $(SQLITE_OMIT) -c -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Dashboard: Bun + Preact sources in ui/ → single-file www/index.html
www:
	cd ui && bun install --frozen-lockfile && bun run build
	mkdir -p $(BUILDDIR)/www
	cp www/index.html $(BUILDDIR)/www/index.html
	@echo "Built and copied: $(BUILDDIR)/www/index.html"

all-deploy: all www

clean:
	rm -rf $(BUILDDIR)

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

SRCDIR   := src
BUILDDIR := build

# Flags
CFLAGS  := -O2 -Wall -Wno-unused-function -D_BSD_SOURCE -std=gnu11 \
           -I$(SRCDIR) $(if $(INCDIR),-I$(INCDIR)) -DSQLITE_THREADSAFE=1

# Linker
LDFLAGS := $(if $(LIBDIR),-L$(LIBDIR)) -Wl,--allow-multiple-definition

# Libraries
LIBS := \
    -lkernel_sys        \
    -lSceSystemService  \
    -lSceUserService    \
    -lSceAppInstUtil    \
    -lSceSysmodule

# Sources
SRCS := \
    main.c      \
    globals.c   \
    logging.c   \
    utils.c     \
    config.c    \
    fan.c       \
    kernel.c    \
    install.c   \
    sentinel.c  \
    webserver.c \
    sqlite3.c

OBJS := $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

# Targets
all: $(BUILDDIR) $(BUILDDIR)/voidshell.elf

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/voidshell.elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Built: $@"

# Suppress all warnings for SQLite amalgamation
$(BUILDDIR)/sqlite3.o: $(SRCDIR)/sqlite3.c
	$(CC) $(CFLAGS) -Wno-everything -c -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

www: index.html
	mkdir -p $(BUILDDIR)/www
	cp index.html $(BUILDDIR)/www/index.html
	@echo "Copied: $(BUILDDIR)/www/index.html"

all-deploy: all www

clean:
	rm -rf $(BUILDDIR)

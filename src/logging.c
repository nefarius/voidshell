/*
 * logging.c — VoidShell debug and startup loggers
 *
 * Both log_debug() and log_startup() work identically except for the file
 * path and the line prefix:
 *
 *   debug.log   writes:  "[HH:MM:SS] <message>\n"
 *   startup.log writes:  "[HH:MM:SS] STARTUP: <message>\n"
 *
 * The logs directory is created unconditionally on every call (safe if it
 * already exists — mkdir returns EEXIST which is ignored).
 *
 * After writing, fsync() is called on the file descriptor so the data is
 * visible immediately even if the process is killed shortly after.
 */

#include "logging.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define LOGS_DIR   "/data/voidshell/logs"
#define DEBUG_LOG  LOGS_DIR "/debug.log"
#define STARTUP_LOG LOGS_DIR "/startup.log"

/* ------------------------------------------------------------------ */

void log_debug(const char *fmt, ...)
{
    char timebuf[64];
    time_t now;
    struct tm *tm_info;
    FILE *fp;
    va_list ap;
    int fd;

    mkdir(LOGS_DIR, 0777);

    fp = fopen(DEBUG_LOG, "a");
    if (!fp)
        return;

    now     = time(NULL);
    tm_info = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_info);

    fprintf(fp, "[%s] ", timebuf);

    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);

    fputc('\n', fp);
    fflush(fp);

    /* Ensure data reaches storage before returning */
    fd = fileno(fp);
    fsync(fd);

    fclose(fp);
}

/* ------------------------------------------------------------------ */

void log_startup(const char *fmt, ...)
{
    char timebuf[64];
    time_t now;
    struct tm *tm_info;
    FILE *fp;
    va_list ap;
    int fd;

    mkdir(LOGS_DIR, 0777);

    fp = fopen(STARTUP_LOG, "a");
    if (!fp)
        return;

    now     = time(NULL);
    tm_info = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_info);

    fprintf(fp, "[%s] STARTUP: ", timebuf);

    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);

    fputc('\n', fp);
    fflush(fp);

    fd = fileno(fp);
    fsync(fd);

    fclose(fp);
}

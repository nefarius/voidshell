#ifndef LOGGING_H
#define LOGGING_H

/*
 * logging.h — VoidShell logging interface
 *
 * Two log sinks, both append-only, timestamped:
 *   /data/voidshell/logs/debug.log    -- general runtime log (log_debug)
 *   /data/voidshell/logs/startup.log  -- startup / init events (log_startup)
 *
 * Both functions accept printf-style format strings.
 * The logs directory is created automatically if missing.
 */

/**
 * Write a timestamped line to /data/voidshell/logs/debug.log.
 * Format written: "[HH:MM:SS] <message>\n"
 */
void log_debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * Write a timestamped line to /data/voidshell/logs/startup.log.
 * Format written: "[HH:MM:SS] STARTUP: <message>\n"
 */
void log_startup(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* LOGGING_H */

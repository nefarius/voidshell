/* sentinel.h -- VoidShell sentinel / game watchdog */
#ifndef SENTINEL_H
#define SENTINEL_H

#include <stdbool.h>
#include <stddef.h>

/* Thread entry points */
void *thread_sentinel_logic(void *arg);
void *thread_klog_monitor(void *arg);

/* Sentinel helpers */
int  get_sentinel_delay(const char *title_id);
bool is_sentinel_whitelisted(const char *title_id);

/*
 * smart_wait() -- wait up to `delay_ms` milliseconds in 100ms intervals,
 * returning early if the app state changes (leaves GAME state) or if
 * g_sentinel_override is set.
 *
 * Returns true if the full delay elapsed, false if interrupted.
 */
bool smart_wait(int delay_ms);

/*
 * get_running_app_title() -- title ID of the running "big app" (the one game/app the console
 * runs, whatever has focus). Returns 1 and copies it into `out` when one runs, 0 when nothing
 * runs (confirmed), and -1 when the lookup failed (unknown).
 */
int get_running_app_title(char *out, size_t size);

/* Check / update the currently running foreground app */
void check_running_app_state(void);

/* System-level helpers */
void respring_shell(void);

/*
 * list_processes() -- walk kern.proc.all. The callback gets the pid and the name (at most 19
 * characters) and returns non-zero to stop. Returns the number of entries visited, or -1 on failure.
 */
typedef int (*process_cb)(int pid, const char *name, void *ctx);
int list_processes(process_cb cb, void *ctx);

/* kill_process() -- SIGKILL one process. Refuses pid <= 1, VoidShell and SceShellUI. 0 or an errno value. */
int kill_process(int pid);
void trigger_repair(void);
void *thread_repair_worker(void *arg);

#endif /* SENTINEL_H */

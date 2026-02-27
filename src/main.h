/* main.h -- VoidShell top-level entry point and startup threads */
#ifndef MAIN_H
#define MAIN_H

/* Startup helpers */
int   init_work_dirs(void);     /* ensure /data/voidshell directory tree exists */
void  write_pid_file(void);     /* write PID to /data/voidshell/voidshell.pid   */
void  init_mutexes(void);       /* initialise all pthread mutexes               */
void  detect_local_ip(void);    /* auto-detect local IP via UDP connect trick   */

/* Top-level threads */
void *thread_stats_monitor(void *arg);
void *thread_autorun_payloads(void *arg);

#endif /* MAIN_H */

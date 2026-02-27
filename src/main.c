/* main.c -- VoidShell entry point, startup, and top-level threads
 *
 * PS5 homebrew system management daemon.
 * Binary:  voidshell.elf  |  Version: V1.1  |  Author: VoidWhisper
 *
 * EXPIRY CHECK (beta gate) -- PATCHED IN ANALYZED BINARY:
 *   Original valid window was Feb 8-20 2026 only.
 *   Three NOP patches (90 90 90 90 90 90) applied at file offsets:
 *     FILE 0x71db  (VA 0x1031db)  JNZ  -- year == 2026 check
 *     FILE 0x71e5  (VA 0x1031e5)  JNZ  -- month == Feb check
 *     FILE 0x720e  (VA 0x10320e)  JNC  -- day in range 8-20 check
 *   Version string patched at FILE 0x128c0d (VA 0x224c0d):
 *     "V1.00 - Pre-Beta" -> "V1.01 Pre-Beta"
 *
 *   NOTE: ELF PT_LOAD maps VA 0x100000 -> file offset 0x4000
 *         Correct offset formula: file_off = (VA - 0x100000) + 0x4000
 */

#include "main.h"
#include "types.h"
#include "logging.h"
#include "utils.h"
#include "config.h"
#include "kernel.h"
#include "install.h"
#include "sentinel.h"
#include "webserver.h"
#include "ps5_sdk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>

/* ---- init_work_dirs  (0x1004f0) ---- */
/*
 * Ensure the VoidShell working directory tree exists,
 * creating any missing folders with chmod 0777.
 *
 * Directories ensured:
 *   /data/voidshell
 *   /data/voidshell/www
 *   /data/voidshell/logs
 *   /data/voidshell/payloads
 */
int init_work_dirs(void)
{
    struct stat st;

    if (stat("/data/voidshell", &st) != 0)
        mkdir("/data/voidshell", 0777);

    if (stat("/data/voidshell/www", &st) != 0)
        mkdir("/data/voidshell/www", 0777);

    if (stat("/data/voidshell/logs", &st) != 0)
        mkdir("/data/voidshell/logs", 0777);

    if (stat("/data/voidshell/payloads", &st) != 0)
        mkdir("/data/voidshell/payloads", 0777);

    return 1;
}

/* ---- write_pid_file ---- */
void write_pid_file(void)
{
    FILE *fp = fopen("/data/voidshell/voidshell.pid", "w");
    if (fp) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
    }
    log_startup("Process Init (PID: %d)", getpid());
}

/* ---- init_mutexes ---- */
void init_mutexes(void)
{
    pthread_mutex_init(&g_lib_mutex,      NULL);
    pthread_mutex_init(&g_sentinel_mutex, NULL);
    pthread_mutex_init(&g_stats_mutex,    NULL);
}

/* ---- detect_local_ip ---- */
/*
 * Auto-detect the PS5's local IP by opening a UDP socket and connecting
 * to 8.8.8.8:53 (no packets actually sent).  getsockname() then reveals
 * the outbound interface address, which is stored in g_ip_address.
 */
void detect_local_ip(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct sockaddr_in dns;
        memset(&dns, 0, sizeof(dns));
        dns.sin_family      = AF_INET;
        dns.sin_port        = htons(53);
        dns.sin_addr.s_addr = inet_addr("8.8.8.8");
        connect(sock, (struct sockaddr *)&dns, sizeof(dns));
        struct sockaddr_in local;
        socklen_t local_len = sizeof(local);
        getsockname(sock, (struct sockaddr *)&local, &local_len);
        inet_ntop(AF_INET, &local.sin_addr, g_ip_address, sizeof(g_ip_address));
        close(sock);
    }
}

/* ---- thread_stats_monitor  (0x102970) ---- */
/*
 * Polls system metrics every 2 seconds:
 *   - CPU temperature via sceKernelGetCpuTemperature()
 *   - SoC temperature via sceKernelGetSocSensorTemperature(0, ...)
 *   - System uptime via sysctl({CTL_KERN=1, KERN_BOOTTIME=21}, ...)
 *     formatted as HH:MM and stored in g_sys_uptime
 *
 * On the first tick, calls check_running_app_state() to detect a
 * game that was already running when VoidShell started.
 *
 * Time-jump detection: if difftime(now, last_time) exceeds a threshold
 * (DAT_0021f0a0, approximately 10-15 s), assumes the system woke from
 * Rest Mode, logs the event, waits 5 s, then calls restart_webserver().
 *
 * sysctl MIB for uptime:
 *   mib[0] = CTL_KERN  (1)
 *   mib[1] = KERN_BOOTTIME (21)   -- returns struct timeval
 */
void *thread_stats_monitor(void *arg)
{
    (void)arg;
    time_t  t_start  = time(NULL);
    int     first    = 1;

    if (!g_running)
        return NULL;

    do {
        int    cpu_temp = 0;
        int    soc_temp = 0;
        char   uptime_str[64] = "Unknown";

        /* Read temperatures */
        sceKernelGetCpuTemperature(&cpu_temp);
        sceKernelGetSocSensorTemperature(0, &soc_temp);

        /* Read boot time via sysctl */
        {
            struct timeval boot_tv;
            size_t tv_len = sizeof(boot_tv);
            int mib[2] = { 1 /* CTL_KERN */, 21 /* KERN_BOOTTIME */ };
            if (sysctl(mib, 2, &boot_tv, &tv_len, NULL, 0) != -1) {
                time_t now_t = time(NULL);
                long   up_s  = (long)(now_t - boot_tv.tv_sec);
                long   up_m  = up_s / 60;
                long   up_h  = up_m / 60;
                snprintf(uptime_str, sizeof(uptime_str), "%02ld:%02ld",
                         up_h, up_m % 60);
            }
        }

        /* Update shared stats under mutex */
        pthread_mutex_lock(&g_stats_mutex);
        g_temp_cpu = cpu_temp;
        g_temp_soc = soc_temp;
        strncpy(g_sys_uptime, uptime_str, 0x3f);
        pthread_mutex_unlock(&g_stats_mutex);

        /* First-tick: detect already-running game */
        if (first) {
            check_running_app_state();
            first = 0;
        }

        sceKernelUsleep(2000000);

        /* Time-jump / wake detection */
        time_t t_now = time(NULL);
        double elapsed = difftime(t_now, t_start);
        if (elapsed > 30.0 /* DAT_0021f0a0 threshold */) {
            log_debug("CORE: System time jump detected (Wake?). Restarting services.");
            notify_system("VoidShell: Wake Detected (Time Jump)");
            sceKernelUsleep(5000000);
            restart_webserver();
        }
        t_start = t_now;

    } while (g_running);

    return NULL;
}

/* ---- thread_autorun_payloads  (0x105b40) ---- */
/*
 * Runs once at startup after a 3-second initial delay.
 * Iterates g_autopayloads[] (count = g_autopayload_count).
 * For each entry with the enabled flag set:
 *   1. Waits entry->delay_ms * 1000 microseconds
 *   2. Calls send_payload(entry->filename) to inject via port 15651
 *   3. Logs success / failure
 *
 * Payload struct layout (0x48 bytes each):
 *   +0x00  char filename[0x40]   -- payload filename (in /data/voidshell/payloads/)
 *   +0x40  int  delay_ms         -- delay before sending, in milliseconds
 *   +0x44  char enabled          -- non-zero = enabled
 */
void *thread_autorun_payloads(void *arg)
{
    (void)arg;
    sceKernelUsleep(3000000);

    if (g_autopayload_count <= 0)
        return NULL;

    for (int i = 0; i < g_autopayload_count; i++) {
        autorun_payload_t *p = &g_autopayloads[i];
        if (!p->enabled)
            continue;
        log_debug("AUTORUN: Waiting %dms for %s", p->delayMs, p->filename);
        sceKernelUsleep((unsigned int)p->delayMs * 1000);
        if (send_payload(p->filename))
            log_debug("AUTORUN: %s injected", p->filename);
    }

    return NULL;
}

/* ---- main  (0x103180) ---- */
/*
 * VoidShell daemon entry point.
 *
 * STARTUP SEQUENCE:
 *   1. Privilege escalation: setuid(0) + kernel_set_ucred_authid
 *   2. check_integrity()          -- ensure /data/voidshell dirs exist
 *   3. Date expiry check          -- beta gate, patched out in analyzed binary
 *   4. init_kernel_offsets()      -- firmware detection + kstuff init
 *   5. load_config()              -- parse /data/voidshell/config.ini
 *   6. Write PID to /data/voidshell/voidshell.pid
 *   7. Initialize mutexes: g_lib_mutex, g_sentinel_mutex, g_stats_mutex
 *   8. Auto-detect local IP via UDP socket to 8.8.8.8:53 + getsockname
 *   9. Spawn thread_web_server    -- port 7007 (if g_enable_web_server)
 *  10. Spawn thread_stats_monitor, thread_autorun_payloads
 *  11. Sleep 5 s
 *  12. Show startup notification
 *  13. refresh_library()
 *  14. Initial scan of all paths
 *  15. Spawn thread_klog_monitor, thread_sentinel_logic
 *  16. mkdir /user/data/void_tmp
 *  17. Spawn thread_pkg_server
 *  18. Enter main scan loop
 *
 * MAIN LOOP:
 *   - If g_cmd_rescan: refresh_library(), clear path cache
 *   - If g_cmd_launch_id set: call sceSystemServiceLaunchApp
 *   - If g_library_dirty: refresh_library()
 *   - Scan all default + custom paths via process_path()
 *   - Sleep g_scan_interval * 1000000 us (config-driven, or 1 s if paused)
 */
int main(void)
{
    /* --- 1. Privilege escalation --- */
    setuid(0);
    /* setgid(0) omitted — not exported by ps5-payload-sdk libkernel_sys;
     * actual privilege escalation is performed by kernel_set_ucred_authid. */
    kernel_set_ucred_authid(0xffffffff, 0x4801000000000013ULL);

    /* --- 2. Ensure directory tree exists --- */
    init_work_dirs();

    /* --- 3. Date expiry check ---
     *
     * Original window (beta): Feb 8-20 2026 only.
     * Extended window (V1.1): all of 2026, Jan 1 – Dec 31.
     *
     * Valid if tm_year == 126 (2026).  Expires Jan 1 2027.
     */
    {
        time_t raw = time(NULL);
        struct tm *lt = localtime(&raw);
        /* Valid for the entire calendar year 2026 */
        if (lt->tm_year != 126 /* 2026 */) {
            log_startup("LICENSE EXPIRED or TAMPER DETECTED. Date: %d-%d-%d",
                        lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
            notify_system("VoidShell Expired.\nContact VoidWhisper.");
            return 0;
        }
    }

    /* --- 4. Kernel / kstuff init --- */
    if (!init_kernel_offsets()) {
        log_startup("SENTINEL: Kernel init failed. Kstuff interactions disabled.");
    }

    /* --- 5. Load configuration --- */
    load_config();

    /* --- 6. Write PID file --- */
    write_pid_file();

    /* --- 7. Initialize mutexes --- */
    init_mutexes();

    /* --- 8. Auto-detect local IP via UDP connect trick --- */
    detect_local_ip();

    /* --- 9. Spawn web server thread (if enabled) --- */
    if (g_enable_web_server) {
        pthread_create(&g_tid_webserver, NULL, thread_web_server, NULL);
        pthread_detach(g_tid_webserver);
    }

    /* --- 10. Spawn stats and autorun threads --- */
    {
        pthread_t tid_stats, tid_autorun;
        pthread_create(&tid_stats,   NULL, thread_stats_monitor,    NULL);
        pthread_detach(tid_stats);
        pthread_create(&tid_autorun, NULL, thread_autorun_payloads, NULL);
        pthread_detach(tid_autorun);
    }

    /* --- 11. Wait 5 s for threads to settle --- */
    sceKernelUsleep(5000000);

    /* --- 12. Startup notification --- */
    notify_system("VoidShell %s Loaded\n%s\nhttp://%s:%d",
                  VOIDSHELL_VERSION, "VoidWhisper",
                  g_ip_address, WEBSERVER_PORT);

    /* --- 13. Initial library refresh --- */
    refresh_library();

    /* --- 14. Initial scan of all paths --- */
    scan_all_paths();

    /* --- 15. Spawn klog monitor and sentinel threads --- */
    {
        pthread_t tid_klog, tid_sentinel;
        pthread_create(&tid_klog,     NULL, thread_klog_monitor, NULL);
        pthread_detach(tid_klog);
        pthread_create(&tid_sentinel, NULL, thread_sentinel_logic, NULL);
        pthread_detach(tid_sentinel);
    }

    /* --- 16. Create PKG temp upload directory --- */
    mkdir("/user/data/void_tmp", 0777);

    /* --- 17. Spawn PKG install server --- */
    {
        pthread_t tid_pkg;
        pthread_create(&tid_pkg, NULL, thread_pkg_server, NULL);
        pthread_detach(tid_pkg);
    }

    /* --- 18. Main scan loop --- */
    while (g_running) {
        int sleep_us = 1000000;  /* default: 1 s (used when paused) */

        if (!g_paused) {
            /* Handle explicit rescan command */
            if (g_cmd_rescan) {
                notify_system("Rescan Requested");
                g_cmd_rescan = 0;
                refresh_library();
                /* Clear the 512-slot path cache:
                 * Each slot occupies 0x421 bytes; the cache has 512 entries
                 * (total cache array = 512 * 0x421 bytes).
                 * The Ghidra decompile clears cache[0x420], cache[0x841], ...
                 * which are the "used" sentinel bytes at the end of each slot. */
                for (int ci = 0x420; ci < 0x84620; ci += 0x421)
                    cache[ci] = 0;
            }

            /* Handle launch command */
            if (g_cmd_launch_id[0] != '\0') {
                int foreground_user_id = 0;
                sceUserServiceGetForegroundUser(&foreground_user_id);
                /* Launch params struct (0x20 bytes):
                 *   +0x00  size     = 0x20
                 *   +0x04  user_id
                 *   +0x08  reserved (zeroed) */
                struct {
                    unsigned int size;
                    unsigned int user_id;
                    char         reserved[0x18];
                } launch_params;
                memset(&launch_params, 0, sizeof(launch_params));
                launch_params.size    = 0x20;
                launch_params.user_id = foreground_user_id;
                sceSystemServiceLaunchApp(g_cmd_launch_id, NULL, &launch_params);
                memset(g_cmd_launch_id, 0, sizeof(g_cmd_launch_id));
            }

            /* Refresh library if dirty flag set by another thread */
            if (g_library_dirty) {
                refresh_library();
                g_library_dirty = 0;
            }

            /* Scan all homebrew paths */
            scan_all_paths();

            /* Use config-driven scan interval */
            sleep_us = g_scan_interval * 1000000;
        }

        sceKernelUsleep(sleep_us);
    }

    return 0;
}

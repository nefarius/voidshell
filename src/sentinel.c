/*
 * sentinel.c -- VoidShell sentinel / game watchdog
 *
 * The sentinel watches the foreground application and controls kstuff:
 *
 *   - When a NON-whitelisted game launches: wait DefaultDelay ms, then
 *     disable kstuff (set sysentvec syscall_count to 0xFFFF) so the
 *     game does not see extended syscalls.
 *
 *   - When the game exits / system menu is shown: re-enable kstuff
 *     (0xDEB7) so homebrew functions normally.
 *
 *   - Whitelisted titles (PPSA01650, CUSA00000, NPXS00000 by default)
 *     never cause kstuff to be disabled.
 *
 * State machine states (stored in g_s_current_state):
 *   0x454d4147 = "GAME" -- a game is in the foreground
 *   0x554e454d = "MENU" -- system UI / no game
 *
 * The klog monitor thread (thread_klog_monitor) reads /dev/klog to detect:
 *   - Game launches (OnFocusActiveSceneChanged + title ID)
 *   - Resume from suspend / rest mode
 *   - Control center open/close (NPXS40003 / NPXS40002)
 *
 * The sentinel thread (thread_sentinel_logic) acts on state changes.
 */

#include "sentinel.h"
#include "types.h"
#include "logging.h"
#include "utils.h"
#include "kernel.h"
#include "install.h"
#include "webserver.h"
#include "ps5_sdk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/select.h>
#include <pthread.h>
#include <sys/sysctl.h>
#include <signal.h>

/* Sentinel state constants */
#define STATE_GAME  0x454d4147  /* "GAME" */
#define STATE_MENU  0x554e454d  /* "MENU" */

/* ----------------------------------------------------------
 * Global sentinel state (definitions)
 * ---------------------------------------------------------- */
char g_s_current_id[0x20]  = {0};
int  g_s_current_state     = 0;
int  g_s_is_resume_event   = 0;
int  g_s_in_control_center = 0;
char g_sentinel_override   = 0;
char g_in_rest_mode        = 0;

/* ============================================================
 * get_sentinel_delay  (addr 0x104c40)
 * ============================================================
 * Returns per-game delay from [SentinelGames] if found,
 * otherwise returns g_sentinel_default_delay.
 */
int get_sentinel_delay(const char *title_id)
{
    int i;
    for (i = 0; i < g_sentinel_games_count; i++) {
        if (strcasecmp(g_sentinel_games[i].titleId, title_id) == 0)
            return g_sentinel_games[i].delayMs;
    }
    return g_sentinel_default_delay;
}

/* ============================================================
 * is_sentinel_whitelisted  (addr 0x104cb0)
 * ============================================================ */
bool is_sentinel_whitelisted(const char *title_id)
{
    int i;
    for (i = 0; i < g_whitelist_count; i++) {
        if (strcasecmp(g_whitelist[i], title_id) == 0)
            return true;
    }
    return false;
}

/* ============================================================
 * get_running_app_title
 * ============================================================
 * Title ID of the running "big app" (the single game/app the console runs, whatever has
 * focus). The system functions are linked from libSceSystemService by the SDK (NID import);
 * sceKernelDlsym by plain name does not find them (ESRCH on firmware 13.40).
 */
int get_running_app_title(char *out, size_t size)
{
    static int logged = 0;
    int  app_id;
    int  rc;
    char buf[0x20];

    if (!out || size == 0) return 0;
    out[0] = '\0';

    app_id = sceSystemServiceGetAppIdOfRunningBigApp();
    /* 0 and -1 are the "no big app" answers (-1 seen on firmware 13.40 after the app quit);
     * any other negative value is a system error code: state unknown. */
    if (app_id == 0 || app_id == -1) return 0;
    if (app_id < 0) {
        if (!logged) { log_debug("RUNNING_APP: lookup failed (id=0x%x)", app_id); logged = 1; }
        return -1;
    }

    memset(buf, 0, sizeof(buf));
    rc = sceSystemServiceGetAppTitleId(app_id, buf, sizeof(buf) - 1);
    if (rc != 0 || buf[0] == '\0') {
        log_debug("RUNNING_APP: title lookup for app %d failed (rc=0x%x)", app_id, rc);
        return -1;
    }

    strncpy(out, buf, size - 1);
    out[size - 1] = '\0';
    return 1;
}

/* ============================================================
 * check_running_app_state  (addr 0x102890)
 * ============================================================
 * Called from the stats monitor thread on first tick.
 * Detects if a game is already running at daemon startup.
 */
void check_running_app_state(void)
{
    char title_id[0x20];
    size_t len;

    pthread_mutex_lock(&g_sentinel_mutex);

    /* Only run if no game is currently tracked */
    if (g_s_current_id[0] != '\0' || g_s_current_state == STATE_GAME) {
        pthread_mutex_unlock(&g_sentinel_mutex);
        return;
    }
    pthread_mutex_unlock(&g_sentinel_mutex);

    memset(title_id, 0, sizeof(title_id));
    if (get_running_app_title(title_id, sizeof(title_id)) != 1) return;

    len = strlen(title_id);
    if (len <= 8) return;

    pthread_mutex_lock(&g_sentinel_mutex);
    strncpy(g_s_current_id, title_id, sizeof(g_s_current_id) - 1);
    g_s_current_state = STATE_GAME;
    pthread_mutex_unlock(&g_sentinel_mutex);
}

/* ============================================================
 * smart_wait  (addr 0x104d50)
 * ============================================================
 * Wait up to delay_ms milliseconds in 100ms intervals.
 * Returns true if full delay elapsed, false if interrupted.
 */
bool smart_wait(int delay_ms)
{
    int  ticks_total, ticks_max;
    int  i;
    int  cur_state;
    char cur_flag;

    ticks_total = delay_ms / 100;
    ticks_max   = (ticks_total < 1) ? 1 : ticks_total;

    sceKernelUsleep(100000);

    pthread_mutex_lock(&g_sentinel_mutex);
    cur_flag  = (char)(g_s_current_state != STATE_GAME);
    cur_state = g_s_current_state;
    pthread_mutex_unlock(&g_sentinel_mutex);

    if (cur_flag || cur_state != STATE_GAME)
        return false;

    for (i = 1; i < ticks_max; i++) {
        if (g_sentinel_override)
            return (i >= ticks_total);

        if (i >= ticks_total)
            return true;

        sceKernelUsleep(100000);

        pthread_mutex_lock(&g_sentinel_mutex);
        cur_flag  = (char)(g_s_current_state != STATE_GAME);
        cur_state = g_s_current_state;
        pthread_mutex_unlock(&g_sentinel_mutex);

        if (cur_flag || cur_state != STATE_GAME)
            return false;
    }
    return (i >= ticks_total);
}

/* ============================================================
 * thread_klog_monitor  (addr 0x104e60)
 * ============================================================
 * Reads /dev/klog line-by-line and updates sentinel state on
 * game focus events and suspend/resume events.
 *
 * Key log strings detected:
 *   "OnFocusActiveSceneChanged" + title ID prefix (PPSA/CUSA/etc.)
 *   "NPXS40002" -- control centre closed
 *   "NPXS40003" -- control centre opened
 *   "Power Mode Change: BIG_APP" / "Resume from suspend" / etc.
 *   "Power Mode Change: SUSPEND" / "Entering standby" / etc.
 */
void *thread_klog_monitor(void *arg)
{
    int         klog_fd;
    fd_set      rfds;
    struct timeval tv;
    char        read_buf[4096];
    char        line_buf[4096];
    int         line_pos = 0;
    ssize_t     nread;
    int         i;

    (void)arg;

    klog_fd = open("/dev/klog", O_RDONLY);
    if (klog_fd < 0)
        return NULL;

    while (g_running) {
        FD_ZERO(&rfds);
        FD_SET(klog_fd, &rfds);
        tv.tv_sec  = 2;
        tv.tv_usec = 0;

        if (select(klog_fd + 1, &rfds, NULL, NULL, &tv) <= 0)
            continue;

        nread = read(klog_fd, read_buf, sizeof(read_buf) - 1);
        if (nread < 1) break;

        for (i = 0; i < (int)nread; i++) {
            if (read_buf[i] == '\n') {
                line_buf[line_pos] = '\0';
                line_pos = 0;

                /* --- Suspend / resume detection --- */
                if (strstr(line_buf, "Power Mode Change: BIG_APP")  ||
                    strstr(line_buf, "Resume from suspend")          ||
                    strstr(line_buf, "sceKernelResume")              ||
                    strstr(line_buf, "timer_restore")                ||
                    strstr(line_buf, "apic_timer_recalibrate")) {

                    if (g_in_rest_mode) {
                        g_in_rest_mode = 0;
                        log_debug("REST: Console waking from rest mode");
                        notify_system("VoidShell: Waking up...");
                        sceKernelUsleep(10000000);
                        restart_webserver();
                    }

                } else if (strstr(line_buf, "Power Mode Change: SUSPEND") ||
                           strstr(line_buf, "Entering standby")           ||
                           strstr(line_buf, "sceKernelSuspend")) {

                    if (!g_in_rest_mode) {
                        g_in_rest_mode = 1;
                        log_debug("REST: Console entering rest mode");
                    }
                }

                /* --- Focus / game change detection --- */
                {
                    char *focus_ev = strstr(line_buf, "OnFocusActiveSceneChanged");
                    if (focus_ev) {
                        char *app_line = strstr(focus_ev, "PPSA");
                        if (!app_line) app_line = strstr(focus_ev, "CUSA");
                        if (!app_line) app_line = strstr(focus_ev, "NPXS");

                        if (app_line) {
                            /* Check if it is the control centre */
                            if (strstr(focus_ev, "NPXS40002")) {
                                pthread_mutex_lock(&g_sentinel_mutex);
                                g_s_in_control_center = 0;
                                g_s_current_state = STATE_MENU;
                                pthread_mutex_unlock(&g_sentinel_mutex);
                            } else if (strstr(focus_ev, "NPXS40003")) {
                                pthread_mutex_lock(&g_sentinel_mutex);
                                g_s_in_control_center = 1;
                                g_s_current_state = STATE_MENU;
                                pthread_mutex_unlock(&g_sentinel_mutex);
                            } else {
                                /* Extract the full title ID (9 chars, e.g. PPSA99575) */
                                char new_id[0x10] = {0};
                                size_t j = 0;
                                char c = app_line[0];
                                while (j < 9 && c != '\0' && isalnum((unsigned char)c)) {
                                    new_id[j++] = c;
                                    c = app_line[j];
                                }

                                pthread_mutex_lock(&g_sentinel_mutex);
                                g_s_is_resume_event = (g_s_current_id[0] != '\0' &&
                                                       strncmp(g_s_current_id, new_id, 9) == 0);
                                g_s_current_state     = STATE_GAME;
                                g_s_in_control_center = 0;
                                strncpy(g_s_current_id, new_id, sizeof(g_s_current_id) - 1);
                                pthread_mutex_unlock(&g_sentinel_mutex);
                            }
                        }
                    }
                }

            } else if (line_pos < (int)sizeof(line_buf) - 1) {
                line_buf[line_pos++] = read_buf[i];
            }
        }
    }

    close(klog_fd);
    return NULL;
}

/* ============================================================
 * thread_sentinel_logic  (addr 0x1052d0)
 * ============================================================
 * Main sentinel loop.
 *
 *   State: GAME (non-whitelisted) -> wait delay -> disable kstuff
 *   State: MENU                   -> enable kstuff
 *   State: GAME (whitelisted)     -> enable kstuff (stay on)
 *   Override set                  -> force kstuff ON
 */
void *thread_sentinel_logic(void *arg)
{
    char   prev_state_str[5] = {0};
    char   prev_id[0x20]     = {0};
    char   cur_id[0x20];
    int    cur_state;
    int    is_resume;
    int    kstuff_was_disabled = 0; /* 0 = kstuff ON, 1 = kstuff OFF */
    int    app_close_counter   = 0;
    int    rc;
    int    delay_ms;
    short  cur_val;

    (void)arg;

    sceKernelUsleep(5000000);

    rc = init_kernel_offsets();
    if (!rc) {
        log_startup("SENTINEL: Kernel init failed. Thread exiting.");
        return NULL;
    }
    log_startup("SENTINEL: Warden logic started.");

    /* Start with kstuff enabled */
    if (g_sysentvec) {
        cur_val = (short)kernel_getshort(g_sysentvec + 0x0E);
        if (cur_val != (short)KSTUFF_ON) {
            kernel_setshort(g_sysentvec     + 0x0E, KSTUFF_ON);
            kernel_setshort(g_sysentvec_ps4 + 0x0E, KSTUFF_ON);
            log_debug("SENTINEL: Kstuff state set to ON (Val: 0x%x)", KSTUFF_ON);
        }
    }

    while (g_running) {
        if (g_security_trip)
            return NULL;

        if (g_paused || !g_sentinel_enabled) {
            /* While paused: ensure kstuff is ON */
            if (!kstuff_was_disabled) {
                if (g_sysentvec) {
                    cur_val = (short)kernel_getshort(g_sysentvec + 0x0E);
                    if (cur_val != (short)KSTUFF_ON) {
                        kernel_setshort(g_sysentvec     + 0x0E, KSTUFF_ON);
                        kernel_setshort(g_sysentvec_ps4 + 0x0E, KSTUFF_ON);
                        log_debug("SENTINEL: Kstuff state set to ON (Val: 0x%x)", KSTUFF_ON);
                    }
                }
                log_debug("SENTINEL: Paused (Kstuff Forced ON)");
            }
            sceKernelUsleep(1000000);
            kstuff_was_disabled = 0;
            continue;
        }

        if (g_sentinel_override) {
            /* Override: force kstuff ON */
            if (!kstuff_was_disabled) {
                if (g_sysentvec) {
                    cur_val = (short)kernel_getshort(g_sysentvec + 0x0E);
                    if (cur_val != (short)KSTUFF_ON) {
                        kernel_setshort(g_sysentvec     + 0x0E, KSTUFF_ON);
                        kernel_setshort(g_sysentvec_ps4 + 0x0E, KSTUFF_ON);
                    }
                }
                notify_system("System Secured (kstuff ON)");
            }
            sceKernelUsleep(1000000);
            kstuff_was_disabled = 0;
            continue;
        }

        /* Read current state */
        pthread_mutex_lock(&g_sentinel_mutex);
        cur_state  = g_s_current_state;
        is_resume  = g_s_is_resume_event;
        strncpy(cur_id, g_s_current_id, sizeof(cur_id));
        pthread_mutex_unlock(&g_sentinel_mutex);

        /* No state change — nothing to do */
        if (memcmp(&cur_state, prev_state_str, 4) == 0 &&
            strcmp(cur_id, prev_id) == 0) {
            sceKernelUsleep(100000);
            continue;
        }

        /* Detect app close (MENU state, known previous game, not in control centre) */
        if (!kstuff_was_disabled && cur_state == STATE_MENU && cur_id[0] != '\0' &&
            !g_s_in_control_center) {
            if (app_close_counter < 9) {
                app_close_counter++;
            } else {
                /* Verify the big app is still the same game */
                char check_tid[0x20] = {0};
                app_close_counter = 0;
                if (get_running_app_title(check_tid, sizeof(check_tid)) == 1) {
                    if (strncmp(check_tid, cur_id, 9) != 0) {
                        log_debug("SENTINEL: App Close Detected (%s). Resetting ID.", cur_id);
                        pthread_mutex_lock(&g_sentinel_mutex);
                        g_s_current_id[0] = '\0';
                        pthread_mutex_unlock(&g_sentinel_mutex);
                        cur_id[0] = '\0';
                    }
                }
            }
        } else {
            app_close_counter = 0;
        }

        /* MENU state: ensure kstuff is ON */
        if (cur_state == STATE_MENU) {
            if (!kstuff_was_disabled)
                goto update_prev;

            if (g_sysentvec) {
                cur_val = (short)kernel_getshort(g_sysentvec + 0x0E);
                if (cur_val != (short)KSTUFF_ON) {
                    kernel_setshort(g_sysentvec     + 0x0E, KSTUFF_ON);
                    kernel_setshort(g_sysentvec_ps4 + 0x0E, KSTUFF_ON);
                    log_debug("SENTINEL: Kstuff state set to ON (Val: 0x%x)", KSTUFF_ON);
                }
            }
            notify_system("System Secured (kstuff ON)");
            kstuff_was_disabled = 0;
            goto update_prev;
        }

        /* GAME state: check whitelist */
        if (cur_state == STATE_GAME) {
            if (is_sentinel_whitelisted(cur_id)) {
                /* Whitelisted: keep kstuff ON */
                if (g_sysentvec) {
                    cur_val = (short)kernel_getshort(g_sysentvec + 0x0E);
                    if (cur_val != (short)KSTUFF_ON) {
                        kernel_setshort(g_sysentvec     + 0x0E, KSTUFF_ON);
                        kernel_setshort(g_sysentvec_ps4 + 0x0E, KSTUFF_ON);
                        log_debug("SENTINEL: Kstuff state set to ON (Val: 0x%x)", KSTUFF_ON);
                    }
                }
                notify_system("System Secured (kstuff ON)");
                kstuff_was_disabled = 0;
                goto update_prev;
            }

            /* Non-whitelisted game: wait then disable kstuff */
            if (!kstuff_was_disabled) {
                if (is_resume && g_sentinel_fast_resume) {
                    delay_ms = 3000;
                    log_debug("State: %s (Resume) - Waiting %dms", cur_id, delay_ms);
                } else {
                    delay_ms = get_sentinel_delay(cur_id);
                    log_debug("State: %s (Launch) - Waiting %dms", cur_id, delay_ms);
                }

                if (g_sentinel_debug_notify)
                    notify_system("%s: Pausing kstuff in %d seconds", cur_id, delay_ms / 1000);

                /* Wait with interrupt detection */
                {
                    int   ticks = delay_ms / 100;
                    int   j;
                    int   interrupted = 0;
                    int   st2;

                    if (ticks < 1) ticks = 1;

                    sceKernelUsleep(100000);
                    pthread_mutex_lock(&g_sentinel_mutex);
                    st2 = g_s_current_state;
                    pthread_mutex_unlock(&g_sentinel_mutex);

                    if (st2 != STATE_GAME || g_sentinel_override) {
                        interrupted = 1;
                    } else {
                        for (j = 1; j < ticks; j++) {
                            if (g_sentinel_override) { interrupted = 1; break; }
                            sceKernelUsleep(100000);
                            pthread_mutex_lock(&g_sentinel_mutex);
                            st2 = g_s_current_state;
                            pthread_mutex_unlock(&g_sentinel_mutex);
                            if (st2 != STATE_GAME) { interrupted = 1; break; }
                        }
                    }

                    if (interrupted) goto update_prev;
                }

                /* Disable kstuff */
                if (g_sysentvec) {
                    cur_val = (short)kernel_getshort(g_sysentvec + 0x0E);
                    if (cur_val != (short)KSTUFF_OFF) {
                        kernel_setshort(g_sysentvec     + 0x0E, KSTUFF_OFF);
                        kernel_setshort(g_sysentvec_ps4 + 0x0E, KSTUFF_OFF);
                        log_debug("SENTINEL: Kstuff state set to OFF (Val: 0x%x)", KSTUFF_OFF);
                    }
                }
                notify_system("GAME MODE ACTIVE (kstuff OFF)");
                kstuff_was_disabled = 1;
            }
        }

update_prev:
        memcpy(prev_state_str, &cur_state, 4);
        strncpy(prev_id, cur_id, sizeof(prev_id));
        sceKernelUsleep(100000);
    }

    return NULL;
}

/* ============================================================
 * respring_shell  (addr 0x102b70)
 * ============================================================
 * Finds the SceShellUI process via sysctl kern.proc.all and sends
 * SIGKILL to restart the shell (equivalent to a system "respring").
 */
void respring_shell(void)
{
    int     mib[4];
    size_t  buf_len = 0;
    char   *buf;
    char   *p;

    /* sysctl kern.proc.all: mib = {1, 14, 0, 0} */
    /* The binary uses mib {0x1, 0x15, 0x1500000001} which resolves to
     * CTL_KERN / KERN_PROC / KERN_PROC_ALL with pid=0 flag.
     * Simplified here to the standard BSD form.
     */
    mib[0] = 1;   /* CTL_KERN */
    mib[1] = 14;  /* KERN_PROC */
    mib[2] = 0;   /* KERN_PROC_ALL */
    mib[3] = 0;

    /* First call: get size */
    sysctl(mib, 4, NULL, &buf_len, NULL, 0);

    buf = malloc(buf_len);
    if (!buf) return;

    sysctl(mib, 4, buf, &buf_len, NULL, 0);

    /* Walk kinfo_proc entries */
    p = buf;
    while (p < buf + (int)buf_len) {
        int entry_size = *(int *)p;
        if (entry_size < 1) break;

        /* Process name is at offset 0x1BF (from decompile: (char*)entry + 0x1bf) */
        char *pname = p + 0x1bf;

        if (strcmp(pname, "SceShellUI") == 0) {
            /* PID is at offset 0x48 in kinfo_proc (p->ki_pid) */
            int pid = *(int *)(p + 0x48);
            kill(pid, SIGKILL);
            break;
        }
        p += entry_size;
    }
    free(buf);
}

/* ============================================================
 * trigger_repair  (addr 0x102c50)
 * ============================================================
 * Spawn thread_repair_worker in a detached thread with 1 MB stack.
 */
void trigger_repair(void)
{
    pthread_t      tid;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x100000);
    pthread_create(&tid, &attr, thread_repair_worker, NULL);
    pthread_detach(tid);
    pthread_attr_destroy(&attr);
}

/* ============================================================
 * thread_repair_worker  (addr 0x102cb0)
 * ============================================================
 * Repair worker:
 *   1. Clear all history marker files.
 *   2. Walk /user/app/ and fix any PPSA apps missing appmeta assets.
 *   3. Update app DB for repaired apps.
 *   4. If anything was repaired, respring the shell.
 *   5. Set g_cmd_rescan = 1.
 */
void *thread_repair_worker(void *arg)
{
    DIR           *dir;
    struct dirent *entry;
    char           path[1024];
    char           cur_id[40];
    int            repaired = 0;

    (void)arg;

    log_debug("REPAIR: Started (Verbose Root Mode)");

    /* Clear all history markers */
    dir = opendir("/data/voidshell/history");
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            snprintf(path, sizeof(path), "/data/voidshell/history/%s", entry->d_name);
            unlink(path);
        }
        closedir(dir);
    }

    /* Copy current game id for exclusion */
    pthread_mutex_lock(&g_sentinel_mutex);
    strncpy(cur_id, g_s_current_id, sizeof(cur_id) - 1);
    cur_id[sizeof(cur_id) - 1] = '\0';
    pthread_mutex_unlock(&g_sentinel_mutex);

    /* Allocate work buffers */
    char *appmeta_path = malloc(0x400);
    char *icon_appmeta = malloc(0x400);
    char *snd_appmeta  = malloc(0x400);
    char *pic_appmeta  = malloc(0x400);
    char *icon_app     = malloc(0x400);
    char *pic_app      = malloc(0x400);
    char *snd_app      = malloc(0x400);

    dir = opendir("/user/app");
    if (!dir) goto cleanup;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strlen(entry->d_name) <= 8) continue;

        /* Only process PPSA apps (PS5 native) */
        if (strncmp(entry->d_name, "PPSA", 4) != 0) continue;

        /* Skip current foreground game */
        if (strcmp(entry->d_name, cur_id) == 0) continue;

        snprintf(appmeta_path, 0x400, "/user/appmeta/%s", entry->d_name);
        snprintf(icon_appmeta, 0x400, "%s/icon0.png", appmeta_path);
        snprintf(snd_appmeta,  0x400, "%s/snd0.at9",  appmeta_path);

        /* Check if appmeta assets are present */
        if (access(icon_appmeta, F_OK) == 0 && access(snd_appmeta, F_OK) == 0)
            continue;

        log_debug("REPAIR: Fixing %s...", entry->d_name);

        snprintf(icon_app, 0x400, "/user/app/%s/sce_sys/icon0.png", entry->d_name);
        if (access(icon_app, F_OK) != 0) continue;

        /* Create appmeta dir and copy icon */
        mkdir(appmeta_path, 0777);
        if (access(icon_appmeta, F_OK) != 0)
            copy_file(icon_app, icon_appmeta);

        /* Copy pic0.png */
        snprintf(pic_app,     0x400, "/user/app/%s/sce_sys/pic0.png", entry->d_name);
        snprintf(pic_appmeta, 0x400, "%s/pic0.png", appmeta_path);
        if (access(pic_app, F_OK) == 0 && access(pic_appmeta, F_OK) != 0)
            copy_file(pic_app, pic_appmeta);

        /* Copy snd0.at9 */
        snprintf(snd_app,    0x400, "/user/app/%s/sce_sys/snd0.at9", entry->d_name);
        if (access(snd_app, F_OK) == 0 && access(snd_appmeta, F_OK) != 0)
            copy_file(snd_app, snd_appmeta);

        /* Fix permissions */
        chmod(appmeta_path, 0777);
        snprintf(path, sizeof(path), "%s/icon0.png", appmeta_path); chmod(path, 0777);
        snprintf(path, sizeof(path), "%s/snd0.at9",  appmeta_path); chmod(path, 0777);
        snprintf(path, sizeof(path), "%s/pic0.png",  appmeta_path); chmod(path, 0777);

        update_app_db(entry->d_name);
        repaired = 1;
    }
    closedir(dir);

cleanup:
    free(appmeta_path); free(icon_appmeta); free(snd_appmeta); free(pic_appmeta);
    free(icon_app); free(pic_app); free(snd_app);

    if (repaired)
        respring_shell();

    g_cmd_rescan = 1;
    return NULL;
}

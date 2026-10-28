/*
 * globals.c — VoidShell global variable definitions
 *
 * All variables declared extern in types.h are defined here,
 * except those owned by their logical module:
 *   config.c   : g_scan_interval, g_sentinel_*, g_whitelist/blacklist, etc.
 *   sentinel.c : g_s_current_*, g_sentinel_override, g_in_rest_mode
 *   kernel.c   : g_sysentvec, g_sysentvec_ps4, f_sceNotificationSend
 *   webserver.c: g_pkg_status_log, g_pkg_list_json
 *   install.c  : user_table_count, user_tables
 */

#include "types.h"
#include "ps5_sdk.h"
#include <string.h>
#include <stddef.h>

/* ----------------------------------------------------------
 * Main loop control
 * ---------------------------------------------------------- */
volatile int  g_running        = 1;
volatile char g_paused         = 0;
volatile char g_cmd_rescan     = 0;
volatile char g_library_dirty  = 0;
char          g_cmd_launch_id[0x20] = {0};
char          g_ip_address[0x20]    = "127.0.0.1";

/* ----------------------------------------------------------
 * Mutexes and thread handles
 * ---------------------------------------------------------- */
pthread_mutex_t g_lib_mutex;
pthread_mutex_t g_sentinel_mutex;
pthread_mutex_t g_stats_mutex;
pthread_t       g_tid_webserver;

/* ----------------------------------------------------------
 * System stats (updated by thread_stats_monitor)
 * ---------------------------------------------------------- */
int  g_temp_cpu            = 0;
int  g_temp_soc            = 0;
char g_sys_uptime[0x40]    = "Unknown";

/* ----------------------------------------------------------
 * Install / session tracking
 * ---------------------------------------------------------- */
int  g_session_count               = 0;
char g_session_installed[200][0x20];
char g_security_trip               = 0;

/* ----------------------------------------------------------
 * Library (populated by refresh_library in webserver.c)
 * ---------------------------------------------------------- */
char g_library[LIBRARY_MAX_ENTRIES * LIBRARY_ENTRY_STRIDE];
int  g_lib_count  = 0;
int  g_stat_total = 0;
int  g_stat_ps5   = 0;
int  g_stat_ps4   = 0;

/* ----------------------------------------------------------
 * Path cache (512 slots × 0x421 bytes each)
 * Layout: [path_string 0x420 bytes][valid_byte 1 byte]
 * ---------------------------------------------------------- */
char cache[512 * 0x421];

/* ----------------------------------------------------------
 * KERNEL_ADDRESS_DATA_BASE
 *
 * Must be a GLOBAL (non-weak) symbol so the exploit loader patches it.
 * The loader scans the ELF symbol table for GLOBAL KERNEL_ADDRESS_*
 * symbols and writes the correct kernel base addresses before main()
 * runs. A WEAK symbol is skipped by the loader and stays 0, causing
 * all kernel r/w primitives to fail.
 *
 * --allow-multiple-definition in LDFLAGS handles the crt1.o conflict.
 * ---------------------------------------------------------- */
uint64_t KERNEL_ADDRESS_DATA_BASE = 0;

/* ---- image_mount globals ---- */
image_mount_entry_t image_cache[MAX_IMAGE_MOUNTS];
bool g_img_mount_ro        = true;
int  g_img_scan_interval   = 30;
int  g_img_stability_wait  = 5;
char g_img_backend[8]      = "lvd";
int  g_img_scan_paths_count = 0;
char g_img_scan_paths[16][1024];

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

/* ============================================================
 * VoidShell — Shared Types and Globals
 * Binary:  voidshell.elf  (x86-64, FreeBSD/PS5)
 * Version: V1.1
 * Author:  VoidWhisper
 * ============================================================ */

#define VOIDSHELL_VERSION  "V1.1"

/* Full SemVer of this fork build, reported as "semver" by GET /api/version (upstream has no such field,
 * which is how clients tell the fork apart while "version" stays V1.1). The release build overrides it with
 * -DVOIDSHELL_SEMVER from the tag (see the Makefile and Dockerfile). */
#ifndef VOIDSHELL_SEMVER
#define VOIDSHELL_SEMVER   "1.1.1-nefarius.4"
#endif

/* ----------------------------------------------------------
 * game_info struct — layout recovered from get_game_info_extended()
 *
 *  +0x000  titleId[0x20]         -- "PPSA01234"
 *  +0x020  titleName[0x100]      -- display name (UTF-8)
 *  +0x120  contentVersion[0x10]  -- "01.00" / "00"
 *  +0x130  regionCode[4+1]       -- first 4 chars of titleId (e.g. "PPSA")
 *  +0x134  <padding>
 * ---------------------------------------------------------- */
typedef struct {
    char titleId[0x20];          /* e.g. "PPSA01234"           */
    char titleName[0x100];       /* localised display name      */
    char contentVersion[0x10];   /* "01.00"                    */
    char regionCode[5];          /* first 4 chars + NUL        */
} game_info_t;

/* ----------------------------------------------------------
 * Library entry layout (recovered from refresh_library / handle_client)
 *
 * Each entry is 0x4be bytes.  g_library holds up to 200 entries.
 * Field offsets are relative to the start of an entry.
 *
 *  +0x000  titleId[0x20]          (g_library + n*0x4be + 0x000)
 *  +0x020  <padding to 0x4a5>
 *  +0x4a5  titleName[0x80]        (offset 0x2d1730 in global = base+0x000)
 *          wait — refresh_library stores:
 *            base+lVar5+0x000    = titleId   strncpy 0x1f
 *            base+lVar5+0x4a5    = contentVersion
 *            base+lVar5+0x4a0    = type "PS5" / "PS4"
 *            base+lVar5+0x4bd    = exists flag (bool)
 *            base+lVar5+0x2d1bc5 = fullPath  snprintf "/user/app/%s"
 *  The structure is large; we expose the direct globals instead.
 * ---------------------------------------------------------- */
#define LIBRARY_MAX_ENTRIES  200
#define LIBRARY_ENTRY_STRIDE 0x4be

/* ----------------------------------------------------------
 * Sentinel game entry (from [SentinelGames] config section)
 * stride 0x24, max 50 entries
 * ---------------------------------------------------------- */
typedef struct {
    char  titleId[0x20];  /* game title ID        */
    int   delayMs;        /* delay override (ms)  */
} sentinel_game_t;

/* ----------------------------------------------------------
 * AutoPayload entry (from [AutoPayloads] config section)
 * stride 0x48, max 50 entries
 * ---------------------------------------------------------- */
typedef struct {
    char  filename[0x40];  /* payload filename in /data/voidshell/payloads */
    int   delayMs;         /* sleep before sending (converted to us)       */
    bool  enabled;         /* true if entry is valid                       */
} autorun_payload_t;

/* ----------------------------------------------------------
 * Config globals (recovered from load_config / main)
 * ---------------------------------------------------------- */
extern int  g_scan_interval;        /* [Settings] ScanInterval (seconds)   */
extern int  g_stable_window;        /* [Settings] StableWindow             */
extern bool g_enable_db_patch;      /* [Settings] EnableDBPatch            */
extern bool g_enable_web_server;    /* [Settings] EnableWebServer          */
extern bool g_auto_kill;            /* [Settings] AutoKill                 */

extern bool g_fan_enabled;          /* [FanControl] Enabled                */
extern int  g_fan_target_temp;      /* [FanControl] TargetTemp (°C)        */

extern bool g_sentinel_enabled;     /* [Sentinel] EnableSentinel           */
extern bool g_sentinel_fast_resume; /* [Sentinel] FastResume               */
extern bool g_sentinel_debug_notify;/* [Sentinel] ShowDebugNotify          */
extern int  g_sentinel_default_delay;/* [Sentinel] DefaultDelay (ms)       */

/* Whitelist / blacklist / game arrays */
#define MAX_WHITELIST    50
#define MAX_BLACKLIST    50
#define MAX_SENTINEL_GAMES 50
#define MAX_AUTOPAYLOADS   50
#define MAX_CUSTOM_PATHS   50

extern int  g_whitelist_count;
extern char g_whitelist[MAX_WHITELIST][0x20];

extern int  g_blacklist_count;
extern char g_blacklist[MAX_BLACKLIST][0x80];

extern int            g_sentinel_games_count;
extern sentinel_game_t g_sentinel_games[MAX_SENTINEL_GAMES];

extern int              g_autopayload_count;
extern autorun_payload_t g_autopayloads[MAX_AUTOPAYLOADS];

extern int  g_custom_paths_count;
extern char g_custom_paths[MAX_CUSTOM_PATHS][0x400];

/* ----------------------------------------------------------
 * Runtime state globals
 * ---------------------------------------------------------- */
extern volatile int  g_running;       /* main loop guard (1 = run)          */
extern volatile char g_paused;        /* scanner paused flag                */
extern volatile char g_cmd_rescan;    /* command: trigger rescan            */
extern char          g_cmd_launch_id[0x20]; /* command: title ID to launch  */
extern volatile char g_library_dirty; /* set when library needs refresh     */
extern char          g_ip_address[0x20]; /* detected local IP               */
extern int           g_session_count; /* number of apps installed this session */

extern pthread_t     g_tid_webserver; /* web server thread handle           */
extern pthread_mutex_t g_sentinel_mutex;
extern pthread_mutex_t g_lib_mutex;
extern pthread_mutex_t g_stats_mutex;

/* ----------------------------------------------------------
 * Sentinel state (protected by g_sentinel_mutex)
 * ---------------------------------------------------------- */
extern char g_s_current_id[0x20];      /* title ID of foreground game       */
extern int  g_s_current_state;         /* 0x454d4147 = "GAME", 0x554e454d = "MENU" */
extern int  g_s_is_resume_event;       /* non-zero on resume               */
extern int  g_s_in_control_center;     /* 1 when control centre is open    */
extern char g_sentinel_override;       /* manual override flag             */
extern char g_in_rest_mode;            /* console in rest/suspend mode     */

/* ----------------------------------------------------------
 * Library / stats
 * ---------------------------------------------------------- */
extern char g_library[LIBRARY_MAX_ENTRIES * LIBRARY_ENTRY_STRIDE];
extern int  g_lib_count;   /* total entries in g_library   */
extern int  g_stat_total;
extern int  g_stat_ps5;
extern int  g_stat_ps4;

extern int  g_temp_cpu;          /* CPU temperature (degrees)     */
extern int  g_temp_soc;          /* SoC temperature (degrees)     */
extern char g_sys_uptime[0x40];  /* formatted uptime string       */

/* ----------------------------------------------------------
 * Kernel exploit state (kstuff)
 * ---------------------------------------------------------- */
extern uint64_t g_sysentvec;      /* kernel address of sysentvec struct     */
extern uint64_t g_sysentvec_ps4;  /* same for PS4-compat sysentvec          */
extern uint64_t KERNEL_ADDRESS_DATA_BASE; /* kernel data base (kstuff)      */

/* Security trip wire — set if exploit fails */
extern char g_security_trip;

/* ----------------------------------------------------------
 * PKG server
 * ---------------------------------------------------------- */
extern char g_pkg_status_log[0x400]; /* last PKG install status string      */
extern char g_pkg_list_json[0x3e800];/* JSON list of available PKGs         */

/* ----------------------------------------------------------
 * Scan path table (NULL-terminated array of char pointers)
 * Default paths populated from kstuff/etaHEN paths at startup.
 * ---------------------------------------------------------- */
extern char *SCAN_PATHS[];  /* default scan paths (NULL-terminated)         */

/* ----------------------------------------------------------
 * Session tracking
 * ---------------------------------------------------------- */
extern char g_session_installed[200][0x20]; /* title IDs installed this boot */

/* ----------------------------------------------------------
 * DB patch helper
 * ---------------------------------------------------------- */
extern int   user_table_count;
extern char  user_tables[10][0x100];

/* ----------------------------------------------------------
 * Path-cache for processed paths (avoids re-processing)
 * Layout: [path_string 0x420 bytes][valid_byte 1 byte] * 512 slots
 * ---------------------------------------------------------- */
extern char cache[512 * 0x421];

/* ----------------------------------------------------------
 * Scan-path sentinel data (recovered from thread_sentinel_logic)
 * ---------------------------------------------------------- */
/* Sentinel override flag written by REST API */
/* g_sentinel_override above */

/* ----------------------------------------------------------
 * Notification helper imported at runtime via dlsym
 * ---------------------------------------------------------- */
extern void *f_sceNotificationSend;

/* ---- Image mount types (ported from ShadowMountPlus) ---- */
#define MAX_IMAGE_MOUNTS   64
#define IMAGE_MOUNT_BASE   "/data/imgmnt"

typedef enum {
    IMAGE_FS_UNKNOWN = 0,
    IMAGE_FS_EXFAT,
    IMAGE_FS_UFS,       /* .ffpkg */
    IMAGE_FS_PFS,       /* .ffpfs */
} image_fs_type_e;

typedef enum {
    ATTACH_BACKEND_NONE = 0,
    ATTACH_BACKEND_LVD,
    ATTACH_BACKEND_MD,
} attach_backend_e;

typedef struct {
    char             path[1024];        /* absolute source image path   */
    char             mount_point[1024]; /* mount directory              */
    int              unit_id;           /* lvdN / mdN unit number       */
    attach_backend_e backend;
    bool             valid;
} image_mount_entry_t;

extern image_mount_entry_t image_cache[MAX_IMAGE_MOUNTS];

/* Image mount config (set by config.c, read by image_mount.c) */
extern bool g_img_mount_ro;          /* [ImageMount] ReadOnly      */
extern int  g_img_scan_interval;     /* [ImageMount] ScanInterval  */
extern int  g_img_stability_wait;    /* [ImageMount] StabilityWait */
extern char g_img_backend[8];        /* [ImageMount] Backend "lvd"/"md" */
extern int  g_img_scan_paths_count;
extern char g_img_scan_paths[16][1024]; /* [ImageMount] ScanPath= lines */

#endif /* TYPES_H */

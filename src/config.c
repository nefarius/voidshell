/*
 * config.c — VoidShell INI configuration parser
 *
 * Config file: /data/voidshell/config.ini
 *
 * The parser is a line-by-line state machine that tracks the current
 * [Section] and dispatches key=value pairs to the appropriate globals.
 * Lines beginning with '#' are comments and are ignored.
 *
 * Section indices used internally:
 *   0 = [Settings]
 *   1 = [CustomPaths]
 *   2 = [Blacklist]
 *   3 = [Sentinel]
 *   4 = [SentinelWhitelist]
 *   5 = [SentinelGames]
 *   6 = [AutoPayloads]
 *   7 = [FanControl]
 *   8 = [ImageMount]
 */

#include "config.h"
#include "types.h"
#include "fan.h"
#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

/* ============================================================
 * Global config state (definitions)
 * ============================================================ */
int  g_scan_interval        = 5;
int  g_stable_window        = 10;
bool g_enable_db_patch      = true;
bool g_enable_web_server    = true;
bool g_auto_kill            = true;

bool g_fan_enabled          = false;
int  g_fan_target_temp      = 60;

bool g_sentinel_enabled     = true;
bool g_sentinel_fast_resume = true;
bool g_sentinel_debug_notify= true;
int  g_sentinel_default_delay = 10000;

int  g_whitelist_count      = 0;
char g_whitelist[MAX_WHITELIST][0x20];

int  g_blacklist_count      = 0;
char g_blacklist[MAX_BLACKLIST][0x80];

int            g_sentinel_games_count = 0;
sentinel_game_t g_sentinel_games[MAX_SENTINEL_GAMES];

int              g_autopayload_count  = 0;
autorun_payload_t g_autopayloads[MAX_AUTOPAYLOADS];

int  g_custom_paths_count   = 0;
char g_custom_paths[MAX_CUSTOM_PATHS][0x400];

/* Config file modification time (used to detect external changes) */
static time_t g_config_mtime = 0;

/* ============================================================
 * Default config content
 * ============================================================ */
static const char DEFAULT_CONFIG[] =
    "# VOIDSHELL CONFIGURATION\n"
    "[Settings]\n"
    "ScanInterval=5\n"
    "StableWindow=10\n"
    "EnableDBPatch=true\n"
    "EnableWebServer=true\n"
    "AutoKill=true\n"
    "\n"
    "# FAN CONTROL\n"
    "[FanControl]\n"
    "Enabled=false\n"
    "TargetTemp=60\n"
    "\n"
    "# SENTINEL CONFIGURATION\n"
    "[Sentinel]\n"
    "EnableSentinel=true\n"
    "DefaultDelay=10000\n"
    "FastResume=true\n"
    "ShowDebugNotify=true\n"
    "\n"
    "[SentinelWhitelist]\n"
    "PPSA01650\n"
    "CUSA00000\n"
    "NPXS00000\n"
    "\n"
    "[SentinelGames]\n"
    "\n"
    "[AutoPayloads]\n"
    "# Filename=DelayMs\n"
    "\n"
    "[CustomPaths]\n"
    "\n"
    "[Blacklist]\n"
    "\n"
    "# IMAGE MOUNT\n"
    "[ImageMount]\n"
    "ReadOnly=1\n"
    "ScanInterval=30\n"
    "StabilityWait=5\n"
    "Backend=lvd\n"
    "# ScanPath=/mnt/usb0\n";

/* ============================================================
 * create_default_config  (addr 0x100930)
 * ============================================================ */
void create_default_config(void)
{
    FILE *fp = fopen("/data/voidshell/config.ini", "w");
    if (!fp)
        return;
    fwrite(DEFAULT_CONFIG, 1, sizeof(DEFAULT_CONFIG) - 1, fp);
    fclose(fp);
}

/* ============================================================
 * load_config  (addr 0x100990)
 * ============================================================
 * Parse /data/voidshell/config.ini.
 * Creates file with defaults if it does not exist.
 * Applies fan control ioctl if [FanControl] Enabled=true.
 */
void load_config(void)
{
    FILE *fp;
    char  line[512];
    char *p, *eq, *val;
    int   section = 0; /* default = [Settings] */
    struct stat st;

    /* ---- Reset to defaults before re-parsing ---- */
    g_scan_interval          = 5;
    g_stable_window          = 10;
    g_enable_db_patch        = true;
    g_enable_web_server      = true;

    g_sentinel_enabled       = true;
    g_sentinel_fast_resume   = true;
    g_sentinel_default_delay = 10000;

    g_fan_enabled            = false;
    g_fan_target_temp        = 60;

    /* ---- Create default file if absent ---- */
    if (access("/data/voidshell/config.ini", F_OK) != 0) {
        fp = fopen("/data/voidshell/config.ini", "w");
        if (fp) {
            fwrite(DEFAULT_CONFIG, 1, sizeof(DEFAULT_CONFIG) - 1, fp);
            fclose(fp);
        }
    }

    /* Record modification time */
    if (stat("/data/voidshell/config.ini", &st) == 0)
        g_config_mtime = st.st_mtime;

    /* ---- Open for reading ---- */
    fp = fopen("/data/voidshell/config.ini", "r");
    if (!fp)
        return;

    while (fgets(line, sizeof(line), fp)) {
        /* Trim leading/trailing whitespace */
        p = line;
        while (isspace((unsigned char)*p)) p++;
        {
            char *end = p + strlen(p);
            while (end > p && isspace((unsigned char)*(end - 1)))
                end--;
            *end = '\0';
        }

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == '#')
            continue;

        /* ---- Section header ---- */
        if (*p == '[') {
            if (strncasecmp(p, "[Settings]",       10) == 0) { section = 0; continue; }
            if (strncasecmp(p, "[CustomPaths]",    13) == 0) { section = 1; continue; }
            if (strncasecmp(p, "[Blacklist]",      11) == 0) { section = 2; continue; }
            if (strncasecmp(p, "[Sentinel]",       10) == 0) { section = 3; continue; }
            if (strncasecmp(p, "[SentinelWhitelist]", 19) == 0) { section = 4; continue; }
            if (strncasecmp(p, "[SentinelGames]",  15) == 0) { section = 5; continue; }
            if (strncasecmp(p, "[AutoPayloads]",   14) == 0) { section = 6; continue; }
            if (strncasecmp(p, "[FanControl]",     12) == 0) { section = 7; continue; }
            if (strncasecmp(p, "[ImageMount]",     12) == 0) { section = 8; continue; }
            /* Unknown section — stay in current section */
            continue;
        }

        /* ---- Key=Value dispatch ---- */
        switch (section) {

        case 0: /* [Settings] */
            eq = strchr(p, '=');
            if (!eq) { section = 0; break; }
            *eq = '\0';
            val = eq + 1;
            while (isspace((unsigned char)*val)) val++;

            if (strcasecmp(p, "ScanInterval") == 0)
                g_scan_interval = atoi(val);
            else if (strcasecmp(p, "StableWindow") == 0)
                g_stable_window = atoi(val);
            else if (strcasecmp(p, "EnableDBPatch") == 0)
                g_enable_db_patch = (strcasecmp(val, "true") == 0);
            else if (strcasecmp(p, "EnableWebServer") == 0)
                g_enable_web_server = (strcasecmp(val, "true") == 0);
            else if (strcasecmp(p, "AutoKill") == 0)
                g_auto_kill = (strcasecmp(val, "true") == 0);
            break;

        case 1: /* [CustomPaths] — one path per line */
            if (g_custom_paths_count < MAX_CUSTOM_PATHS) {
                /* Deduplicate */
                int dup = 0;
                for (int i = 0; i < g_custom_paths_count; i++) {
                    if (strcmp(g_custom_paths[i], p) == 0) { dup = 1; break; }
                }
                if (!dup)
                    strncpy(g_custom_paths[g_custom_paths_count++], p, 0x3ff);
            }
            break;

        case 2: /* [Blacklist] — one title ID per line */
            if (g_blacklist_count < MAX_BLACKLIST)
                strncpy(g_blacklist[g_blacklist_count++], p, 0x7f);
            break;

        case 3: /* [Sentinel] */
            eq = strchr(p, '=');
            if (!eq) { section = 3; break; }
            *eq = '\0';
            val = eq + 1;
            while (isspace((unsigned char)*val)) val++;

            if (strcasecmp(p, "EnableSentinel") == 0)
                g_sentinel_enabled = (strcasecmp(val, "true") == 0);
            else if (strcasecmp(p, "FastResume") == 0)
                g_sentinel_fast_resume = (strcasecmp(val, "true") == 0);
            else if (strcasecmp(p, "ShowDebugNotify") == 0)
                g_sentinel_debug_notify = (strcasecmp(val, "true") == 0);
            else if (strcasecmp(p, "DefaultDelay") == 0)
                g_sentinel_default_delay = atoi(val);
            break;

        case 4: /* [SentinelWhitelist] — one title ID per line */
            if (g_whitelist_count < MAX_WHITELIST)
                strncpy(g_whitelist[g_whitelist_count++], p, 0x1f);
            break;

        case 5: /* [SentinelGames] — TitleId=DelayMs */
            eq = strchr(p, '=');
            if (!eq) break;
            *eq = '\0';
            val = eq + 1;
            if (g_sentinel_games_count < MAX_SENTINEL_GAMES) {
                strncpy(g_sentinel_games[g_sentinel_games_count].titleId, p, 0x1f);
                g_sentinel_games[g_sentinel_games_count].delayMs = atoi(val);
                g_sentinel_games_count++;
            }
            break;

        case 6: /* [AutoPayloads] — Filename=DelayMs */
            eq = strchr(p, '=');
            if (!eq) break;
            *eq = '\0';
            val = eq + 1;
            if (g_autopayload_count < MAX_AUTOPAYLOADS) {
                strncpy(g_autopayloads[g_autopayload_count].filename, p, 0x3f);
                g_autopayloads[g_autopayload_count].delayMs = atoi(val);
                g_autopayloads[g_autopayload_count].enabled = true;
                g_autopayload_count++;
            }
            break;

        case 7: /* [FanControl] */
            eq = strchr(p, '=');
            if (!eq) break;
            *eq = '\0';
            val = eq + 1;
            while (isspace((unsigned char)*val)) val++;

            if (strcasecmp(p, "Enabled") == 0)
                g_fan_enabled = (strcasecmp(val, "true") == 0);
            else if (strcasecmp(p, "TargetTemp") == 0)
                g_fan_target_temp = atoi(val);
            break;

        case 8: /* [ImageMount] */
            eq = strchr(p, '=');
            if (!eq) break;
            *eq = '\0';
            val = eq + 1;
            while (isspace((unsigned char)*val)) val++;

            if (strcasecmp(p, "ReadOnly") == 0)
                g_img_mount_ro = (atoi(val) != 0);
            else if (strcasecmp(p, "ScanInterval") == 0)
                g_img_scan_interval = atoi(val);
            else if (strcasecmp(p, "StabilityWait") == 0)
                g_img_stability_wait = atoi(val);
            else if (strcasecmp(p, "Backend") == 0)
                snprintf(g_img_backend, sizeof(g_img_backend), "%s", val);
            else if (strcasecmp(p, "ScanPath") == 0 && g_img_scan_paths_count < 16)
                snprintf(g_img_scan_paths[g_img_scan_paths_count++], 1024, "%s", val);
            break;
        }
    }

    fclose(fp);

    /* Apply fan control immediately if enabled */
    if (g_fan_enabled)
        apply_fan_control();
}

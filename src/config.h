#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h — VoidShell INI config parser
 *
 * Config file: /data/voidshell/config.ini
 *
 * Sections parsed:
 *   [Settings]          -- scan interval, feature flags
 *   [FanControl]        -- fan speed control
 *   [Sentinel]          -- game watchdog settings
 *   [SentinelWhitelist] -- title IDs that bypass sentinel (one per line)
 *   [SentinelGames]     -- per-game delay overrides (TitleId=DelayMs)
 *   [AutoPayloads]      -- payloads to send on boot (Filename=DelayMs)
 *   [CustomPaths]       -- extra homebrew scan paths (one per line)
 *   [Blacklist]         -- title IDs to ignore entirely (one per line)
 */

/**
 * Load (or create-and-load) /data/voidshell/config.ini.
 *
 * If the file does not exist it is created with default values.
 * Parses all sections and updates the corresponding globals defined
 * in types.h.
 * After parsing, if [FanControl] Enabled=true, the fan ioctl is
 * applied immediately.
 */
void load_config(void);

/**
 * Write the default config file to /data/voidshell/config.ini.
 * Called by load_config() when the file is missing.
 */
void create_default_config(void);

/*
 * Default config content (written verbatim when file is absent):
 *
 * # VOIDSHELL CONFIGURATION
 * [Settings]
 * ScanInterval=5
 * StableWindow=10
 * EnableDBPatch=true
 * EnableWebServer=true
 * AutoKill=true
 *
 * [FanControl]
 * Enabled=false
 * TargetTemp=60
 *
 * [Sentinel]
 * EnableSentinel=true
 * DefaultDelay=10000
 * FastResume=true
 * ShowDebugNotify=true
 *
 * [SentinelWhitelist]
 * PPSA01650
 * CUSA00000
 * NPXS00000
 *
 * [SentinelGames]
 *
 * [AutoPayloads]
 * # Filename=DelayMs
 *
 * [CustomPaths]
 *
 * [Blacklist]
 */

#endif /* CONFIG_H */

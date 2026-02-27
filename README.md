# VoidShell V1.1

A PS5 homebrew management daemon. Runs as a background process after jailbreak, providing a web-based dashboard for game library management, PKG installation, payload injection, and system monitoring.

---

## Requirements

| Requirement | Notes |
|---|---|
| PS5 with jailbreak (etaHEN) | kstuff-compatible firmware |
| [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) | Installed at `/opt/ps5-payload-sdk` |
| WSL2 (Ubuntu 24.04) | Or any Linux with the SDK installed |
| `nc` or `socat` | For sending the payload to the PS5 |

---

## Project Structure

```
voidshell_src/
├── src/
│   ├── main.c          # Entry point, startup sequence, main scan loop
│   ├── webserver.c     # HTTP REST API (port 7007) + PKG server (port 12576)
│   ├── sentinel.c      # Game watchdog — manages kstuff state per-app
│   ├── kernel.c        # kstuff kernel exploit interface (r/w primitives)
│   ├── install.c       # Homebrew installation pipeline (nullfs, SQLite DB patch)
│   ├── config.c        # INI config parser (/data/voidshell/config.ini)
│   ├── fan.c           # Fan speed control via /dev/icc_fan ioctl
│   ├── logging.c       # Timestamped logging to /data/voidshell/logs/
│   ├── utils.c         # String utilities, JSON helpers, PS5 notifications
│   ├── globals.c       # Global variable definitions
│   ├── types.h         # Shared types, struct definitions, extern declarations
│   └── ps5_sdk.h       # PS5 SDK function forward declarations
├── index.html          # Web dashboard (React SPA — copy to PS5)
├── Makefile
└── deploy.sh           # One-command build + deploy script
```

---

## Building

### First-time SDK setup

Install the ps5-payload-sdk into WSL2 (Ubuntu 24.04):

```bash
# Follow the official SDK install instructions, then verify:
ls /opt/ps5-payload-sdk/toolchain/prospero.mk
```

If `PS5_PAYLOAD_SDK` is not in your PATH, add it to `~/.bashrc`:

```bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
```

### Build

Open a WSL2 terminal in the `voidshell_src/` directory:

```bash
# Full build
make

# Clean + rebuild
make clean && make

# Build ELF + copy dashboard to build/www/
make all-deploy
```

Output: `build/voidshell.elf`

> **Note:** Use `bash -i` if `PS5_PAYLOAD_SDK` is only set in `.bashrc`:
> ```bash
> bash -i -c "make clean && make"
> ```

---

## Deploying to PS5

### Option 1 — deploy.sh (recommended)

```bash
# Build + send in one command
./deploy.sh 192.168.1.17

# Send without rebuilding
./deploy.sh 192.168.1.17 --no-build
```

The script:
1. Builds `build/voidshell.elf`
2. Copies `index.html` → `build/www/index.html`
3. Sends the ELF to the PS5 via `nc` (falls back to `socat`)
4. Prints the dashboard URL on success

### Option 2 — manual

```bash
# Send the ELF payload (PS5 must be jailbroken, etaHEN listening on 9021)
nc -w 5 192.168.1.17 9021 < build/voidshell.elf

# Or with socat
socat - TCP:192.168.1.17:9021 < build/voidshell.elf
```

### Copy the dashboard

Copy `index.html` to the PS5 at `/data/voidshell/www/index.html`. This only needs to be done once (or when the dashboard is updated). You can do this via FTP, SSH, or any file transfer method available on your jailbreak.

---

## Dashboard

Once VoidShell is running on the PS5, open a browser on any device on the same network:

```
http://<ps5-ip>:7007
```

| Feature | Description |
|---|---|
| **Home** | System stats, game library, featured game carousel |
| **PKG** | Upload and install PKG files, remote URL install |
| **Payloads** | Inject ELF payloads, configure auto-run on startup |
| **Settings** | Fan control, Sentinel Warden config, blacklist/whitelist |
| **Logs** | Live system log viewer with filter and auto-scroll |

---

## Configuration

VoidShell reads `/data/voidshell/config.ini` on startup. A default config is created automatically on first run.

```ini
[Settings]
ScanInterval=5          ; Library scan interval (seconds)
EnableWebServer=true    ; Enable the web dashboard
AutoKill=false          ; Kill previous app before launching

[FanControl]
Enabled=false
TargetTemp=60           ; Target temperature in °C

[Sentinel]
EnableSentinel=true     ; Disable kstuff when non-whitelisted games run
DefaultDelay=10000      ; ms to wait before disabling kstuff
ResumeDelay=3000        ; ms to wait on app resume
ShowDebugNotify=false

[SentinelWhitelist]
; Title IDs that are allowed to run with kstuff enabled
; PPSA01650

[AutoPayloads]
; filename=delay_ms
; etahen.elf=0

[CustomPaths]
; Additional scan paths (one per line)
; /mnt/usb0/games
```

---

## API Reference

The REST API runs on port 7007.

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/stats` | System stats (temps, uptime, active game, session count, version) |
| GET | `/api/library` | Full installed game library (PS5 + PS4) |
| GET | `/api/payloads` | Available ELF/BIN files in `/data/voidshell/payloads/` |
| GET | `/api/session` | Titles installed this boot `{"count":N,"installed":[...]}` |
| GET | `/api/version` | `{"version":"V1.1"}` |
| GET | `/api/config_raw` | Raw config.ini text |
| GET | `/api/logs` | Last 64 KB of debug log |
| POST | `/api/logs/clear` | Clear debug log |
| POST | `/api/launch` | Launch app by title ID (body: `PPSA01234`) |
| POST | `/api/send_payload` | Inject payload by filename (body: `etahen.elf`) |
| POST | `/api/rescan` | Trigger library rescan |
| POST | `/api/pause` | Toggle scanner pause |
| POST | `/api/repair` | Trigger icon/metadata repair |
| POST | `/api/notify` | Send a PS5 notification popup (body: message text) |
| POST | `/api/save_ini` | Save config (body: raw INI text) |

PKG install server runs on port **12576** and is used internally by the dashboard.

---

## How It Works

```
[PS5 Jailbreak]
       |
       v
 voidshell.elf loaded by etaHEN / kstuff
       |
       +-- setuid(0) + kernel_set_ucred_authid (privilege escalation)
       +-- init_work_dirs()     /data/voidshell/{www,logs,payloads}
       +-- init_kernel_offsets() firmware detection + kstuff setup
       +-- load_config()        /data/voidshell/config.ini
       +-- detect_local_ip()    UDP connect trick → getsockname
       |
       +-- [Thread] thread_web_server       port 7007  REST API + dashboard
       +-- [Thread] thread_pkg_server       port 12576 PKG upload + install
       +-- [Thread] thread_stats_monitor    CPU/SoC temps, uptime (2s)
       +-- [Thread] thread_autorun_payloads send configured payloads at boot
       +-- [Thread] thread_klog_monitor     /dev/klog → detect game launches
       +-- [Thread] thread_sentinel_logic   manage kstuff ON/OFF per game
       |
       +-- [Main loop] scan homebrew paths, refresh library, handle commands
```

**Sentinel Warden**: When a non-whitelisted game runs, VoidShell disables kstuff (sets syscall count to `0xFFFF`) after a configurable delay. This prevents game anti-cheat from detecting the exploit. On return to the menu, kstuff is re-enabled (`0xDEB7`).

---

## Version History

| Version | Changes |
|---|---|
| V1.1 | Extended date window (all of 2026), function cleanup, new API endpoints, improved dashboard |
| V1.00 Pre-Beta | Initial beta release (Feb 8–20 2026 window) |

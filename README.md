# VoidShell

> **Fork note (nefarius/voidshell).** This fork only adds a fix for VoidShell not loading on firmware that does not
> export `sceAppInstUtilAppInstallTitleDir` (seen on 13.40; upstream PR
> [#2](https://github.com/amilarajans/voidshell/pull/2)), plus a pinned `Dockerfile`. Releases are tagged
> `v<upstream>-nefarius.N`. Build it yourself with `docker build --output dist .` (outputs `voidshell.elf`,
> `index.html`, `SHA256SUMS`). No support promise; use upstream once it carries the fix.


**PS5 homebrew management daemon** — runs after jailbreak and exposes a web dashboard for library management, PKG installs, payload injection, disk-image mounts, fan control, and system monitoring.

[![Build](https://github.com/amilarajans/voidshell/actions/workflows/build-release.yml/badge.svg)](https://github.com/amilarajans/voidshell/actions/workflows/build-release.yml)
[![Release](https://img.shields.io/github/v/release/amilarajans/voidshell?include_prereleases)](https://github.com/amilarajans/voidshell/releases/latest)

| | |
|---|---|
| **Version** | V1.1 |
| **Target** | Jailbroken PS5 (etaHEN / kstuff-compatible firmware) |
| **Dashboard** | `http://<ps5-ip>:7007` |
| **Binary** | `voidshell.elf` |

---

## Features

- **Web dashboard** — library browser, live temps/uptime, session tracking
- **PKG manager** — upload, install from URL, manage local PKG files
- **Payload runner** — inject ELF/BIN payloads; configure auto-run at boot
- **Image mounts** — mount `.exfat`, `.ffpfs`, `.ffpkg` images for game storage
- **Sentinel Warden** — toggles kstuff per-app so non-whitelisted titles are less likely to trip anti-cheat
- **Fan control** — target-temperature fan management via `/dev/icc_fan`
- **Library tools** — rescan, icon/metadata repair, custom scan paths
- **REST API** — full control from any device on the LAN

---

## Quick start (prebuilt release)

1. Jailbreak the PS5 (etaHEN / compatible stack listening on **port 9021**).
2. Download the latest **`voidshell.elf`** (and `index.html`) from  
   **[Releases](https://github.com/amilarajans/voidshell/releases/latest)**.
3. Send the payload:

```bash
nc -w 5 <ps5-ip> 9021 < voidshell.elf
# or
socat - TCP:<ps5-ip>:9021 < voidshell.elf
```

4. Copy the **built** dashboard once (or whenever it changes). Get it from a
   [Release](https://github.com/amilarajans/voidshell/releases/latest) (`index.html`)
   or build it locally (`make www` → `www/index.html`):

```text
index.html  (release)  or  www/index.html  (local build)
        →  /data/voidshell/www/index.html
```

Use FTP, SSH, or any file transfer your jailbreak provides.

5. Open the dashboard:

```text
http://<ps5-ip>:7007
```

---

## Requirements (build from source)

| Requirement | Notes |
|---|---|
| PS5 with jailbreak (etaHEN) | kstuff-compatible firmware |
| [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) | Install under `/opt/ps5-payload-sdk` |
| Linux or WSL2 (Ubuntu 24.04 recommended) | Host for the cross-toolchain |
| `clang` / `lld` (SDK docs recommend clang-18) | Used by the Prospero toolchain |
| `nc` or `socat` | Optional — for `deploy.sh` |

---

## Project structure

```text
voidshell/
├── src/                 # PS5 daemon (C)
├── ui/                  # Preact + TypeScript dashboard (built with Bun)
│   ├── src/             # App, tabs, components
│   ├── template.html    # Shell + Tailwind CDN
│   └── build.ts         # Bundles JS into a single HTML file
├── Makefile
├── deploy.sh            # Build + send to PS5
└── .github/workflows/   # CI: ELF + frontend → GitHub Release
```

Generated at build time (gitignored): `www/index.html`, `build/`.

### Frontend

The dashboard is a **Bun-compiled Preact** app (`ui/`). The built file is **not** committed — run the build (or CI) to produce it. Output is a single HTML file with no React/Babel CDN dependency (Tailwind still loads from CDN).

```bash
cd ui
bun install
bun run build          # → www/index.html (local, gitignored)

# or from repo root (also stages build/www/index.html for deploy):
make www
```
---

## Building

### Install the SDK

```bash
# Binary SDK distribution (easiest)
wget https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
sudo unzip -d /opt ps5-payload-sdk.zip

export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
# Optional: add the export to ~/.bashrc
```

Prerequisites (Debian/Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y bash clang-18 lld-18 wget unzip make
```

### Compile

```bash
make              # → build/voidshell.elf
make clean && make
make all-deploy   # ELF + build dashboard into build/www/
```

If `PS5_PAYLOAD_SDK` is only set in an interactive shell:

```bash
bash -i -c "make clean && make"
```

---

## Deploying to a PS5

### One-shot script

```bash
./deploy.sh 192.168.1.17            # build + www + send
./deploy.sh 192.168.1.17 --no-build # send existing ELF
```

The script builds the ELF, stages `build/www/index.html`, and sends the payload to port **9021**.

### Manual

```bash
nc -w 5 192.168.1.17 9021 < build/voidshell.elf
```

Remember to place the built dashboard at `/data/voidshell/www/index.html` when updating the UI.

---

## Dashboard

| Tab / area | Description |
|---|---|
| **Home** | Temps, uptime, library, session stats |
| **PKG** | Upload / install PKG files and remote URLs |
| **Payloads** | List and inject ELF/BIN from `/data/voidshell/payloads/` |
| **Images** | View / unmount mounted storage images |
| **Settings** | Fan, Sentinel, raw `config.ini` editor |
| **Logs** | Live log viewer |

---

## Configuration

Config path: **`/data/voidshell/config.ini`** (created with defaults on first run).

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
; Title IDs allowed to run with kstuff enabled
; PPSA01650

[AutoPayloads]
; filename=delay_ms
; etahen.elf=0

[CustomPaths]
; Extra library scan paths
; /mnt/usb0/games
```

Working directories created at runtime:

```text
/data/voidshell/
├── www/        # dashboard
├── logs/       # debug / startup logs
├── payloads/   # ELF/BIN drop folder
└── config.ini
```

---

## REST API

Base URL: `http://<ps5-ip>:7007`

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/stats` | Temps, uptime, focused title (`active_game`), running title (`running_app`, empty when nothing runs), counts, version |
| GET | `/api/library` | Installed PS5 + PS4 library |
| GET | `/api/payloads` | Files in `/data/voidshell/payloads/` |
| GET | `/api/session` | Titles installed this boot |
| GET | `/api/version` | `{"version":"V1.1"}` |
| GET | `/api/config_raw` | Raw `config.ini` |
| GET | `/api/logs` | Last ~64 KB of debug log |
| GET | `/api/images` | Currently mounted images |
| POST | `/api/logs/clear` | Truncate debug log |
| POST | `/api/launch` | Launch by title ID (body: `PPSA01234`) |
| POST | `/api/send_payload` | Inject payload by filename |
| POST | `/api/rescan` | Rescan library |
| POST | `/api/pause` | Toggle scanner pause |
| POST | `/api/repair` | Icon / metadata repair |
| POST | `/api/notify` | On-console notification |
| POST | `/api/save_ini` | Save config + reload |
| POST | `/api/images/unmount` | Unmount image by path |

PKG helper endpoints run on a separate port used by the dashboard (see `webserver.c` / UI for the active port).

> **Security note:** The API is open on the LAN by design for homebrew convenience. Do not expose a jailbroken PS5 to untrusted networks.

---

## How it works

```text
[PS5 Jailbreak]
       |
       v
 voidshell.elf  (etaHEN / elfldr on :9021)
       |
       +-- privilege setup (setuid / authid)
       +-- /data/voidshell/{www,logs,payloads}
       +-- firmware offsets + kstuff init
       +-- load config.ini
       +-- detect LAN IP
       |
       +-- [Thread] web server      :7007  REST + dashboard
       +-- [Thread] PKG server             upload / install
       +-- [Thread] stats monitor          CPU/SoC temps, uptime
       +-- [Thread] autorun payloads       boot-time injects
       +-- [Thread] klog monitor           game launch detection
       +-- [Thread] sentinel               kstuff ON/OFF per title
       +-- [Thread] image scan             disk image discovery
       |
       +-- [Main]   library scan loop + command handling
```

**Sentinel Warden** — for non-whitelisted titles, VoidShell disables kstuff after a configurable delay (syscall table marker `0xFFFF`). On return to the menu, kstuff is re-enabled (`0xDEB7`). Whitelisted title IDs keep kstuff active.

---

## CI / releases

GitHub Actions (`.github/workflows/build-release.yml`):

- Builds the **Preact dashboard** with Bun from `ui/` (output is not stored in git)
- Builds `voidshell.elf` with the official **ps5-payload-sdk**
- Uploads build artifacts for download from the Actions run
- On version tags (`v*`), publishes a **GitHub Release** with:
  - `voidshell.elf`
  - `index.html` (built Preact dashboard)
  - `SHA256SUMS`

Create a release from a tag:

```bash
git tag v1.1.0
git push origin v1.1.0
```

---

## Version history

| Version | Changes |
|---|---|
| **V1.1** | Broader date window, API/dashboard improvements, image mount (`.exfat` / `.ffpfs` / `.ffpkg`) |
| **V1.00 Pre-Beta** | Initial beta (Feb 2026 window) |

---

## Disclaimer

This project is intended for **homebrew research and personal use** on consoles you own. Jailbreaking and running unsigned code may void warranties and violate terms of service. You are responsible for complying with local law and platform rules. No warranty is provided — use at your own risk.

---

## Credits

- Original VoidShell design / binary lineage: **VoidWhisper**
- Toolchain: [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk)
- Jailbreak ecosystem: etaHEN / kstuff and related community work

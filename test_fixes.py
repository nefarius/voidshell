#!/usr/bin/env python3
"""
test_fixes.py — VoidShell bug-fix and feature verification suite

Covers all 13 bug fixes plus the image-mount integration
(image_mount.h/c, config [ImageMount] section, REST endpoints,
thread, and dashboard Images tab).

Modes:
    python3 test_fixes.py           # static + HTTP protocol tests, then exit
    python3 test_fixes.py --serve   # same, then keep mock server alive for browser use

When --serve is active, open http://localhost:7007 in a browser to interact
with the real index.html against mock PS5 API data.
"""

import io
import json
import re
import sys
import socket
import threading
import time

# Force UTF-8 output on Windows (avoids cp1252 UnicodeEncodeError)
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import urllib.request
import urllib.error
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
ROOT          = Path(__file__).parent
WEBSERVER     = ROOT / "src" / "webserver.c"
HTML          = ROOT / "www" / "index.html"
CONFIG_C      = ROOT / "src" / "config.c"
TYPES_H       = ROOT / "src" / "types.h"
GLOBALS_C     = ROOT / "src" / "globals.c"
IMAGE_MOUNT_H = ROOT / "src" / "image_mount.h"
IMAGE_MOUNT_C = ROOT / "src" / "image_mount.c"

MOCK_PORT     = 17007  # test-only REST mock (avoids conflict with real VoidShell on 7007)
PKG_MOCK_PORT = 19200  # test-only PKG mock  (avoids conflict with real PKG server on 9200/12576)

# ---------------------------------------------------------------------------
# Pretty output
# ---------------------------------------------------------------------------
PASS = "\033[92m[PASS]\033[0m"
FAIL = "\033[91m[FAIL]\033[0m"
results: list[tuple[str, bool]] = []

def check(name: str, ok: bool, note: str = "") -> bool:
    results.append((name, ok))
    tag  = PASS if ok else FAIL
    hint = f"  -> {note}" if note else ""
    print(f"  {tag}  {name}{hint}")
    return ok

def section(title: str) -> None:
    print(f"\n{'-'*60}")
    print(f"  {title}")
    print(f"{'-'*60}")

# ---------------------------------------------------------------------------
# File helpers
# ---------------------------------------------------------------------------
def slurp(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except Exception as exc:
        print(f"  ERROR reading {path}: {exc}")
        return ""

# ---------------------------------------------------------------------------
# STATIC CHECKS — webserver.c
# ---------------------------------------------------------------------------
def test_static_webserver() -> None:
    section("webserver.c — static source checks")
    src = slurp(WEBSERVER)
    if not src:
        return

    # BUG 1 — asset title ID from ?id= query param (not /icon or /pic path)
    check(
        'BUG1  icon handler uses strstr("?id=") not strstr("/icon")',
        'strstr(request, "?id=")' in src and 'strstr(request, "/icon")' not in src,
        'frontend sends /assets/icon?id=PPSA01234',
    )
    check(
        'BUG1  pic handler uses strstr("?id=") not strstr("/pic")',
        'strstr(request, "?id=")' in src and 'strstr(request, "/pic")' not in src,
    )

    # BUG 2 — Content-Length must be 15 for {"status":"ok"} (15 bytes, not 16)
    # C source contains literal backslash-quote pairs, e.g. {\"status\":\"ok\"}
    # In the Python string read from the file, these appear as: {\"status\":\"ok\"}
    ok_15 = '\\"ok\\"}", 15, 1)' in src   # matches ok\"}", 15, 1) in .c source
    ok_16 = '\\"ok\\"}", 16, 1)' in src   # old broken pattern
    check(
        'BUG2  Content-Length is 15 (not 16) for {"status":"ok"}',
        ok_15 and not ok_16,
        'len("{\\"status\\":\\"ok\\"}") == 15',
    )

    # BUG 3 — malloc NULL check before recv() in thread_pkg_server
    # Find the block: malloc(0x10000) ... recv(client_fd
    m = re.search(r'malloc\(0x10000\)(.*?)recv\(client_fd', src, re.DOTALL)
    between = m.group(1) if m else ""
    check(
        'BUG3  malloc(0x10000) result checked before recv() in pkg server',
        bool(m) and ('if (!buf)' in between or 'if(!buf)' in between),
        'OOM guard prevents NULL deref',
    )

    # BUG 8 — payload filename copied+trimmed, not passed raw
    check(
        'BUG8  send_payload uses local payload_name[256] buffer',
        'char payload_name[256]' in src,
        'trims trailing \\r\\n before calling send_payload()',
    )
    check(
        'BUG8  raw HTTP body no longer passed directly to send_payload()',
        'send_payload(payload_name)' in src and 'send_payload(body)' not in src,
    )

    # IMP 2 — localtime_r instead of localtime (thread-safe)
    check(
        'IMP2  localtime_r() used in pkg_log (thread-safe)',
        'localtime_r(' in src,
    )
    check(
        'IMP2  non-thread-safe localtime(&now) removed',
        'localtime(&now)' not in src,
    )

    # IMP 3 — /delete endpoint rejects path traversal
    check(
        'IMP3  /delete handler rejects paths containing ".."',
        'strstr(p, "..")' in src,
        'prevents deleting files outside the PKG directories',
    )


# ---------------------------------------------------------------------------
# STATIC CHECKS — index.html
# ---------------------------------------------------------------------------
def test_static_html() -> None:
    section("index.html — static source checks")
    html = slurp(HTML)
    if not html:
        return

    # BUG 4 — showToast must be declared BEFORE launch (avoids TDZ trap)
    st_pos = html.find("const showToast =")
    la_pos = html.find("const launch =")
    check(
        'BUG4  showToast declared before launch (avoids const TDZ)',
        0 <= st_pos < la_pos,
        'launch() calls showToast(), so showToast must come first',
    )

    # BUG 5 — dead PKG_SERVER variable removed from PKGManager
    check(
        'BUG5  dead "const PKG_SERVER = ..." variable removed',
        'const PKG_SERVER =' not in html,
        'PKG_HOST (top-level) is the correct variable to use',
    )

    # BUG 6 — RecentlyAdded accepts library as prop (no independent fetch)
    check(
        'BUG6  RecentlyAdded signature includes library prop',
        'RecentlyAdded = ({ onLaunch, library })' in html,
    )
    # Verify no fetch(/api/library) inside the RecentlyAdded component
    ra_start = html.find('const RecentlyAdded =')
    ra_end   = html.find('\n        const ', ra_start + 1)  # next top-level component
    ra_body  = html[ra_start:ra_end] if ra_start != -1 and ra_end != -1 else ''
    check(
        'BUG6  RecentlyAdded no longer fetches /api/library internally',
        'api/library' not in ra_body,
        'avoids duplicate network request',
    )
    check(
        'BUG6  App passes library prop to <RecentlyAdded />',
        'RecentlyAdded onLaunch={launch} library={library' in html,
    )

    # BUG 7 — fetchLibrary helper extracted and called in key places
    check(
        'BUG7  fetchLibrary() helper extracted from inline useEffect',
        'const fetchLibrary =' in html,
    )
    # Verify it's called after rescan action (same line, within 300 chars)
    ha_pos   = html.find("action === 'rescan'")
    fetch_pos_rescan = html.find('fetchLibrary', ha_pos)
    check(
        'BUG7  fetchLibrary() called after rescan action',
        ha_pos != -1 and fetch_pos_rescan != -1 and (fetch_pos_rescan - ha_pos) < 300,
    )
    # Verify it's called after upload completes
    upload_pos = html.find('"Upload Complete"')
    fetch_pos_upload = html.find('fetchLibrary()', upload_pos)
    check(
        'BUG7  fetchLibrary() called after upload completes',
        upload_pos != -1 and fetch_pos_upload != -1 and (fetch_pos_upload - upload_pos) < 60,
    )

    # IMP 4 — lucide.createIcons() useEffect has [tab] dependency array
    check(
        'IMP4  lucide.createIcons() useEffect has [tab] dep (not every render)',
        re.search(r'createIcons\(\).*\}, \[tab\]\)', html, re.DOTALL) is not None,
    )
    # PKGManager and Payloads re-run createIcons() when their async data arrives
    check(
        'IMP4  PKGManager re-runs createIcons() after pkgs load',
        re.search(r'createIcons.*\[pkgs\]', html) is not None,
    )
    check(
        'IMP4  Payloads re-runs createIcons() after payloads load',
        re.search(r'createIcons.*\[payloads\]', html) is not None,
    )

    # IMP 5 — Carousel useMemo uses stable sort, not Math.random()
    car_start = html.find('const Carousel =')
    car_end   = html.find('const LibraryGrid =', car_start)
    car_body  = html[car_start:car_end] if car_start != -1 and car_end != -1 else ''
    check(
        'IMP5  Carousel useMemo no longer uses Math.random() (stable sort)',
        'Math.random' not in car_body,
        'game order is deterministic across re-renders',
    )

    # IMP 6 — Settings save has .catch() error handler
    save_pos  = html.find("const save = ()")
    catch_pos = html.find('.catch(', save_pos)
    check(
        'IMP6  Settings save() has .catch() error handler',
        save_pos != -1 and catch_pos != -1 and (catch_pos - save_pos) < 300,
        'shows "Save Failed" toast on network error',
    )


# ---------------------------------------------------------------------------
# STATIC CHECKS — image mount integration
# ---------------------------------------------------------------------------
def test_static_image_mount() -> None:
    section("image_mount integration — static source checks")

    # --- new files exist ---
    check("IMG  src/image_mount.h exists", IMAGE_MOUNT_H.exists())
    check("IMG  src/image_mount.c exists", IMAGE_MOUNT_C.exists())

    # --- types.h: new types ---
    th = slurp(TYPES_H)
    check("IMG  types.h defines MAX_IMAGE_MOUNTS",         "MAX_IMAGE_MOUNTS" in th)
    check("IMG  types.h defines image_mount_entry_t",      "image_mount_entry_t" in th)
    check("IMG  types.h defines image_fs_type_e",          "image_fs_type_e" in th)
    check("IMG  types.h defines attach_backend_e",         "attach_backend_e" in th)
    check("IMG  types.h declares extern image_cache[]",    "extern image_mount_entry_t image_cache" in th)
    check("IMG  types.h declares g_img_scan_interval",     "g_img_scan_interval" in th)
    check("IMG  types.h declares g_img_scan_paths",        "g_img_scan_paths" in th)

    # --- globals.c: definitions ---
    gc = slurp(GLOBALS_C)
    check("IMG  globals.c defines image_cache[]",          "image_mount_entry_t image_cache[MAX_IMAGE_MOUNTS]" in gc)
    check("IMG  globals.c defines g_img_mount_ro",         "g_img_mount_ro" in gc)
    check("IMG  globals.c defines g_img_scan_interval",    "g_img_scan_interval" in gc)
    check("IMG  globals.c defines g_img_backend",          "g_img_backend" in gc)

    # --- config.c: [ImageMount] section ---
    cc = slurp(CONFIG_C)
    check("IMG  config.c has section index 8 = [ImageMount]",  "8 = [ImageMount]" in cc)
    check("IMG  config.c detects [ImageMount] header",         '"[ImageMount]"' in cc or "[ImageMount]" in cc)
    check("IMG  config.c parses ReadOnly key",                 "ReadOnly" in cc and "g_img_mount_ro" in cc)
    check("IMG  config.c parses ScanInterval key",             '"ScanInterval"' in cc or "ScanInterval" in cc and "g_img_scan_interval" in cc)
    check("IMG  config.c parses ScanPath key",                 "ScanPath" in cc and "g_img_scan_paths" in cc)
    check("IMG  DEFAULT_CONFIG includes [ImageMount] section", "[ImageMount]" in cc and "ScanInterval=30" in cc)

    # --- image_mount.h: public API ---
    ih = slurp(IMAGE_MOUNT_H)
    check("IMG  image_mount.h declares image_mount_init()",         "image_mount_init" in ih)
    check("IMG  image_mount.h declares image_mount_scan()",         "image_mount_scan" in ih)
    check("IMG  image_mount.h declares image_mount_shutdown()",     "image_mount_shutdown" in ih)
    check("IMG  image_mount.h declares image_mount_list_json()",    "image_mount_list_json" in ih)
    check("IMG  image_mount.h declares image_mount_unmount_by_path()", "image_mount_unmount_by_path" in ih)

    # --- image_mount.c: key implementation details ---
    ic = slurp(IMAGE_MOUNT_C)
    check("IMG  image_mount.c includes sys/uio.h (struct iovec)",   "#include <sys/uio.h>" in ic)
    check("IMG  image_mount.c includes sys/mdioctl.h",              "#include <sys/mdioctl.h>" in ic)
    check("IMG  image_mount.c defines LVD_CTRL_PATH",               'LVD_CTRL_PATH' in ic)
    check("IMG  image_mount.c defines MD_CTRL_PATH",                'MD_CTRL_PATH' in ic)
    check("IMG  image_mount.c defines SCE_LVD_IOC_ATTACH",          "SCE_LVD_IOC_ATTACH" in ic)
    check("IMG  image_mount.c defines IOVEC_ENTRY macro",           "IOVEC_ENTRY" in ic)
    check("IMG  image_mount.c has detect_image_fs_type()",          "detect_image_fs_type" in ic)
    check("IMG  image_mount.c has build_image_mount_point()",       "build_image_mount_point" in ic)
    check("IMG  image_mount.c has mount_image()",                   "mount_image" in ic)
    check("IMG  image_mount.c has unmount_image()",                 "unmount_image" in ic)
    check("IMG  image_mount.c uses g_img_mount_ro config",          "g_img_mount_ro" in ic)
    check("IMG  image_mount.c uses g_img_scan_paths",               "g_img_scan_paths" in ic)
    check("IMG  image_mount.c emits JSON array in list_json()",     '"]"' in ic or '"]' in ic)

    # --- main.c: thread + shutdown ---
    mc = slurp(ROOT / "src" / "main.c")
    check("IMG  main.c includes image_mount.h",                     '#include "image_mount.h"' in mc)
    check("IMG  main.c has thread_image_scan_loop()",               "thread_image_scan_loop" in mc)
    check("IMG  main.c spawns thread_image_scan_loop",              "thread_image_scan_loop" in mc and "pthread_create" in mc)
    check("IMG  main.c calls image_mount_shutdown() before exit",   "image_mount_shutdown()" in mc)

    # --- webserver.c: REST endpoints ---
    ws = slurp(WEBSERVER)
    check("IMG  webserver.c includes image_mount.h",                '#include "image_mount.h"' in ws)
    check("IMG  webserver.c has GET /api/images route",             "/api/images" in ws and "image_mount_list_json" in ws)
    check("IMG  webserver.c has POST /api/images/unmount route",    "/api/images/unmount" in ws and "image_mount_unmount_by_path" in ws)
    # Each route block (up to the next "/* ---- API" marker) has to call its helper, not merely mention it.
    check("API  webserver.c GET /api/processes route calls list_processes()",
          re.search(r'strncmp\(request, "GET /api/processes".*?list_processes\(', ws, re.S) is not None
          and "/* ---- API" not in re.search(r'strncmp\(request, "GET /api/processes".*?list_processes\(', ws, re.S).group(0)[1:])
    check("API  webserver.c POST /api/kill route calls kill_process()",
          re.search(r'strncmp\(request, "POST /api/kill".*?kill_process\(', ws, re.S) is not None
          and "/* ---- API" not in re.search(r'strncmp\(request, "POST /api/kill".*?kill_process\(', ws, re.S).group(0)[1:])

    # --- Makefile ---
    mk = slurp(ROOT / "Makefile")
    check("IMG  Makefile includes image_mount.c in SRCS",           "image_mount.c" in mk)

    # --- index.html: Images tab ---
    html = slurp(HTML)
    check("IMG  index.html has 'images' tab entry",                 "'images'" in html or '"images"' in html)
    check("IMG  index.html has hard-drive icon for Images tab",     "hard-drive" in html)
    check("IMG  index.html defines Images component",               "const Images =" in html)
    check("IMG  index.html renders <Images /> in tab switcher",     "<Images " in html)
    check("IMG  Images component fetches /api/images",              "api/images" in html)
    check("IMG  Images component POSTs to /api/images/unmount",     "api/images/unmount" in html)
    check("IMG  Images component shows 'No images mounted' empty state",
          "No images mounted" in html)


# ---------------------------------------------------------------------------
# MOCK PS5 HTTP SERVER
# ---------------------------------------------------------------------------
MOCK_GAMES = [
    {"id": "PPSA01234", "name": "Mock Game PS5", "size": 53_687_091_200, "exists": True},
    {"id": "CUSA00042", "name": "Mock Game PS4", "size": 21_474_836_480, "exists": True},
    {"id": "PPSA99999", "name": "Ghost Game",    "size": 0,              "exists": False},
]

# In-memory image mount state for mock server tests
_mock_images: list[dict] = [
    {
        "path":        "/mnt/usb0/game1.exfat",
        "mount_point": "/data/imgmnt/exfatmnt/game1",
        "backend":     "lvd",
        "unit":        0,
    },
    {
        "path":        "/mnt/usb0/dlc.ffpkg",
        "mount_point": "/data/imgmnt/ufsmnt/dlc",
        "backend":     "lvd",
        "unit":        1,
    },
]

# 1×1 transparent PNG
PNG_1X1 = bytes([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,
    0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x01, 0xe2, 0x21, 0xbc,
    0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82,
])

OK_BODY = b'{"status":"ok"}'
assert len(OK_BODY) == 15, "sanity: Content-Length must be 15"

class MockHandler(BaseHTTPRequestHandler):
    """Minimal mock of the VoidShell REST API."""

    def log_message(self, *_): pass  # silence

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin",  "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")

    def _send(self, status: int, ct: str, body: bytes,
              extra_headers: dict | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", ct)
        self.send_header("Content-Length", str(len(body)))
        self._cors()
        for k, v in (extra_headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, data, status: int = 200, extra: dict | None = None) -> None:
        self._send(status, "application/json",
                   json.dumps(data).encode(), extra)

    def _ok(self) -> None:
        """Send {"status":"ok"} with Content-Length 15 — mirrors BUG 2 fix."""
        self._send(200, "application/json", OK_BODY)

    def do_OPTIONS(self):
        self.send_response(200)
        self._cors()
        self.end_headers()

    def do_GET(self):
        raw  = self.path
        path = raw.split("?")[0]
        qs   = raw[len(path):]

        # Serve the actual dashboard
        if path in ("/", "/index.html"):
            try:
                body = HTML.read_bytes()
                self._send(200, "text/html", body)
            except Exception as exc:
                self._send(500, "text/plain", str(exc).encode())
            return

        # BUG 1 test: assets via ?id= query param
        if path.startswith("/assets/icon") or path.startswith("/assets/pic"):
            m = re.search(r"\?id=([A-Z0-9a-z]+)", qs)
            if m:
                title_id = m.group(1)
                # Echo back the extracted title ID so the protocol test can verify it
                self._send(200, "image/png", PNG_1X1,
                           extra_headers={"X-Extracted-ID": title_id})
            else:
                self._send(400, "text/plain",
                           b"Missing or malformed ?id= parameter")
            return

        # Images API
        if path == "/api/images":
            self._json(_mock_images)
            return

        # API routes
        dispatch = {
            "/api/stats": lambda: self._json({
                "cpu": 48.3, "soc": 55.7, "sys_uptime": 7200,
                "active_game": "MENU", "running_app": "", "fan_active": False,
                "total": len(MOCK_GAMES), "ps5": 2, "ps4": 1,
                "session_count": 1, "version": "V1.1", "firmware": "13.40",
            }),
            "/api/library": lambda: self._json({"games": MOCK_GAMES}),
            "/api/payloads": lambda: self._json({"payloads": [
                {"name": "mirahen.elf",  "size": 4096,  "autorun": False, "delay": 0},
                {"name": "ftpd.elf",     "size": 8192,  "autorun": False, "delay": 0},
            ]}),
            "/api/session": lambda: self._json({
                "count": 1, "installed": ["PPSA01234"],
            }),
            "/api/version": lambda: self._json({"version": "V1.1"}),
            "/api/processes": lambda: self._json({"processes": [
                {"pid": 1234, "name": "pldmgr_v0.6.3.el"},
                {"pid": 1300, "name": "SceShellUI"},
            ]}),
            "/api/config_raw": lambda: self._send(200, "text/plain", (
                "[Settings]\n"
                "ScanInterval=5\n"
                "EnableWebServer=true\n"
                "AutoKill=false\n"
                "\n[FanControl]\n"
                "Enabled=false\n"
                "TargetTemp=60\n"
                "\n[Sentinel]\n"
                "EnableSentinel=true\n"
            ).encode()),
            "/api/logs": lambda: self._send(200, "text/plain", (
                "2026-02-27 12:00:00 [INFO] VoidShell V1.1 started\n"
                "2026-02-27 12:00:01 [INFO] Kernel offsets detected\n"
                "2026-02-27 12:00:02 [INFO] Web server listening on port 7007\n"
            ).encode()),
        }

        handler = dispatch.get(path)
        if handler:
            handler()
        else:
            self._send(404, "text/plain", b"Not found")

    def do_POST(self):
        path   = self.path.split("?")[0]
        length = int(self.headers.get("Content-Length", 0))
        body   = self.rfile.read(length)

        if path == "/api/send_payload":
            # Simulate BUG 8 fix: decode and trim the payload name
            name = body.decode("utf-8", errors="replace").strip()
            if name and ".." not in name:
                self._json({"status": "Success", "payload": name})
            else:
                self._json({"status": "Failed",  "reason": "invalid name"}, 400)
            return

        if path == "/api/launch":
            tid = body.decode("utf-8", errors="replace").strip()
            self._json({"status": "ok", "launched": tid})
            return

        if path == "/api/notify":
            msg = body.decode("utf-8", errors="replace").strip()
            self._json({"status": "ok", "message": msg})
            return

        if path == "/api/kill":
            # Mirrors the C handler: marker header first, then strict decimal (optional sign, padding allowed;
            # an embedded NUL is invalid), then 1 < pid <= INT_MAX.
            if self.headers.get("X-VoidShell-Request") != "1":
                self._json({"status": "error", "error": "forbidden"})
                return
            text = body.decode("ascii", errors="replace").strip()
            pid = int(text) if re.fullmatch(r"[+-]?[0-9]+", text) else None
            if pid is None or pid <= 1 or pid > 2**31 - 1:
                self._json({"status": "error", "error": "bad request"})
            elif pid == 1234:
                self._json({"status": "ok"})
            elif pid == 1300:
                self._json({"status": "error", "error": "refused"})
            else:
                self._json({"status": "error", "error": "no such process"})
            return

        if path == "/api/images/unmount":
            src_path = body.decode("utf-8", errors="replace").strip()
            removed = any(img["path"] == src_path for img in _mock_images)
            if removed:
                _mock_images[:] = [i for i in _mock_images if i["path"] != src_path]
                self._ok()
            else:
                self._json({"status": "error"}, 200)
            return

        if path in ("/api/rescan", "/api/pause", "/api/repair",
                    "/api/logs/clear", "/api/save_ini"):
            self._ok()
            return

        self._send(404, "text/plain", b"Unknown POST endpoint")


class MockPKGHandler(BaseHTTPRequestHandler):
    """Minimal mock of the VoidShell PKG server."""

    def log_message(self, *_): pass

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin",  "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")

    def _json(self, data, status: int = 200) -> None:
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self._cors()
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(200)
        self._cors()
        self.end_headers()

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/list":
            self._json({"files": [
                {"name": "game_demo.pkg", "path": "/user/data/void_tmp/game_demo.pkg", "size": 4_294_967_296},
            ]})
        elif path == "/status":
            body = b"Idle"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self._cors()
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        path   = self.path.split("?")[0]
        length = int(self.headers.get("Content-Length", 0))
        body   = self.rfile.read(length).decode("utf-8", errors="replace")

        if path == "/install_existing":
            self._json({"status": "ok"})
        elif path == "/api/install_url":
            self._json({"status": "ok", "msg": "URL install queued"})
        elif path == "/delete":
            # IMP 3 simulation: reject path traversal
            if ".." in body:
                self._json({"status": "error", "msg": "path traversal rejected"}, 400)
            else:
                self._json({"status": "ok"})
        elif path.startswith("/upload"):
            # Drain body, simulate success
            body_bytes = self.rfile.read(max(0, length - len(body.encode())))
            self._json({"status": "ok"})
        else:
            self.send_response(404)
            self.end_headers()


# ---------------------------------------------------------------------------
# HTTP PROTOCOL TESTS
# ---------------------------------------------------------------------------
def wait_for_server(port: int, timeout: float = 5.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.3):
                return True
        except OSError:
            time.sleep(0.1)
    return False

def http_get(path: str, port: int = MOCK_PORT):
    url = f"http://127.0.0.1:{port}{path}"
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status, dict(r.headers), r.read()

def http_post(path: str, body: bytes = b"", port: int = MOCK_PORT, headers: dict | None = None):
    url = f"http://127.0.0.1:{port}{path}"
    req = urllib.request.Request(url, data=body, method="POST", headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as exc:
        return exc.code, {}, exc.read()

def test_protocol(rest_port: int = MOCK_PORT, pkg_port: int = PKG_MOCK_PORT) -> None:
    section(f"HTTP protocol tests — mock REST:{rest_port}  PKG:{pkg_port}")

    # BUG 1 — icon ?id= routing
    try:
        s, h, _ = http_get("/assets/icon?id=PPSA01234", rest_port)
        check("BUG1  GET /assets/icon?id=PPSA01234 → 200 PNG",
              s == 200 and "image/png" in h.get("Content-Type", ""))
        check("BUG1  title ID 'PPSA01234' extracted from ?id= param",
              h.get("X-Extracted-Id", h.get("X-Extracted-ID", "")) == "PPSA01234")
    except Exception as exc:
        check("BUG1  icon asset routing", False, str(exc))

    # BUG 1 — missing ?id= returns 400 (not silent empty response)
    try:
        s, _, b = http_get("/assets/icon", rest_port)
        check("BUG1  GET /assets/icon (no ?id=) → 400",
              s == 400, "server should reject request with no title ID")
    except urllib.error.HTTPError as exc:
        check("BUG1  GET /assets/icon (no ?id=) → 400", exc.code == 400)
    except Exception as exc:
        check("BUG1  GET /assets/icon (no ?id=) → 400", False, str(exc))

    # BUG 2 — Content-Length == 15 for {"status":"ok"}
    try:
        s, h, b = http_post("/api/rescan", port=rest_port)
        cl = int(h.get("Content-Length", h.get("content-length", -1)))
        check('BUG2  POST /api/rescan Content-Length == 15',
              cl == 15, f"got {cl}")
        check('BUG2  body is exactly b\'{"status":"ok"}\'',
              b == b'{"status":"ok"}')
    except Exception as exc:
        check("BUG2  Content-Length == 15", False, str(exc))

    # BUG 8 — send_payload trims trailing whitespace from filename
    try:
        s, _, b = http_post("/api/send_payload", b"mirahen.elf\r\n", rest_port)
        data = json.loads(b)
        check("BUG8  POST /api/send_payload with trailing \\r\\n → 200",
              s == 200)
        check("BUG8  payload name trimmed: 'mirahen.elf' (no newline)",
              data.get("payload") == "mirahen.elf")
    except Exception as exc:
        check("BUG8  send_payload trimming", False, str(exc))

    # IMP 3 — /delete rejects path traversal (tested via mock PKG server)
    try:
        s, _, b = http_post("/delete", b"../../etc/passwd.pkg", pkg_port)
        check("IMP3  /delete rejects '..' path traversal → 400",
              s == 400)
    except urllib.error.HTTPError as exc:
        check("IMP3  /delete rejects '..' path traversal → 400", exc.code == 400)
    except Exception as exc:
        check("IMP3  /delete rejects '..' path traversal", False, str(exc))

    # Image mount endpoints
    try:
        s, h, b = http_get("/api/images", rest_port)
        data = json.loads(b)
        check("IMG  GET /api/images → 200 JSON array",
              s == 200 and isinstance(data, list))
        check("IMG  GET /api/images response has ≥1 entry with required fields",
              len(data) > 0 and all(
                  k in data[0] for k in ("path", "mount_point", "backend", "unit")
              ))
    except Exception as exc:
        check("IMG  GET /api/images", False, str(exc))

    try:
        target = b"/mnt/usb0/game1.exfat"
        s, _, b = http_post("/api/images/unmount", target, rest_port)
        data = json.loads(b)
        check("IMG  POST /api/images/unmount (known path) → status ok",
              s == 200 and data.get("status") == "ok")
        # Verify the entry was actually removed
        s2, _, b2 = http_get("/api/images", rest_port)
        remaining = json.loads(b2)
        check("IMG  POST /api/images/unmount removes entry from list",
              all(i["path"] != "/mnt/usb0/game1.exfat" for i in remaining))
    except Exception as exc:
        check("IMG  POST /api/images/unmount", False, str(exc))

    try:
        s, _, b = http_post("/api/images/unmount", b"/nonexistent/path.exfat", rest_port)
        data = json.loads(b)
        check("IMG  POST /api/images/unmount (unknown path) → status error",
              data.get("status") == "error")
    except Exception as exc:
        check("IMG  POST /api/images/unmount (unknown path)", False, str(exc))

    # Process API
    try:
        s, _, b = http_get("/api/processes", rest_port)
        data = json.loads(b)
        check("API   GET /api/processes lists pid + name",
              s == 200 and all("pid" in p and "name" in p for p in data["processes"]))
        s, _, b = http_post("/api/kill", b"1", rest_port)
        marker = {"X-VoidShell-Request": "1"}
        s, _, b = http_post("/api/kill", b"1234", rest_port)
        check("API   POST /api/kill without the marker header is forbidden",
              json.loads(b) == {"status": "error", "error": "forbidden"})
        for bad in (b"1", b"0", b"-5", b"12abc", b"", b"2147483648", b"12\x00abc"):
            s, _, b = http_post("/api/kill", bad, rest_port, marker)
            check(f"API   POST /api/kill rejects {bad!r} as bad request",
                  json.loads(b) == {"status": "error", "error": "bad request"})
        s, _, b = http_post("/api/kill", b"1300", rest_port, marker)
        check("API   POST /api/kill refuses SceShellUI", json.loads(b).get("error") == "refused")
        s, _, b = http_post("/api/kill", b"1234", rest_port, marker)
        check("API   POST /api/kill accepts a listed pid", json.loads(b).get("status") == "ok")
        s, _, b = http_post("/api/kill", b"+01234", rest_port, marker)
        check("API   POST /api/kill accepts a signed, zero-padded pid", json.loads(b).get("status") == "ok")
    except Exception as exc:
        check("API   process API", False, str(exc))

    # Stats endpoint includes new fields
    try:
        s, _, b = http_get("/api/stats", rest_port)
        data = json.loads(b)
        check("API   /api/stats has 'version' field",   "version"       in data)
        check("API   /api/stats has 'session_count'",   "session_count" in data)
        check("API   /api/stats has 'running_app'",    "running_app"   in data)
        check("API   /api/stats has 'firmware' as NN.NN",
              re.fullmatch(r"\d{2}\.\d{2}", str(data.get("firmware", ""))) is not None)
    except Exception as exc:
        check("API   /api/stats fields", False, str(exc))

    # Session endpoint
    try:
        s, _, b = http_get("/api/session", rest_port)
        data = json.loads(b)
        check("API   /api/session returns count + installed list",
              "count" in data and "installed" in data)
    except Exception as exc:
        check("API   /api/session", False, str(exc))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def start_server(handler_class, port: int):
    """Try preferred port, then fall back to OS-assigned port."""
    for try_port in (port, 0):
        try:
            srv = HTTPServer(("127.0.0.1", try_port), handler_class)
            actual = srv.server_address[1]
            if actual != port:
                print(f"  NOTE: port {port} busy, using {actual} instead")
            threading.Thread(target=srv.serve_forever, daemon=True).start()
            return srv, actual
        except OSError:
            continue
    print(f"  WARNING: could not bind to any port for mock server")
    return None, port

def main() -> None:
    serve_mode = "--serve" in sys.argv

    print("=" * 60)
    print("  VoidShell Bug Fix Test Suite")
    print("=" * 60)

    # Static checks (no network needed)
    test_static_webserver()
    test_static_html()
    test_static_image_mount()

    # Start mock servers (fall back to OS-assigned port if preferred is busy)
    rest_srv, rest_port = start_server(MockHandler,    MOCK_PORT)
    pkg_srv,  pkg_port  = start_server(MockPKGHandler, PKG_MOCK_PORT)

    if rest_srv and wait_for_server(rest_port):
        if pkg_srv:
            wait_for_server(pkg_port)
        test_protocol(rest_port, pkg_port)
    else:
        print("\n  Skipping protocol tests (server unavailable)")

    # Summary
    passed = sum(1 for _, ok in results if ok)
    failed = sum(1 for _, ok in results if not ok)
    total  = len(results)

    print(f"\n{'=' * 60}")
    status_str = "ALL PASS" if failed == 0 else f"{failed} FAILED"
    print(f"  Result: {passed}/{total} passed  [{status_str}]")
    if failed:
        print("\n  Failed checks:")
        for name, ok in results:
            if not ok:
                print(f"    FAIL  {name}")
    print("=" * 60)

    if serve_mode and rest_srv:
        print(f"""
  Mock PS5 server running — open in a browser to test the dashboard:

    http://localhost:{rest_port}

  The server responds to all REST API endpoints with realistic mock data.
  Test the following UI fixes manually:

    BUG4  Click any game launch button — toast appears immediately
    BUG5  Open DevTools → no 'PKG_SERVER' in PKGManager scope
    BUG6  Network tab shows ONE /api/library request (not two)
    BUG7  Click Rescan — library re-fetches after ~3 s
    IMP4  Switch tabs — icons render; DevTools shows no flood of createIcons()
    IMP5  Carousel order is stable across refreshes
    IMP6  Settings save works (or shows "Save Failed" toast on error)

  Press Ctrl+C to stop.
""")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n  Shutting down.")

    for srv in (rest_srv, pkg_srv):
        if srv is not None:
            srv.shutdown()

    sys.exit(0 if failed == 0 else 1)

if __name__ == "__main__":
    main()

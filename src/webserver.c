/*
 * webserver.c -- VoidShell HTTP REST server and PKG install server
 *
 * REST server (port 7007):
 *   GET  /               -> serve_dashboard (index.html from /data/voidshell/www/)
 *   GET  /index.html     -> serve_dashboard
 *   GET  /assets/icon/:id  -> serve icon0.png from /user/appmeta/<id>/
 *   GET  /assets/pic/:id   -> serve pic0.png  from /user/appmeta/<id>/
 *   GET  /api/stats        -> JSON: cpu/soc temps, uptime, active game, running app, counts, firmware
 *   GET  /api/library      -> JSON: full installed app library
 *   GET  /api/payloads     -> JSON: .elf/.bin files in /data/voidshell/payloads/
 *   GET  /api/config_raw   -> raw text: /data/voidshell/config.ini
 *   GET  /api/logs         -> last 64KB of debug.log
 *   GET  /api/logs/clear   -> truncate debug.log
 *   POST /api/logs/clear   -> truncate debug.log
 *   POST /api/send_payload -> send ELF payload to 127.0.0.1:15651
 *   GET  /api/processes    -> JSON: running processes (pid, name)
 *   POST /api/kill         -> SIGKILL the process whose pid is the body
 *   POST /api/launch       -> set g_cmd_launch_id
 *   POST /api/rescan       -> set g_cmd_rescan
 *   POST /api/pause        -> toggle g_paused
 *   POST /api/repair       -> trigger_repair()
 *   POST /api/save_ini     -> overwrite config.ini + reload
 *   POST /api/cache/save   -> save a PNG to /data/voidshell/cache/
 *   GET  /api/images       -> JSON: currently mounted images (image_cache)
 *   POST /api/images/unmount -> unmount image by source path (body = path)
 *
 * PKG server (port 12576):
 *   POST /upload               -> upload PKG file, start install
 *   GET  /status               -> current install status string
 *   GET  /list                 -> JSON: available PKG files
 *   POST /install_existing     -> install PKG already on device
 *   POST /api/install_url      -> install PKG from URL
 *   POST /delete               -> delete a PKG file
 *   OPTIONS *                  -> CORS preflight
 */

#include "webserver.h"
#include "types.h"
#include "logging.h"
#include "utils.h"
#include "kernel.h"
#include "install.h"
#include "sentinel.h"
#include "config.h"
#include "image_mount.h"
#include "ps5_sdk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <pthread.h>
#include <sys/sysctl.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>

/* ============================================================
 * Global PKG server state
 * ============================================================ */
char g_pkg_status_log[0x400]   = {0};
char g_pkg_list_json[0x3e800]  = {0};

/* ============================================================
 * Default scan paths (from binary data at 0x238078 area)
 * The NULL-terminated list of paths scanned for homebrew apps.
 * ============================================================ */
const char *DEFAULT_SCAN_PATHS[] = {
    "/data/etaHEN/homebrew",
    "/mnt/ext0/etaHEN/homebrew",
    "/mnt/usb3/etaHEN/games",
    "/user/homebrew/lib",
    NULL
};

/* ============================================================
 * pkg_log  (addr 0x105c00)
 * ============================================================
 * Write to g_pkg_status_log AND append to /user/data/void_install.log.
 */
void pkg_log(const char *fmt, ...)
{
    char    msg[1024];
    char    timebuf[64];
    time_t  now;
    struct tm  tm_buf;
    struct tm *tm_info;
    FILE   *fp;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    strncpy(g_pkg_status_log, msg, sizeof(g_pkg_status_log) - 1);
    g_pkg_status_log[sizeof(g_pkg_status_log) - 1] = '\0';

    fp = fopen("/user/data/void_install.log", "a");
    if (!fp) return;

    now = time(NULL);
    tm_info = localtime_r(&now, &tm_buf);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_info);
    fprintf(fp, "[%s] %s\n", timebuf, msg);
    fclose(fp);
}

/* ============================================================
 * restart_webserver  (addr 0x1012d0)
 * ============================================================ */
void restart_webserver(void)
{
    int rc;

    log_debug("REST: Restarting webserver service...");

    if (g_tid_webserver) {
        pthread_cancel(g_tid_webserver);
        pthread_join(g_tid_webserver, NULL);
        sceKernelUsleep(500000);
    }

    rc = pthread_create(&g_tid_webserver, NULL, thread_web_server, NULL);
    if (rc != 0) {
        log_debug("REST: Webserver restart FAILED");
        return;
    }

    pthread_detach(g_tid_webserver);
    log_debug("REST: Webserver is back online");
    notify_system("Webserver back online");
}

/* ============================================================
 * CORS response helper  (addr 0x106040)
 * ============================================================ */
#define CORS_HEADERS \
    "Access-Control-Allow-Origin: *\r\n" \
    "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n" \
    "Access-Control-Allow-Headers: Content-Type, Content-Length, X-File-Name\r\n"

static const char *HTTP_RESPONSE_TEMPLATE =
    "HTTP/1.1 %s\r\n"
    CORS_HEADERS
    "Content-Type: %s\r\n"
    "Content-Length: %ld\r\n"
    "Connection: close\r\n\r\n";

void send_cors_response(int sock, const char *status, const char *content_type,
                        const char *body)
{
    char   header[1024];
    long   body_len = (long)strlen(body);

    snprintf(header, sizeof(header), HTTP_RESPONSE_TEMPLATE,
             status, content_type, body_len);
    send(sock, header, strlen(header), 0);
    send(sock, body,   body_len,       0);
}

/* ============================================================
 * send_response  (addr 0x107130)
 * ============================================================ */
void send_response(int sock, const char *content_type, const char *body,
                   long body_len, int no_cache)
{
    char header[1024];
    const char *cache_ctrl = no_cache
        ? "no-cache, no-store, must-revalidate"
        : "public, max-age=3600";

    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "Cache-Control: %s\r\n\r\n",
             content_type, body_len, cache_ctrl);

    send(sock, header, strlen(header), 0);
    send(sock, body,   body_len,       0);
}

/* Builds the /api/processes array; names are reduced to printable ASCII with " and \ escaped. */
struct proc_json {
    char *buf;
    int   pos;
    int   cap;
    int   count;
};

static int proc_json_cb(int pid, const char *name, void *ctx)
{
    struct proc_json *pj = ctx;
    char esc[48];
    int  n = 0;

    for (; *name && n < (int)sizeof(esc) - 2; name++) {
        unsigned char c = (unsigned char)*name;
        if (c < 0x20 || c > 0x7e) continue;
        if (c == '"' || c == '\\') esc[n++] = '\\';
        esc[n++] = (char)c;
    }
    esc[n] = '\0';

    if (pj->cap - pj->pos < 128) return 1; /* keep room for the closing brackets */
    pj->pos += snprintf(pj->buf + pj->pos, pj->cap - pj->pos,
                        "%s{\"pid\":%d,\"name\":\"%s\"}", pj->count ? "," : "", pid, esc);
    pj->count++;
    return 0;
}

/*
 * Completes a small POST in `request` (capacity `cap`, `*received` bytes so far): keeps reading until the
 * declared Content-Length (required, at most 64) is present. Returns the body start and sets *body_len,
 * or NULL when the length is missing, too large, or the connection ends first.
 */
#define SMALL_BODY_DEADLINE_MS 5000

/* Milliseconds left until `deadline` (CLOCK_MONOTONIC, in ms); 0 when it has passed. */
static long ms_until(long long deadline)
{
    struct timespec now;
    long long       left;

    clock_gettime(CLOCK_MONOTONIC, &now);
    left = deadline - ((long long)now.tv_sec * 1000 + now.tv_nsec / 1000000);
    return left > 0 ? (long)left : 0;
}

/*
 * Strict Content-Length value: optional blanks, decimal digits only, optional blanks, then the end of the
 * header line. Returns -1 for anything else (so "5abc" or "-1" is not read as 5 or accepted).
 */
static long parse_content_length(const char *value, const char *line_end)
{
    long len = 0;
    int  digits = 0;

    while (value < line_end && (*value == ' ' || *value == '\t')) value++;
    while (value < line_end && *value >= '0' && *value <= '9') {
        len = len * 10 + (*value - '0');
        if (len > 1000000) return -1;
        value++;
        digits++;
    }
    while (value < line_end && (*value == ' ' || *value == '\t')) value++;
    return (digits > 0 && value == line_end) ? len : -1;
}

static const char *read_small_body(int sock, char *request, unsigned int *received, size_t cap, long *body_len)
{
    struct timespec start;
    long long       deadline;

    /* One fixed deadline for the whole request: a client that trickles bytes does not get more time. */
    clock_gettime(CLOCK_MONOTONIC, &start);
    deadline = (long long)start.tv_sec * 1000 + start.tv_nsec / 1000000 + SMALL_BODY_DEADLINE_MS;

    for (;;) {
        char *sep = strstr(request, "\r\n\r\n");
        if (sep) {
            long  len = -1;
            char *p;

            for (p = request; p < sep; p++) {
                if ((p == request || p[-1] == '\n') && strncasecmp(p, "Content-Length:", 15) == 0) {
                    char *eol = strstr(p, "\r\n");
                    len = parse_content_length(p + 15, eol ? eol : sep);
                    break;
                }
            }
            if (len < 0 || len > 64) return NULL;
            if (*received >= (unsigned int)((sep + 4 - request) + len)) {
                *body_len = len;
                return sep + 4;
            }
        }

        if (*received >= cap - 1) return NULL;
        {
            long           left = ms_until(deadline);
            fd_set         rfds;
            struct timeval tv;
            int            n;

            if (left <= 0) return NULL;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            tv.tv_sec  = left / 1000;
            tv.tv_usec = (left % 1000) * 1000;
            if (select(sock + 1, &rfds, NULL, NULL, &tv) <= 0) return NULL;

            n = recv(sock, request + *received, cap - 1 - *received, 0);
            if (n < 1) return NULL;
            *received += (unsigned int)n;
            request[*received] = '\0';
        }
    }
}

/*
 * True when the request headers carry `X-VoidShell-Request: 1`. A browser cannot add that header to a
 * cross-origin request without a CORS preflight, which this server never approves, so a web page
 * cannot trigger state-changing calls that require it.
 */
static int has_request_marker(const char *request)
{
    const char *end = strstr(request, "\r\n\r\n");
    const char *p;

    if (!end) return 0;
    for (p = request; p < end; p++) {
        if (p == request || p[-1] == '\n') {
            if (strncasecmp(p, "X-VoidShell-Request:", 20) == 0) {
                p += 20;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                return p < end && *p == '1' && (p + 1 == end || p[1] == '\r' || p[1] == ' ');
            }
        }
    }
    return 0;
}

/* ============================================================
 * serve_dashboard  (addr 0x1071d0)
 * ============================================================
 * Serve /data/voidshell/www/index.html.
 * If the file is missing, serve a built-in recovery page.
 */
void serve_dashboard(int sock)
{
    char   path[1024];
    char   header[1024];
    FILE  *fp;
    long   sz;
    char  *buf;

    static const char RECOVERY_HTML[] =
        "<html><body style='background:#111;color:#fff;"
        "font-family:sans-serif;text-align:center;padding-top:50px;'>"
        "<h1>VoidShell Recovery</h1>"
        "<p>Dashboard not found at: /data/voidshell/www/index.html</p>"
        "</body></html>";

    snprintf(path, sizeof(path), "%s/index.html", "/data/voidshell/www");
    fp = fopen(path, "rb");

    if (!fp) {
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "Cache-Control: no-cache, no-store, must-revalidate\r\n\r\n",
                 strlen(RECOVERY_HTML));
        send(sock, header,        strlen(header),        0);
        send(sock, RECOVERY_HTML, strlen(RECOVERY_HTML), 0);
        return;
    }

    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    buf = malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "Cache-Control: no-cache, no-store, must-revalidate\r\n\r\n",
             sz);
    send(sock, header, strlen(header), 0);
    send(sock, buf,    sz,             0);
    free(buf);
}

/* ============================================================
 * serve_asset  (addr 0x107370)
 * ============================================================
 * Serve icon0.png or pic0.png from /user/appmeta/<title_id>/.
 * Falls back to /data/voidshell/cache/<title_id>.png for pic0.
 */
void serve_asset(int sock, const char *title_id, const char *asset_type)
{
    char   path[1024];
    char   header[512];
    FILE  *fp;
    long   sz;
    char   buf[8192];
    size_t n;

    /* Determine path */
    if (strcmp(asset_type, "pic") == 0) {
        /* Try cache first */
        snprintf(path, sizeof(path), "/data/voidshell/cache/%s.png", title_id);
        if (access(path, F_OK) != 0)
            snprintf(path, sizeof(path), "/user/appmeta/%s/pic0.png", title_id);
    } else {
        snprintf(path, sizeof(path), "/user/appmeta/%s/icon0.png", title_id);
    }

    fp = fopen(path, "rb");
    if (!fp) {
        send_response(sock, "text/plain", "404", 3, 0);
        return;
    }

    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);

    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: image/png\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "Cache-Control: public, max-age=3600\r\n\r\n",
             sz);
    send(sock, header, strlen(header), 0);

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        send(sock, buf, n, 0);

    fclose(fp);
}

/* ============================================================
 * refresh_library  (addr 0x102480)
 * ============================================================
 * Rebuild g_library[] by scanning /user/app/ (PS5 apps) and
 * /user/appmeta/ (PS4 apps installed via appmeta).
 */
void refresh_library(void)
{
    DIR           *dir;
    struct dirent *entry;
    typedef struct {
        char titleId[0x20];
        char titleName[0x100];
        char contentVersion[0x10];
        char regionCode[5];
    } GI;
    GI   gi;
    long idx = 0;

    pthread_mutex_lock(&g_lib_mutex);

    /* Scan /user/app/ for PS5 (PPSA) and PS4 (CUSA) apps */
    dir = opendir("/user/app");
    if (dir) {
        while ((entry = readdir(dir)) != NULL && idx < LIBRARY_MAX_ENTRIES) {
            char app_path[1024];
            long stride = idx * LIBRARY_ENTRY_STRIDE;

            if (entry->d_name[0] == '.') continue;
            if (strncmp(entry->d_name, "PPSA", 4) != 0 &&
                strncmp(entry->d_name, "CUSA", 4) != 0)
                continue;

            snprintf(app_path, sizeof(app_path), "/user/app/%s", entry->d_name);

            memset(&gi, 0, sizeof(gi));
            if (get_game_info_extended(app_path, &gi) != 0) {
                strncpy(g_library + stride,          entry->d_name, 0x1f);
                strncpy(g_library + stride + 0x2d0,  entry->d_name, 0x7f);
                strncpy(g_library + stride + 0x4a5,  "01.00",       0xf);
                memcpy (g_library + stride + 0x4a0,  "PS5", 4);
            } else {
                strncpy(g_library + stride,          gi.titleId,    0x1f);
                strncpy(g_library + stride + 0x2d0,  gi.titleName,  0x7f);
                strncpy(g_library + stride + 0x4a5,  gi.contentVersion, 0xf);
                strncpy(g_library + stride + 0x4b5,  gi.regionCode, 4);
                memcpy (g_library + stride + 0x4a0,
                        (strncmp(entry->d_name,"PPSA",4)==0) ? "PS5" : "PS4", 4);
            }
            /* Full app path */
            snprintf(g_library + stride + 0x480, 0x400,
                     "/user/app/%s", entry->d_name);
            g_library[stride + 0x4bd] = 1; /* exists flag */

            idx++;
        }
        closedir(dir);
    }

    g_lib_count = g_stat_ps5 = (int)idx;

    /* Scan /user/appmeta/ for additional entries (homebrew installed via appmeta) */
    int ps4_count = 0;
    dir = opendir("/user/appmeta");
    if (dir) {
        while ((entry = readdir(dir)) != NULL && idx < LIBRARY_MAX_ENTRIES) {
            long stride = idx * LIBRARY_ENTRY_STRIDE;

            if (entry->d_name[0] == '.') continue;
            if (strncmp(entry->d_name, "NPXS", 4) != 0 &&
                strncmp(entry->d_name, "PPSB", 4) != 0)
                continue;

            strncpy(g_library + stride,         entry->d_name, 0x1f);
            memcpy (g_library + stride + 0x4a0, "PS4", 4);
            strncpy(g_library + stride + 0x2d0, entry->d_name, 0x7f);
            strncpy(g_library + stride + 0x4a5, "01.00",       0xf);
            strncpy(g_library + stride + 0x4b5, entry->d_name, 4);
            snprintf(g_library + stride + 0x480, 0x400,
                     "/user/appmeta/%s", entry->d_name);
            g_library[stride + 0x4bd] = 1;

            ps4_count++;
            idx++;
        }
        closedir(dir);
    }

    g_lib_count = g_stat_total = (int)idx;
    g_stat_ps4  = ps4_count;

    pthread_mutex_unlock(&g_lib_mutex);
}

/* ============================================================
 * process_path  (addr 0x102030)
 * ============================================================
 * Scan one directory for homebrew apps and call mount_and_install().
 * Maintains a simple path cache to avoid re-processing.
 */
void process_path(const char *dir_path)
{
    DIR           *dir;
    struct dirent *entry;
    char           app_path[1024];
    typedef struct {
        char titleId[0x20];
        char titleName[0x100];
        char contentVersion[0x10];
        char regionCode[5];
    } GI;
    GI gi;

    if (g_security_trip) return;

    dir = opendir(dir_path);
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL) {
        char   hist_path[1024];
        char   user_app_path[1024];
        long   ci;
        int    in_cache = 0;
        struct stat st;

        if (g_paused) break;
        if (entry->d_name[0] == '.') continue;

        snprintf(app_path, sizeof(app_path), "%s/%s", dir_path, entry->d_name);

        memset(&gi, 0, sizeof(gi));
        if (get_game_info_extended(app_path, &gi) != 0)
            continue;

        /* Check path cache */
        for (ci = 0; ci < 512; ci++) {
            if (cache[ci * 0x421 + 0x420] != 0 &&
                strcmp(cache + ci * 0x421, app_path) == 0) {
                in_cache = 1;

                /* Already cached: check if still mounted; if so, do DB patch only */
                snprintf(hist_path, sizeof(hist_path),
                         "/system_ex/app/%s/sce_sys/param.json", gi.titleId);
                if (access(hist_path, F_OK) == 0) {
                    snprintf(hist_path, sizeof(hist_path),
                             "/data/voidshell/history/%s", gi.titleId);
                    if (access(hist_path, F_OK) != 0) {
                        update_app_db(gi.titleId);
                        FILE *fp = fopen(hist_path, "w");
                        if (fp) { fputs(app_path, fp); fclose(fp); }
                    }
                }
                break;
            }
        }

        if (in_cache) continue;

        /* Check if already installed */
        snprintf(user_app_path, sizeof(user_app_path), "/user/app/%s", gi.titleId);
        int already_installed = (stat(user_app_path, &st) == 0);

        mount_and_install(app_path, gi.titleId, gi.titleName, already_installed);

        /* Add to cache */
        for (ci = 0; ci < 512; ci++) {
            if (cache[ci * 0x421 + 0x420] == 0) {
                strncpy(cache + ci * 0x421, app_path, 0x400);
                cache[ci * 0x421 + 0x420] = 1;
                break;
            }
        }
    }

    closedir(dir);
}

/* ============================================================
 * scan_all_paths  (addr 0x1023f0)
 * ============================================================ */
void scan_all_paths(void)
{
    int i;

    /* Default scan paths */
    for (i = 0; DEFAULT_SCAN_PATHS[i] != NULL; i++)
        process_path(DEFAULT_SCAN_PATHS[i]);

    /* User-defined custom paths from config */
    for (i = 0; i < g_custom_paths_count; i++)
        process_path(g_custom_paths[i]);
}

/* ============================================================
 * execute_launch  (addr 0x102830)
 * ============================================================
 * Launch a title ID via sceSystemServiceLaunchApp.
 */
void execute_launch(const char *title_id)
{
    struct {
        unsigned int size;     /* 0x20 */
        unsigned int user_id;
        unsigned char pad[16];
        unsigned long long reserved;
    } params;
    unsigned int uid = 0;

    sceUserServiceGetForegroundUser((int *)&uid);

    memset(&params, 0, sizeof(params));
    params.size    = 0x20;
    params.user_id = uid;

    sceSystemServiceLaunchApp(title_id, NULL, &params);
}

/* ============================================================
 * send_payload  (addr 0x105a00)
 * ============================================================
 * Load a payload file from /data/voidshell/payloads/ and send
 * it to the local kernel exploit listener on port 15651 (0x3D23).
 */
int send_payload(const char *filename)
{
    char path[1024];
    FILE *fp;
    long  sz;
    char *buf;
    int   sock;
    struct sockaddr_in addr;
    int   rc;

    snprintf(path, sizeof(path), "/data/voidshell/payloads/%s", filename);
    fp = fopen(path, "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    buf = malloc(sz);
    if (!buf) { fclose(fp); return false; }
    fread(buf, 1, sz, fp);
    fclose(fp);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { free(buf); return false; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(0x3D23); /* 15651 */
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (rc >= 0)
        send(sock, buf, sz, 0);

    close(sock);
    free(buf);
    return (rc >= 0);
}

/* ============================================================
 * handle_pkg_upload_optimized  (addr 0x1060e0)
 * ============================================================
 * Handle multipart file upload for PKG files.
 * Receives the file incrementally, writes to /user/data/void_tmp/,
 * then spawns thread_installer_worker.
 */
void handle_pkg_upload_optimized(int sock, char *request, int request_len)
{
    char  filename[256] = {0};
    char  tmp_path[512];
    long  content_length = -1;
    char *p;
    char *name_start;
    long  bytes_written;
    char *tmp_buf;
    int   fd;

    /* Handle OPTIONS preflight */
    if (strncmp(request, "OPTIONS", 7) == 0) {
        send_cors_response(sock, "200 OK", "text/plain", "");
        close(sock);
        return;
    }

    /* Extract filename from multipart header "name=" field */
    name_start = strstr(request, "name=");
    if (name_start) {
        char *end = strpbrk(name_start + 5, "&\r\n ");
        if (end) {
            int   len = (int)(end - (name_start + 5));
            if (len > 255) len = 255;
            strncpy(filename, name_start + 5, len);
            filename[len] = '\0';

            /* URL decode + -> space, % -> _ */
            for (int i = 0; i < len; i++) {
                if (filename[i] == '+') filename[i] = ' ';
                else if (filename[i] == '%') filename[i] = '_';
            }
        }
    }

    /* Sanitize filename */
    sanitize_filename(filename);

    /* Extract Content-Length */
    p = request;
    while (*p) {
        if (strncasecmp(p, "Content-Length: ", 16) == 0) {
            content_length = atol(p + 16);
            break;
        }
        p++;
    }

    if (content_length <= 0) {
        pkg_log("Error: Invalid Content-Length");
        send_cors_response(sock, "400 Bad Request", "text/plain", "Missing Size");
        close(sock);
        return;
    }

    /* Build destination path */
    snprintf(tmp_path, sizeof(tmp_path), "%s/%s",
             "/user/data/void_tmp", filename);

    double mb = (double)content_length / (1024.0 * 1024.0);
    pkg_log("Receiving: %s (%.2f MB)...", filename, mb);

    /* Open destination file */
    fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        pkg_log("Error: Cannot write to %s", tmp_path);
        send_cors_response(sock, "500 Error", "text/plain", "Write Error");
        close(sock);
        return;
    }

    /* Find the body separator (\r\n\r\n) */
    char *body_start = strstr(request, "\r\n\r\n");
    if (!body_start) {
        close(fd);
        close(sock);
        return;
    }
    body_start += 4;

    /* Write initial data (already in recv buffer) */
    int initial_len = request_len - (int)(body_start - request);
    if (initial_len > 0)
        write(fd, body_start, initial_len);
    bytes_written = initial_len;

    /* Stream remaining data */
    tmp_buf = malloc(0x2000000); /* 32 MB */
    if (tmp_buf) {
        while (bytes_written < content_length) {
            unsigned int n = recv(sock, tmp_buf, 0x2000000, 0);
            if ((int)n <= 0) break;
            write(fd, tmp_buf, n);
            bytes_written += n;
        }
        free(tmp_buf);
    }
    close(fd);

    if (bytes_written < content_length) {
        pkg_log("Error: Upload truncated");
        send_cors_response(sock, "500 Error", "application/json",
                           "{\"status\":\"fail\"}");
        close(sock);
        return;
    }

    chmod(tmp_path, 0777);
    pkg_log("Upload Complete. Triggering Install...");

    send_cors_response(sock, "200 OK", "application/json", "{\"status\":\"ok\"}");

    /* Spawn installer thread */
    {
        pthread_t  tid;
        char      *path_copy = strdup(tmp_path);
        pthread_create(&tid, NULL, thread_installer_worker, path_copy);
        pthread_detach(tid);
    }

    close(sock);
}

/* ============================================================
 * scan_pkgs_to_json  (addr 0x106760)
 * ============================================================
 * Scan default and custom paths for .pkg files and build
 * g_pkg_list_json as a JSON array of objects.
 */
void scan_pkgs_to_json(void)
{
    static const char *PKG_SCAN_PATHS[] = {
        "/user/data/void_tmp",
        "/data/voidshell/payloads",
        "/mnt/usb3/etaHEN/games",
        "/mnt/ext0/etaHEN/homebrew",
        NULL
    };

    DIR           *dir;
    struct dirent *entry;
    struct stat    st;
    char           path[1024];
    int            first = 1;
    int            pos;
    int            i;

    pos = 0;
    memcpy(g_pkg_list_json, "{\"files\":[", 10);
    pos = 10;

    for (i = 0; PKG_SCAN_PATHS[i] != NULL; i++) {
        dir = opendir(PKG_SCAN_PATHS[i]);
        if (!dir) continue;

        while ((entry = readdir(dir)) != NULL) {
            char *ext;
            int   n;

            if (entry->d_name[0] == '.') continue;
            ext = strrchr(entry->d_name, '.');
            if (!ext || strcasecmp(ext, ".pkg") != 0) continue;

            snprintf(path, sizeof(path), "%s/%s", PKG_SCAN_PATHS[i], entry->d_name);
            if (stat(path, &st) != 0) continue;

            if (!first && pos < (int)sizeof(g_pkg_list_json) - 200)
                pos += snprintf(g_pkg_list_json + pos, sizeof(g_pkg_list_json) - pos, ",");

            if (pos < (int)sizeof(g_pkg_list_json) - 200) {
                double sz_mb = (double)st.st_size / (1024.0 * 1024.0);
                n = snprintf(g_pkg_list_json + pos,
                             sizeof(g_pkg_list_json) - pos,
                             "{\"name\":\"%s\",\"path\":\"%s\",\"size\":\"%.2f MB\"}",
                             entry->d_name, path, sz_mb);
                pos += n;
                first = 0;
            }
        }
        closedir(dir);
    }

    /* Custom paths from config */
    for (i = 0; i < g_custom_paths_count; i++) {
        dir = opendir(g_custom_paths[i]);
        if (!dir) continue;

        while ((entry = readdir(dir)) != NULL) {
            char *ext;
            if (entry->d_name[0] == '.') continue;
            ext = strrchr(entry->d_name, '.');
            if (!ext || strcasecmp(ext, ".pkg") != 0) continue;

            snprintf(path, sizeof(path), "%s/%s", g_custom_paths[i], entry->d_name);
            if (stat(path, &st) != 0) continue;

            if (!first && pos < (int)sizeof(g_pkg_list_json) - 200)
                pos += snprintf(g_pkg_list_json + pos, sizeof(g_pkg_list_json) - pos, ",");

            if (pos < (int)sizeof(g_pkg_list_json) - 200) {
                double sz_mb = (double)st.st_size / (1024.0 * 1024.0);
                pos += snprintf(g_pkg_list_json + pos, sizeof(g_pkg_list_json) - pos,
                                "{\"name\":\"%s\",\"path\":\"%s\",\"size\":\"%.2f MB\"}",
                                entry->d_name, path, sz_mb);
                first = 0;
            }
        }
        closedir(dir);
    }

    if (pos < (int)sizeof(g_pkg_list_json) - 5)
        snprintf(g_pkg_list_json + pos, sizeof(g_pkg_list_json) - pos, "]}");
}

/* ============================================================
 * handle_client  (addr 0x107570)
 * ============================================================
 * Dispatch HTTP requests for the REST API server (port 7007).
 */
void handle_client(int sock)
{
    char  *request;
    unsigned int  bytes_recv;
    char   title_id_buf[32];

    request = malloc(0x40000);
    if (!request) { close(sock); return; }

    bytes_recv = recv(sock, request, 0x3ffff, 0);
    if ((int)bytes_recv < 1) { free(request); close(sock); return; }
    request[bytes_recv] = '\0';

    /* ---- Static assets ---- */
    if (strncmp(request, "GET / ",          6) == 0 ||
        strncmp(request, "GET /index.html", 15) == 0) {
        serve_dashboard(sock);
        goto done;
    }

    if (strncmp(request, "GET /assets/icon", 16) == 0) {
        /* Extract title ID from query param: /assets/icon?id=PPSA01234 */
        char *id_start = strstr(request, "?id=");
        if (!id_start) goto done;
        id_start += 4; /* skip "?id=" */
        memset(title_id_buf, 0, sizeof(title_id_buf));
        for (int i = 0; i < 30 && isalnum((unsigned char)id_start[i]); i++)
            title_id_buf[i] = id_start[i];
        serve_asset(sock, title_id_buf, "icon");
        goto done;
    }

    if (strncmp(request, "GET /assets/pic", 15) == 0) {
        /* Extract title ID from query param: /assets/pic?id=PPSA01234 */
        char *id_start = strstr(request, "?id=");
        if (!id_start) goto done;
        id_start += 4; /* skip "?id=" */
        memset(title_id_buf, 0, sizeof(title_id_buf));
        for (int i = 0; i < 30 && isalnum((unsigned char)id_start[i]); i++)
            title_id_buf[i] = id_start[i];
        serve_asset(sock, title_id_buf, "pic");
        goto done;
    }

    /* ---- API: stats ---- */
    if (strncmp(request, "GET /api/stats", 14) == 0) {
        char  json[1024];
        char  uptime[64];
        char  active_game[0x20];
        char  running_app[0x20];
        char  running_json[0x30];
        int   running_rc;
        char  state_str[8];
        char  firmware[8];
        uint32_t fw;
        int   cpu, soc;
        const char *paused_str, *fan_str;

        pthread_mutex_lock(&g_stats_mutex);
        cpu = g_temp_cpu;
        soc = g_temp_soc;
        strncpy(uptime, g_sys_uptime, sizeof(uptime) - 1);
        pthread_mutex_unlock(&g_stats_mutex);

        pthread_mutex_lock(&g_sentinel_mutex);
        strncpy(active_game, g_s_current_id, sizeof(active_game) - 1);
        memcpy(state_str, &g_s_current_state, 4); state_str[4] = '\0';
        pthread_mutex_unlock(&g_sentinel_mutex);

        /* The running big app, whatever has focus: "" when nothing runs, null when the lookup failed. */
        running_rc = get_running_app_title(running_app, sizeof(running_app));
        if (running_rc < 0)
            snprintf(running_json, sizeof(running_json), "null");
        else
            snprintf(running_json, sizeof(running_json), "\"%s\"", running_rc == 1 ? running_app : "");

        /* System software version, BCD-packed: 0x13400000 -> "13.40". */
        fw = kernel_get_fw_version();
        snprintf(firmware, sizeof(firmware), "%02x.%02x",
                 (unsigned)((fw >> 24) & 0xFF), (unsigned)((fw >> 16) & 0xFF));

        paused_str = g_paused    ? "true" : "false";
        fan_str    = g_fan_enabled ? "true" : "false";

        snprintf(json, sizeof(json),
                 "{\"total\":%d,\"ps5\":%d,\"ps4\":%d,"
                 "\"cpu\":%d,\"soc\":%d,\"sys_uptime\":\"%s\","
                 "\"active_game\":\"%s\",\"running_app\":%s,\"state\":\"%s\","
                 "\"paused\":%s,\"ip\":\"%s\","
                 "\"fan_active\":%s,\"fan_target\":%d,"
                 "\"session_count\":%d,"
                 "\"version\":\"%s\",\"firmware\":\"%s\"}",
                 g_stat_total, g_stat_ps5, g_stat_ps4,
                 cpu, soc, uptime,
                 active_game, running_json, state_str,
                 paused_str, g_ip_address,
                 fan_str, g_fan_target_temp,
                 g_session_count,
                 VOIDSHELL_VERSION, firmware);
        send_response(sock, "application/json", json, strlen(json), 1);
        goto done;
    }

    /* ---- API: library ---- */
    if (strncmp(request, "GET /api/library", 16) == 0) {
        char *json = malloc(0x20000);
        int   pos  = 0;

        if (!json) goto done;

        pthread_mutex_lock(&g_lib_mutex);

        pos += snprintf(json, 0x20000, "{\"games\":[");
        for (int i = 0; i < g_lib_count && pos < 0x1F400; i++) {
            long stride = (long)i * LIBRARY_ENTRY_STRIDE;
            char esc_name[512];
            const char *exists_str;

            json_escape(esc_name, g_library + stride, sizeof(esc_name));
            exists_str = g_library[stride + 0x4bd] ? "true" : "false";

            if (i > 0)
                pos += snprintf(json + pos, 0x20000 - pos, ",");

            pos += snprintf(json + pos, 0x20000 - pos,
                            "{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\","
                            "\"version\":\"%s\",\"region\":\"%s\",\"exists\":%s}",
                            g_library + stride,
                            esc_name,
                            g_library + stride + 0x4a0,
                            g_library + stride + 0x4a5,
                            g_library + stride + 0x4b5,
                            exists_str);
        }
        pos += snprintf(json + pos, 0x20000 - pos, "]}");

        pthread_mutex_unlock(&g_lib_mutex);

        send_response(sock, "application/json", json, pos, 1);
        free(json);
        goto done;
    }

    /* ---- API: payloads ---- */
    if (strncmp(request, "GET /api/payloads", 17) == 0) {
        char *json = malloc(0x8000);
        DIR  *dir;
        struct dirent *entry;
        int   pos = 0, first = 1;

        if (!json) goto done;
        pos = snprintf(json, 0x8000, "{\"files\":[");

        dir = opendir("/data/voidshell/payloads");
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                char *ext;
                bool  enabled = false;
                int   delay   = 0;

                if (entry->d_name[0] == '.') continue;
                ext = strrchr(entry->d_name, '.');
                if (!ext) continue;
                if (strcasecmp(ext, ".elf") != 0 && strcasecmp(ext, ".bin") != 0)
                    continue;

                /* Look up in autorun list */
                for (int j = 0; j < g_autopayload_count; j++) {
                    if (strcmp(g_autopayloads[j].filename, entry->d_name) == 0) {
                        enabled = g_autopayloads[j].enabled;
                        delay   = g_autopayloads[j].delayMs;
                        break;
                    }
                }

                if (!first) pos += snprintf(json+pos, 0x8000-pos, ",");
                pos += snprintf(json+pos, 0x8000-pos,
                                "{\"name\":\"%s\",\"enabled\":%s,\"delay\":%d}",
                                entry->d_name, enabled?"true":"false", delay);
                first = 0;
            }
            closedir(dir);
        }
        pos += snprintf(json+pos, 0x8000-pos, "]}");
        send_response(sock, "application/json", json, pos, 1);
        free(json);
        goto done;
    }

    /* ---- API: send_payload ---- */
    if (strncmp(request, "POST /api/send_payload", 22) == 0) {
        char *body = strstr(request, "\r\n\r\n");
        if (!body) goto done;
        body += 4;
        /* Copy filename and trim trailing whitespace/newlines */
        char payload_name[256] = {0};
        int pn = 0;
        while (pn < 255 && body[pn] && body[pn] != '\r' && body[pn] != '\n' && body[pn] != ' ') {
            payload_name[pn] = body[pn];
            pn++;
        }
        bool ok = send_payload(payload_name);
        const char *resp = ok ? "{\"status\":\"Success\"}" : "{\"status\":\"Failed\"}";
        send_response(sock, "application/json", resp, strlen(resp), 1);
        goto done;
    }

    /* ---- API: repair ---- */
    if (strncmp(request, "POST /api/repair", 16) == 0) {
        trigger_repair();
        send_response(sock, "application/json", "{\"status\":\"ok\"}", 15, 1);
        goto done;
    }

    /* ---- API: launch ---- */
    if (strncmp(request, "POST /api/launch", 16) == 0) {
        char *body = strstr(request, "\r\n\r\n");
        if (!body) goto done;
        strncpy(g_cmd_launch_id, body + 4, sizeof(g_cmd_launch_id) - 1);
        send_response(sock, "application/json", "{\"status\":\"ok\"}", 15, 1);
        goto done;
    }

    /* ---- API: rescan ---- */
    if (strncmp(request, "POST /api/rescan", 16) == 0) {
        g_cmd_rescan = 1;
        send_response(sock, "application/json", "{\"status\":\"ok\"}", 15, 1);
        goto done;
    }

    /* ---- API: pause ---- */
    if (strncmp(request, "POST /api/pause", 15) == 0) {
        const char *msg;
        g_paused ^= 1;
        msg = g_paused ? "Scanner Paused" : "Scanner Resumed";
        notify_system("%s", msg);
        send_response(sock, "application/json", "{\"status\":\"ok\"}", 15, 1);
        goto done;
    }

    /* ---- API: config raw ---- */
    if (strncmp(request, "GET /api/config_raw", 19) == 0) {
        FILE *fp = fopen("/data/voidshell/config.ini", "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *buf = malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, fp);
                buf[sz] = '\0';
                send_response(sock, "text/plain", buf, sz, 1);
                free(buf);
            }
            fclose(fp);
        } else {
            send_response(sock, "text/plain", "", 0, 1);
        }
        goto done;
    }

    /* ---- API: save_ini ---- */
    if (strncmp(request, "POST /api/save_ini", 18) == 0) {
        char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;
            FILE *fp = fopen("/data/voidshell/config.ini", "w");
            if (fp) {
                fwrite(body, 1, strlen(body), fp);
                fclose(fp);
                load_config();
            }
        }
        send_response(sock, "application/json", "{\"status\":\"ok\"}", 15, 1);
        goto done;
    }

    /* ---- API: logs ---- */
    if (strncmp(request, "GET /api/logs/clear",  19) == 0 ||
        strncmp(request, "POST /api/logs/clear", 20) == 0) {
        FILE *fp = fopen("/data/voidshell/logs/debug.log", "w");
        if (fp) fclose(fp);
        send_response(sock, "application/json",
                      "{\"status\":\"cleared\"}", 20, 1);
        goto done;
    }

    if (strncmp(request, "GET /api/logs", 13) == 0) {
        FILE *fp = fopen("/data/voidshell/logs/debug.log", "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            if (sz > 64000) sz = 64000;
            fseek(fp, 0, SEEK_SET);
            char *buf = malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, fp);
                buf[sz] = '\0';
                send_response(sock, "text/plain", buf, sz, 1);
                free(buf);
            }
            fclose(fp);
        } else {
            send_response(sock, "text/plain", "No logs yet.", 12, 1);
        }
        goto done;
    }

    /* ---- API: cache/save ---- */
    if (strncmp(request, "POST /api/cache/save", 20) == 0) {
        char   filename[256] = {0};
        char   cache_path[512];
        char  *body;
        char  *name_p;

        name_p = strstr(request, "name=");
        if (name_p) {
            for (int i = 0; i < 255; i++) {
                char c = name_p[5 + i];
                if (!isalnum((unsigned char)c) && c != '_' && c != '.') break;
                filename[i] = c;
            }
        }

        body = strstr(request, "\r\n\r\n");
        if (body && filename[0]) {
            mkdir("/data/voidshell/cache", 0777);
            snprintf(cache_path, sizeof(cache_path),
                     "/data/voidshell/cache/%s", filename);
            FILE *fp = fopen(cache_path, "wb");
            if (fp) {
                size_t len = bytes_recv - (size_t)(body + 4 - request);
                fwrite(body + 4, 1, len, fp);
                fclose(fp);
            }
        }
        send_response(sock, "application/json", "{\"status\":\"ok\"}", 15, 1);
        goto done;
    }

    /* ---- API: processes ---- */
    if (strncmp(request, "GET /api/processes", 18) == 0) {
        struct proc_json pj = { malloc(0x10000), 0, 0x10000, 0 };

        if (!pj.buf) goto done;
        pj.pos = snprintf(pj.buf, pj.cap, "{\"processes\":[");
        if (list_processes(proc_json_cb, &pj) < 0) {
            const char *err = "{\"status\":\"error\",\"error\":\"process list unavailable\"}";
            send_response(sock, "application/json", err, strlen(err), 1);
            free(pj.buf);
            goto done;
        }
        pj.pos += snprintf(pj.buf + pj.pos, pj.cap - pj.pos, "]}");
        send_response(sock, "application/json", pj.buf, pj.pos, 1);
        free(pj.buf);
        goto done;
    }

    /* ---- API: kill ---- */
    if (strncmp(request, "POST /api/kill", 14) == 0) {
        const char *body;
        long        body_len = 0;
        char        pid_text[16];
        char       *end = NULL;
        long        pid = 0;
        char        json[96];
        int         rc = EINVAL;

        /* Finish reading the (bounded) headers and body first: the marker may arrive in a later segment. */
        body = read_small_body(sock, request, &bytes_recv, 0x40000, &body_len);
        if (!has_request_marker(request)) {
            const char *denied = "{\"status\":\"error\",\"error\":\"forbidden\"}";
            send_response(sock, "application/json", denied, strlen(denied), 1);
            goto done;
        }

        if (body && body_len > 0 && body_len < (long)sizeof(pid_text)) {
            memcpy(pid_text, body, (size_t)body_len);
            pid_text[body_len] = '\0';
            if (strlen(pid_text) != (size_t)body_len) {
                /* An embedded NUL would end the number early while Content-Length says more. */
                body = NULL;
            }
            errno = 0;
            pid = strtol(pid_text, &end, 10);
            while (end && *end && isspace((unsigned char)*end)) end++;
            if (errno != 0 || end == pid_text || *end != '\0' || pid <= 1 || pid > INT_MAX) {
                body = NULL;
            }
        } else {
            body = NULL;
        }

        if (body) rc = kill_process((int)pid);
        if (rc == 0) {
            snprintf(json, sizeof(json), "{\"status\":\"ok\"}");
        } else if (rc == EINVAL) {
            snprintf(json, sizeof(json), "{\"status\":\"error\",\"error\":\"bad request\"}");
        } else {
            snprintf(json, sizeof(json), "{\"status\":\"error\",\"error\":\"%s\"}",
                     rc == ESRCH ? "no such process" : rc == EPERM ? "refused" : "failed");
        }
        send_response(sock, "application/json", json, strlen(json), 1);
        goto done;
    }

    /* ---- API: version ---- */
    if (strncmp(request, "GET /api/version", 16) == 0) {
        char json[64];
        snprintf(json, sizeof(json), "{\"version\":\"%s\"}", VOIDSHELL_VERSION);
        send_response(sock, "application/json", json, strlen(json), 1);
        goto done;
    }

    /* ---- API: session ---- */
    if (strncmp(request, "GET /api/session", 16) == 0) {
        char  *json = malloc(0x4000);
        int    pos  = 0, i;

        if (!json) goto done;
        pos = snprintf(json, 0x4000,
                       "{\"count\":%d,\"installed\":[", g_session_count);
        for (i = 0; i < g_session_count && i < 200; i++) {
            if (i) json[pos++] = ',';
            pos += snprintf(json + pos, 0x4000 - pos,
                            "\"%s\"", g_session_installed[i]);
        }
        pos += snprintf(json + pos, 0x4000 - pos, "]}");
        send_response(sock, "application/json", json, pos, 1);
        free(json);
        goto done;
    }

    /* ---- API: notify ---- */
    if (strncmp(request, "POST /api/notify", 16) == 0) {
        char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;
            char msg[256];
            strncpy(msg, body, sizeof(msg) - 1);
            msg[sizeof(msg) - 1] = '\0';
            notify_system("%s", msg);
        }
        send_response(sock, "application/json", "{\"status\":\"ok\"}", 15, 1);
        goto done;
    }

    /* ---- API: images list ---- */
    if (strncmp(request, "GET /api/images ", 16) == 0) {
        static char img_json[MAX_IMAGE_MOUNTS * 256 + 8];
        int len = image_mount_list_json(img_json, sizeof(img_json));
        send_response(sock, "application/json", img_json, (size_t)len, 1);
        goto done;
    }

    /* ---- API: images unmount ---- */
    if (strncmp(request, "POST /api/images/unmount", 24) == 0) {
        char target[1024] = {0};
        char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;
            int n = 0;
            while (n < 1023 && body[n] && body[n] != '\r' && body[n] != '\n') {
                target[n] = body[n]; n++;
            }
        }
        bool ok = (target[0] != '\0') && image_mount_unmount_by_path(target);
        const char *resp = ok ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}";
        send_response(sock, "application/json", resp, strlen(resp), 1);
        goto done;
    }

    /* ---- Fallback: serve dashboard ---- */
    serve_dashboard(sock);

done:
    free(request);
    close(sock);
}

/* ============================================================
 * thread_web_server  (addr 0x108380)
 * ============================================================
 * Single-threaded HTTP server on port 7007.
 * Uses select() with 2-second timeout so g_running is checked periodically.
 */
void *thread_web_server(void *arg)
{
    int            server_fd;
    int            client_fd;
    int            opt = 1;
    struct sockaddr_in addr;
    fd_set         rfds;
    struct timeval tv;
    int            rc;

    (void)arg;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(WEBSERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY; /* 0.0.0.0 */

    /* From binary: bound to 0.0.0.0:7007 (0x5f1b = port bytes in BE) */
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) goto done;
    if (listen(server_fd, 10) < 0) goto done;

    while (g_running) {
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        tv.tv_sec  = 2;
        tv.tv_usec = 0;

        rc = select(server_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0) continue;

        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd >= 0)
            handle_client(client_fd);
    }

done:
    close(server_fd);
    return NULL;
}

/* ============================================================
 * thread_pkg_server  (addr 0x106ac0)
 * ============================================================
 * PKG install server on port 12576 (0x3120).
 * Handles file uploads, install-from-existing, install-from-URL,
 * status queries, and PKG listing.
 */
void *thread_pkg_server(void *arg)
{
    int   server_fd, client_fd;
    int   opt = 1;
    struct sockaddr_in addr;

    /* Large receive buffer for file uploads */
    int rcvbuf = 0x200000;

    (void)arg;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,   &opt,    sizeof(opt));
    setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF,      &rcvbuf, sizeof(rcvbuf));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PKGSERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd,   (struct sockaddr *)&addr, sizeof(addr)) < 0) goto done;
    if (listen(server_fd, 10) < 0) goto done;

    while (g_running) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        char  *buf = malloc(0x10000);
        if (!buf) { close(client_fd); continue; }
        unsigned int n = recv(client_fd, buf, 0xffff, 0);
        if ((int)n < 1) { free(buf); close(client_fd); continue; }
        buf[n] = '\0';

        /* Dispatch */
        if (strncmp(buf, "POST /upload", 12) == 0) {
            handle_pkg_upload_optimized(client_fd, buf, (int)n);
            free(buf);
            continue; /* handle_pkg_upload_optimized closes the socket */
        }

        if (strncmp(buf, "GET /status", 11) == 0) {
            long slen = strlen(g_pkg_status_log);
            char hdr[512];
            snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\n" CORS_HEADERS
                     "Content-Type: text/plain\r\nContent-Length: %ld\r\n"
                     "Connection: close\r\n\r\n", slen);
            send(client_fd, hdr, strlen(hdr), 0);
            send(client_fd, g_pkg_status_log, slen, 0);

        } else if (strncmp(buf, "GET /list", 9) == 0) {
            scan_pkgs_to_json();
            long jlen = strlen(g_pkg_list_json);
            char hdr[512];
            snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\n" CORS_HEADERS
                     "Content-Type: application/json\r\nContent-Length: %ld\r\n"
                     "Connection: close\r\n\r\n", jlen);
            send(client_fd, hdr,             strlen(hdr), 0);
            send(client_fd, g_pkg_list_json, jlen,        0);

        } else if (strncmp(buf, "POST /install_existing", 22) == 0) {
            char *body = strstr(buf, "\r\n\r\n");
            if (body) {
                char *path = strdup(body + 4);
                /* Strip leading '"' if present */
                char *start = path + (path[0] == '"');
                long  plen  = strlen(start);
                if (start[plen - 1] == '"') start[plen - 1] = '\0';
                char *path2 = strdup(start);
                pthread_t tid;
                pthread_create(&tid, NULL, thread_installer_worker, path2);
                pthread_detach(tid);
                free(path);
            }
            send_cors_response(client_fd, "200 OK", "application/json",
                               "{\"status\":\"ok\"}");

        } else if (strncmp(buf, "POST /api/install_url", 21) == 0) {
            char *body = strstr(buf, "\r\n\r\n");
            if (body) {
                char *url_start = strstr(body + 4, "\r\n\r\n");
                if (!url_start) url_start = body + 4;
                /* Find end of URL */
                char *url_end = strpbrk(url_start, "\"'} \r\n");
                if (url_end) *url_end = '\0';
                char *url = strdup(url_start);
                pthread_t tid;
                pthread_create(&tid, NULL, thread_installer_worker, url);
                pthread_detach(tid);
            }
            send_cors_response(client_fd, "200 OK", "application/json",
                               "{\"status\":\"ok\"}");

        } else if (strncmp(buf, "POST /delete", 12) == 0) {
            char *body = strstr(buf, "\r\n\r\n");
            if (body) {
                char *p = body + 4;
                /* Reject path traversal attempts */
                if ((strstr(p, ".pkg") || strstr(p, ".PKG")) && !strstr(p, ".."))
                    unlink(p);
            }
            send_cors_response(client_fd, "200 OK", "application/json",
                               "{\"status\":\"ok\"}");

        } else if (strncmp(buf, "OPTIONS", 7) == 0) {
            send_cors_response(client_fd, "200 OK", "text/plain", "");
        } else {
            send_cors_response(client_fd, "404 Not Found", "text/plain", "");
        }

        free(buf);
        close(client_fd);
    }

done:
    close(server_fd);
    return NULL;
}

/* webserver.h -- VoidShell HTTP REST server and PKG server */
#ifndef WEBSERVER_H
#define WEBSERVER_H

/* Thread entry points */
void *thread_web_server(void *arg);
void *thread_pkg_server(void *arg);

/* Called by sentinel / stats monitor to restart the web server */
void restart_webserver(void);

/* HTTP helpers */
void send_response(int sock, const char *content_type, const char *body,
                   long body_len, int no_cache);
void send_cors_response(int sock, const char *status, const char *content_type,
                        const char *body);

/* Individual request handlers */
void serve_dashboard(int sock);
void serve_asset(int sock, const char *title_id, const char *asset_type);
void handle_client(int sock);
void handle_pkg_upload_optimized(int sock, char *request, int request_len);

/* PKG server helpers */
void scan_pkgs_to_json(void);
void pkg_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Payload injection (send ELF to localhost:15651) */
int send_payload(const char *filename);

/* Library scan */
void scan_all_paths(void);
void process_path(const char *dir_path);
void refresh_library(void);
void execute_launch(const char *title_id);

/* PKG server globals */
extern char  g_pkg_status_log[0x400];
extern char  g_pkg_list_json[0x3e800];

/* Web server port */
#define WEBSERVER_PORT  7007
#define PKGSERVER_PORT  12576  /* 0x3120 */

/* Default scan paths (NULL-terminated, from binary data) */
extern const char *DEFAULT_SCAN_PATHS[];

#endif /* WEBSERVER_H */

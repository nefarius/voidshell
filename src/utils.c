/*
 * utils.c — VoidShell string and notification utilities
 */

#include "utils.h"
#include "ps5_sdk.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
 * trim_space
 * ============================================================
 * Trim leading whitespace by advancing the pointer, then trim
 * trailing whitespace by overwriting with NUL.
 * Returns pointer into the original buffer — caller must not free
 * the returned pointer separately.
 */
char *trim_space(char *s)
{
    char *end;

    /* Skip leading whitespace */
    while (isspace((unsigned char)*s))
        s++;

    if (*s == '\0')
        return s;

    /* Trim trailing whitespace */
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';

    return s;
}

/* ============================================================
 * json_escape
 * ============================================================
 * Copy src into dst, inserting backslash escapes for '"' and '\'.
 * Control characters (< 0x20) are silently dropped.
 * Output is always NUL-terminated.
 */
void json_escape(char *dst, const char *src, size_t dst_size)
{
    size_t out = 0;
    unsigned char c;

    if (!dst_size)
        return;

    while ((c = (unsigned char)*src++) != '\0' && out < dst_size - 1) {
        if (c == '\\' || c == '"') {
            if (out + 2 > dst_size - 1)
                break;
            dst[out++] = '\\';
            dst[out++] = (char)c;
        } else if (c >= 0x20) {
            /* isprint equivalent — drop control chars */
            dst[out++] = (char)c;
        }
    }
    dst[out] = '\0';
}

/* ============================================================
 * extract_json_string
 * ============================================================
 * Search json_buf for `"<key>"`, then parse the colon-separated
 * quoted string value.
 *
 * Returns 0 on success, negative on error (see header).
 */
int extract_json_string(const char *json_buf, const char *key,
                        char *out, size_t out_size)
{
    char search[64];
    const char *p;
    size_t key_len, i;

    /* Build search token: `"key"` */
    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(json_buf, search);
    if (!p)
        return -1;

    key_len = strlen(search);
    p = strchr(p + key_len, ':');
    if (!p)
        return -2;

    p++;
    /* Skip whitespace after colon */
    while (*p != '\0' && isspace((unsigned char)*p))
        p++;

    if (*p == '\0')
        return -3;

    if (*p != '"') {
        /* Not a quoted string — caller gets the raw token up to out_size-1 */
        out[0] = '\0';
        return -3;
    }

    /* Copy quoted string content */
    p++; /* skip opening '"' */
    i = 0;
    while (i < out_size - 1 && *p != '\0' && *p != '"') {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

/* ============================================================
 * strcasestr_custom  (addr 0x105d40)
 * strcasestr_local   (addr 0x107070)
 * ============================================================
 * Both are identical case-insensitive substring search implementations.
 * Duplicated in the binary; we provide both entry points.
 */
static char *_strcasestr_impl(const char *haystack, const char *needle)
{
    int first;
    const char *h, *n;
    int hc, nc;

    if (*needle == '\0')
        return (char *)haystack;

    first = toupper((unsigned char)*needle);

    for (; *haystack != '\0'; haystack++) {
        if (toupper((unsigned char)*haystack) != first)
            continue;
        /* Potential match — compare rest */
        h = haystack;
        n = needle;
        for (;;) {
            if (*n == '\0')
                return (char *)haystack;
            hc = toupper((unsigned char)*h);
            nc = toupper((unsigned char)*n);
            if (*h == '\0' || hc != nc)
                break;
            h++;
            n++;
        }
        if (*n == '\0')
            return (char *)haystack;
    }
    return NULL;
}

char *strcasestr_custom(const char *haystack, const char *needle)
{
    return _strcasestr_impl(haystack, needle);
}

char *strcasestr_local(const char *haystack, const char *needle)
{
    return _strcasestr_impl(haystack, needle);
}

/* ============================================================
 * sanitize_filename  (addr 0x105e00)
 * ============================================================
 * Replaces any char that is not alphanumeric or in {'-', '.', '_', '/'}
 * with '_'.  Allowed set matches the bitmask: 0x4000000000003 at
 * offset from '-' (0x2d).
 */
void sanitize_filename(char *filename)
{
    unsigned char c;

    while ((c = (unsigned char)*filename) != '\0') {
        if (!isalnum((int)(char)c)) {
            /* Allow: '-' (0x2d), '.' (0x2e), '/' (0x2f), '_' (0x5f) */
            if (c != '-' && c != '.' && c != '/' && c != '_') {
                *filename = '_';
            }
        }
        filename++;
    }
}

/* ============================================================
 * notify_system  (addr 0x100410)
 * ============================================================
 * Sends a PS5 system notification popup.
 * The notification buffer is 0xC30 bytes; the message text starts at
 * offset 0x2D inside that buffer (determined by the struct layout used
 * by sceKernelSendNotificationRequest).
 */
void notify_system(const char *fmt, ...)
{
    /*
     * The notification buffer structure (0xC30 bytes total, zeroed):
     *   +0x00  type/flags (set to 0)
     *   +0x2D  message text (up to 0xC03 bytes, NUL-terminated)
     *
     * The buffer layout observed in the binary is:
     *   local_d18 [45 bytes]  -- header / flags
     *   local_ceb [3075 bytes] -- message text
     * Total = 45 + 3075 = 3120 = 0xC30
     */
    char buf[0xC30];
    char msg[0xC03];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    memset(buf, 0, sizeof(buf));
    /* Message text lives at buf+0x2D (= 45 bytes in) */
    strncpy(buf + 0x2D, msg, sizeof(msg) - 1);

    sceKernelSendNotificationRequest(0, buf, sizeof(buf), 0);
}

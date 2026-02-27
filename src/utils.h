#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

/*
 * utils.h — VoidShell string / JSON utility functions
 */

/**
 * Trim leading and trailing whitespace from a string IN PLACE.
 * Returns a pointer to the first non-whitespace character.
 * The trailing whitespace is overwritten with '\0'.
 * The original buffer is modified; returned pointer is into that buffer.
 */
char *trim_space(char *s);

/**
 * JSON-escape the string `src` into the buffer `dst` (capacity `dst_size`).
 *
 * Rules applied:
 *   - Backslash and double-quote are escaped with a leading backslash.
 *   - Control characters (codepoints < 0x20) are dropped.
 *   - All other bytes are copied verbatim.
 *
 * The output is always NUL-terminated and will not exceed dst_size bytes.
 */
void json_escape(char *dst, const char *src, size_t dst_size);

/**
 * Extract a string value from a JSON object by key.
 *
 * Searches `json_buf` for the pattern `"<key>"` then scans past the `:`
 * separator to find the quoted string value, copying at most `out_size-1`
 * characters into `out`.
 *
 * Return values:
 *   0              -- success
 *   -1 (0xffffffff)-- key not found in json_buf
 *   -2 (0xfffffffe)-- key found but no ':' after it
 *   -3 (0xfffffffd)-- key found, ':' found, but no opening '"'
 */
int extract_json_string(const char *json_buf, const char *key,
                        char *out, size_t out_size);

/**
 * Case-insensitive substring search (custom implementation).
 * Equivalent to the POSIX strcasestr().
 * Returns a pointer to the first occurrence of needle in haystack,
 * or NULL if not found.
 */
char *strcasestr_custom(const char *haystack, const char *needle);

/**
 * Second case-insensitive substring search implementation (identical
 * algorithm, separate copy used by the web server module).
 */
char *strcasestr_local(const char *haystack, const char *needle);

/**
 * Sanitize a filename in-place.
 * Replaces any character that is not alphanumeric or one of: - . _ /
 * with an underscore '_'.
 */
void sanitize_filename(char *filename);

/**
 * Send a PS5 system notification popup via sceKernelSendNotificationRequest.
 * Accepts printf-style format string.
 */
void notify_system(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* UTILS_H */

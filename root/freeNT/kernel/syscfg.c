/* =============================================================================
 * SYSCFG.C — generic key=value store backed by /toriginal_os/config.ini
 * See syscfg.h for the format and rationale.
 * ============================================================================= */

#include "syscfg.h"
#include "fs.h"
#include "string.h"

#define SYSCFG_PATH      "/toriginal_os/config.ini"
/* config.ini today holds ~7 short keys (username/timezone/resolution/
 * storage/password/oobe_pending/text_color/bg_color/statusbar_color).
 * 4096 bytes is a full TRPFS block and comfortably fits that many times
 * over - if this ever needs to grow past one block, the read/rewrite
 * loop below needs a real streaming rewrite instead of a flat buffer. */
#define SYSCFG_BUF_MAX   4096

/* Reads the whole file into buf (NUL-terminated). Returns byte count
 * read (0 if the file doesn't exist / is empty), never negative. */
static size_t read_whole_file(char *buf, size_t buf_len) {
    fd_t fd = fs_open(SYSCFG_PATH, O_RDONLY, 0);
    if (fd < 0) return 0;
    ssize_t n = fs_read(fd, buf, buf_len - 1);
    fs_close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    return (size_t)n;
}

/* Finds "key=" at the start of a line within buf. Returns a pointer to
 * the start of that line, or NULL. */
static char *find_key_line(char *buf, const char *key) {
    size_t klen = strlen(key);
    char *p = buf;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            /* Confirm this is really the start of a line, not the tail
             * end of a longer key that happens to share this suffix -
             * find_key_line is only ever called at p==buf or right after
             * a '\n', so this check is naturally satisfied by the loop
             * below; kept explicit here for clarity. */
            return p;
        }
        char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return NULL;
}

int syscfg_get(const char *key, char *out, size_t out_len) {
    if (!key || !out || out_len == 0) return 0;
    out[0] = '\0';

    static char buf[SYSCFG_BUF_MAX];
    if (read_whole_file(buf, sizeof(buf)) == 0) return 0;

    char *line = find_key_line(buf, key);
    if (!line) return 0;

    char *val = line + strlen(key) + 1; /* skip "key=" */
    size_t i = 0;
    while (val[i] && val[i] != '\n' && val[i] != '\r' && i < out_len - 1) {
        out[i] = val[i];
        i++;
    }
    out[i] = '\0';
    return 1;
}

void syscfg_get_or(const char *key, char *out, size_t out_len, const char *dflt) {
    if (!syscfg_get(key, out, out_len)) {
        strncpy(out, dflt, out_len - 1);
        out[out_len - 1] = '\0';
    }
}

int syscfg_set(const char *key, const char *value) {
    if (!key || !value) return -1;

    static char buf[SYSCFG_BUF_MAX];
    size_t len = read_whole_file(buf, sizeof(buf));

    static char rebuilt[SYSCFG_BUF_MAX];
    size_t out_i = 0;
    int replaced = 0;
    size_t klen = strlen(key);
    size_t vlen = strlen(value);

    char *p = buf;
    char *end = buf + len;
    while (p < end) {
        char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);

        int is_match = (line_len > klen && strncmp(p, key, klen) == 0 && p[klen] == '=');

        if (is_match) {
            /* Replace this line's value with the new one. */
            if (out_i + klen + 1 + vlen + 1 < SYSCFG_BUF_MAX) {
                memcpy(rebuilt + out_i, key, klen); out_i += klen;
                rebuilt[out_i++] = '=';
                memcpy(rebuilt + out_i, value, vlen); out_i += vlen;
                rebuilt[out_i++] = '\n';
            }
            replaced = 1;
        } else if (line_len > 0) {
            /* Copy this line through unchanged (plus its newline, if any). */
            if (out_i + line_len + 1 < SYSCFG_BUF_MAX) {
                memcpy(rebuilt + out_i, p, line_len); out_i += line_len;
                rebuilt[out_i++] = '\n';
            }
        }

        p = nl ? nl + 1 : end;
    }

    if (!replaced) {
        if (out_i + klen + 1 + vlen + 1 < SYSCFG_BUF_MAX) {
            memcpy(rebuilt + out_i, key, klen); out_i += klen;
            rebuilt[out_i++] = '=';
            memcpy(rebuilt + out_i, value, vlen); out_i += vlen;
            rebuilt[out_i++] = '\n';
        }
    }
    rebuilt[out_i] = '\0';

    fd_t fd = fs_open(SYSCFG_PATH, O_WRONLY | O_CREAT | O_TRUNC,
                       FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd < 0) return -1;
    ssize_t written = fs_write(fd, rebuilt, out_i);
    fs_close(fd);
    return (written == (ssize_t)out_i) ? 0 : -1;
}

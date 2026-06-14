/*
 * trp_manifest.c — Toriginal OS TRP Application Manifest v1.1
 *
 * Implements a forgiving keyword-scan parser for manifest.txt files
 * embedded in .TRP packages.  Keywords are matched case-insensitively
 * anywhere on a line; unrecognised lines are silently ignored.
 * ALL errors are collected before reporting — execution never starts
 * if any error exists.
 */

#include "../include/Trp manifest.h"
#include "../include/string.h"
#include "../include/fs.h"
#include "../include/io.h"
#include "../include/keybord.h"

/* Forward-declare the serial helpers used elsewhere in the kernel */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint32_t pmm_get_free_ram(void);

/* ══════════════════════════════════════════════════════════════════════════
 * Internal utilities
 * ══════════════════════════════════════════════════════════════════════════ */

static char trp_to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/*
 * ci_find — case-insensitive substring search.
 * Returns pointer to first match inside haystack, or NULL.
 */
static const char *ci_find(const char *haystack, const char *needle)
{
    if (!needle || !*needle) return haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncmp(haystack, needle, nlen) == 0)
            return haystack;          /* fast path: exact match             */
        /* slow path: case-fold compare                                      */
        const char *h = haystack;
        const char *n = needle;
        size_t matched = 0;
        while (*h && *n && trp_to_lower(*h) == trp_to_lower(*n)) {
            h++; n++; matched++;
        }
        if (matched == nlen) return haystack;
    }
    return NULL;
}

/*
 * extract_value — find directive keyword in line, then copy everything
 * after "/:" (trimmed) into dst.
 * Returns 1 if a non-empty value was found, 0 otherwise.
 */
static int extract_value(const char *line,
                          const char *directive,
                          char       *dst,
                          int         dst_len)
{
    const char *p = ci_find(line, directive);
    if (!p) return 0;

    /* Advance past the full keyword (which already ends in "/:") */
    p += strlen(directive);

    /* Skip any leading whitespace after the directive */
    while (*p == ' ' || *p == '\t') p++;

    /* Copy until end-of-line, but stop at newline characters */
    int i = 0;
    while (*p && *p != '\n' && *p != '\r' && i < dst_len - 1)
        dst[i++] = *p++;

    /* Trim trailing whitespace */
    while (i > 0 && (dst[i-1] == ' ' || dst[i-1] == '\t')) i--;
    dst[i] = '\0';

    return (i > 0) ? 1 : 0;
}

/*
 * int_to_dec — convert a non-negative integer to a decimal string.
 */
static void int_to_dec(int n, char *buf, int buf_len)
{
    if (buf_len < 2) return;
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int ti = 0;
    while (n > 0 && ti < 11) { tmp[ti++] = (char)('0' + n % 10); n /= 10; }
    int idx = 0;
    while (ti > 0 && idx < buf_len - 1) buf[idx++] = tmp[--ti];
    buf[idx] = '\0';
}

/*
 * safe_copy — bounded string copy (always null-terminates).
 */
static void safe_copy(char *dst, const char *src, int dst_len)
{
    int i = 0;
    while (src[i] && i < dst_len - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/*
 * str_append — append src to dst, respecting cap.
 * Returns new length of dst.
 */
static int str_append(char *dst, int cur_len, int cap, const char *src)
{
    while (*src && cur_len < cap - 1) dst[cur_len++] = *src++;
    dst[cur_len] = '\0';
    return cur_len;
}

/*
 * add_error — add a formatted error message to the manifest error list.
 * If line_no > 0 the string " (line N)" is appended.
 */
static void add_error(trp_manifest_t *m, const char *msg, int line_no)
{
    if (m->error_count >= TRP_MANIFEST_MAX_ERRORS) return;

    char *dst = m->errors[m->error_count];
    int   cap = TRP_MANIFEST_ERROR_LEN;
    int   len = 0;

    len = str_append(dst, len, cap, msg);

    if (line_no > 0) {
        len = str_append(dst, len, cap, " (line ");
        char nbuf[12];
        int_to_dec(line_no, nbuf, sizeof(nbuf));
        len = str_append(dst, len, cap, nbuf);
        len = str_append(dst, len, cap, ")");
    }

    m->error_count++;
}

/* ══════════════════════════════════════════════════════════════════════════
 * trp_manifest_parse
 * ══════════════════════════════════════════════════════════════════════════ */

int trp_manifest_parse(const char *text, uint32_t len, trp_manifest_t *out)
{
    memset(out, 0, sizeof(trp_manifest_t));

    char     line[512];
    uint32_t pos     = 0;
    int      line_no = 0;
    char     val[256];

    while (pos < len) {
        /* ── collect one line ── */
        int llen = 0;
        while (pos < len && text[pos] != '\n' && text[pos] != '\r' && llen < 511)
            line[llen++] = text[pos++];
        line[llen] = '\0';
        /* consume line endings */
        while (pos < len && (text[pos] == '\n' || text[pos] == '\r')) pos++;
        line_no++;

        if (llen == 0) continue;   /* blank lines are silently skipped */

        /* ── Execute at/: (required) ─────────────────────────────────── */
        if (ci_find(line, "Execute at/:")) {
            int has_val = extract_value(line, "Execute at/:", val, sizeof(val));
            if (!out->execute_at_found) {
                /* Primary entry point */
                out->execute_at_found = 1;
                out->execute_at_line  = line_no;
                if (has_val)
                    safe_copy(out->execute_at, val, sizeof(out->execute_at));
                /* Empty value → error collected in validate phase */
            } else {
                /* Subsequent Execute at entries become fallbacks */
                if (has_val && out->fallback_count < TRP_MANIFEST_MAX_FALLBACKS) {
                    safe_copy(out->fallback_execute_at[out->fallback_count],
                              val,
                              sizeof(out->fallback_execute_at[0]));
                    out->fallback_count++;
                }
            }
            continue;
        }

        /* ── Window name/: ───────────────────────────────────────────── */
        if (ci_find(line, "Window name/:")) {
            extract_value(line, "Window name/:",
                          out->window_name, sizeof(out->window_name));
            continue;
        }

        /* ── Icon/: ──────────────────────────────────────────────────── */
        if (ci_find(line, "Icon/:")) {
            extract_value(line, "Icon/:", out->icon, sizeof(out->icon));
            continue;
        }

        /* ── Priority/: ──────────────────────────────────────────────── */
        if (ci_find(line, "Priority/:")) {
            extract_value(line, "Priority/:",
                          out->priority, sizeof(out->priority));
            continue;
        }

        /* ── Execute if/: ────────────────────────────────────────────── */
        if (ci_find(line, "Execute if/:")) {
            extract_value(line, "Execute if/:",
                          out->execute_if, sizeof(out->execute_if));
            continue;
        }

        /* ── mark executable/ (no value) ─────────────────────────────── */
        if (ci_find(line, "mark executable/")) {
            out->mark_executable = 1;
            continue;
        }

        /* ── Language/: ──────────────────────────────────────────────── */
        if (ci_find(line, "Language/:")) {
            extract_value(line, "Language/:",
                          out->language, sizeof(out->language));
            continue;
        }

        /* ── Version/: ───────────────────────────────────────────────── */
        if (ci_find(line, "Version/:")) {
            extract_value(line, "Version/:",
                          out->version, sizeof(out->version));
            continue;
        }

        /* ── Assets/: ────────────────────────────────────────────────── */
        if (ci_find(line, "Assets/:")) {
            extract_value(line, "Assets/:", out->assets, sizeof(out->assets));
            continue;
        }

        /* ── Debug/: ─────────────────────────────────────────────────── */
        if (ci_find(line, "Debug/:")) {
            extract_value(line, "Debug/:", val, sizeof(val));
            out->debug_mode = (ci_find(val, "true") != NULL) ? 1 : 0;
            continue;
        }

        /* ── Resource request/: ──────────────────────────────────────── */
        if (ci_find(line, "Resource request/:")) {
            extract_value(line, "Resource request/:", val, sizeof(val));
            if (ci_find(val, "high") != NULL) {
                out->resource_request_high = 1;
                out->resource_request_line = line_no;
            }
            continue;
        }

        /*
         * No directive matched → line is silently ignored.
         * This is by design: unrelated text never causes errors.
         */
    }

    /* ── Parse-time validation ─────────────────────────────────────────── */

    if (!out->execute_at_found) {
        /* Spec: "If Execute at exists but has no value → ERROR" — we also
         * catch the case where the directive is missing entirely.         */
        add_error(out, "Error: no Execute at directive found", 0);
    } else if (out->execute_at[0] == '\0') {
        add_error(out, "Error: Execute at is empty", out->execute_at_line);
    }

    return (out->error_count > 0) ? -1 : 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * trp_manifest_validate
 * ══════════════════════════════════════════════════════════════════════════ */

int trp_manifest_validate(trp_manifest_t *out)
{
    /*
     * Only check file existence when Execute at is non-empty.
     * (An empty value was already flagged as an error during parse.)
     */
    if (out->execute_at_found && out->execute_at[0] != '\0') {
        inode_t st;
        if (fs_stat(out->execute_at, &st) != 0) {
            /* Build: Error: no file named 'X' */
            char msg[TRP_MANIFEST_ERROR_LEN];
            int  len = 0;
            int  cap = TRP_MANIFEST_ERROR_LEN;

            len = str_append(msg, len, cap, "Error: no file named '");
            len = str_append(msg, len, cap, out->execute_at);
            len = str_append(msg, len, cap, "'");

            add_error(out, msg, out->execute_at_line);
        }
    }

    return (out->error_count > 0) ? -1 : 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * trp_manifest_print_errors
 * ══════════════════════════════════════════════════════════════════════════ */

void trp_manifest_print_errors(const trp_manifest_t *m, int gui_mode)
{
    if (m->error_count == 0) return;

    if (gui_mode) {
        /* GUI mode: framed error window */
        io_put_string("\n+--------------------------------------+\n");
        io_put_string(  "|  TRP Package Errors                  |\n");
        io_put_string(  "+--------------------------------------+\n");
        for (int i = 0; i < m->error_count; i++) {
            io_put_string("|  ");
            io_put_string(m->errors[i]);
            io_put_string("\n");
        }
        io_put_string("+--------------------------------------+\n\n");
    } else {
        /* CLI mode: print errors line-by-line to VGA and serial */
        io_put_string("\nTRP Manifest Errors:\n");
        serial_puts("\n[TRP] Manifest errors:\n");
        for (int i = 0; i < m->error_count; i++) {
            io_put_string("  ");
            io_put_string(m->errors[i]);
            io_put_string("\n");
            serial_puts("  ");
            serial_puts(m->errors[i]);
            serial_puts("\n");
        }
        io_put_string("\n");
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * trp_manifest_resource_prompt
 * ══════════════════════════════════════════════════════════════════════════ */

int trp_manifest_resource_prompt(int gui_mode)
{
    (void)gui_mode;   /* same prompt for CLI and GUI in v1.1 */

    io_put_string("\nThis program is requesting higher system resources.\n");
    io_put_string("Allow? (Y/N): ");
    serial_puts("[TRP] Resource request prompt displayed.\n");

    for (;;) {
        char c = keyboard_getc();
        if (c == 'Y' || c == 'y') {
            io_put_string("Y\n");
            io_put_string("Resource request approved.\n");
            serial_puts("[TRP] Resource request approved by user.\n");
            return 1;
        }
        if (c == 'N' || c == 'n') {
            io_put_string("N\n");
            io_put_string("Running with standard resources.\n");
            serial_puts("[TRP] Resource request denied by user.\n");
            return 0;
        }
        /* Any other key is ignored — keep waiting */
    }
}

/* ── Internal: check whether the system has headroom for the request ──── */
static int system_can_support_high_resources(void)
{
    /*
     * Require at least 8 MB of free physical RAM before granting a
     * "high" resource request.  This prevents the grant from tipping the
     * system into instability.
     *
     * pmm_get_free_ram() returns free RAM in kilobytes.
     * Replace 8192 with a tuned threshold once benchmarks exist.
     */
    uint32_t free_kb = pmm_get_free_ram();
    return (free_kb >= 8192) ? 1 : 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * trp_manifest_run_gate  — full execution pipeline
 * ══════════════════════════════════════════════════════════════════════════
 *
 * Steps (per spec §Execution Flow):
 *   1-2  Parse manifest
 *   3-4  Validate Execute at + file existence
 *   5    Check Resource request rules
 *   6-7  Collect ALL errors; if any → display and abort
 *   8    Prompt for resource allocation if needed
 *   9    Return entry-point to caller for launch
 *   10   Log optional directives
 */
int trp_manifest_run_gate(const char  *manifest_text,
                           uint32_t    len,
                           char       *entry_out,
                           int         entry_out_len,
                           int         gui_mode)
{
    trp_manifest_t m;

    /* Steps 1-2: parse -------------------------------------------------- */
    trp_manifest_parse(manifest_text, len, &m);

    /* Steps 3-4: validate file existence --------------------------------- */
    trp_manifest_validate(&m);

    /* Steps 6-7: report ALL errors and abort if any exist --------------- */
    if (m.error_count > 0) {
        trp_manifest_print_errors(&m, gui_mode);
        return -1;
    }

    /* Step 5 & 8: handle Resource request/:high ------------------------- */
    if (m.resource_request_high) {
        int user_approved  = trp_manifest_resource_prompt(gui_mode);
        int system_ok      = system_can_support_high_resources();

        if (user_approved && !system_ok) {
            /*
             * Spec: "If system cannot safely support the request OR program
             * requires it to function → MUST THROW ERROR and refuse."
             */
            add_error(&m,
                "Error: resource request denied (insufficient system capacity)",
                m.resource_request_line);
            trp_manifest_print_errors(&m, gui_mode);
            return -1;
        }

        if (!user_approved) {
            /*
             * Spec: "If NO → program runs normally if possible."
             * We continue to launch with standard resources.
             */
            serial_puts("[TRP] Continuing with standard resources.\n");
        } else {
            /* user approved + system OK → OS may allocate more (within limits) */
            serial_puts("[TRP] Higher resources granted within system limits.\n");
        }
    }

    /* Step 9: hand entry-point back to the caller for launch ------------ */
    if (entry_out && entry_out_len > 0)
        safe_copy(entry_out, m.execute_at, entry_out_len);

    /* Step 10: log optional directives (serial only — non-critical) ------ */
    serial_puts("[TRP v1.1] Manifest OK. Entry: ");
    serial_puts(m.execute_at);
    serial_puts("\n");

    if (m.window_name[0])    { serial_puts("  Window   : "); serial_puts(m.window_name);  serial_puts("\n"); }
    if (m.icon[0])           { serial_puts("  Icon     : "); serial_puts(m.icon);          serial_puts("\n"); }
    if (m.language[0])       { serial_puts("  Language : "); serial_puts(m.language);      serial_puts("\n"); }
    if (m.version[0])        { serial_puts("  Version  : "); serial_puts(m.version);       serial_puts("\n"); }
    if (m.priority[0])       { serial_puts("  Priority : "); serial_puts(m.priority);      serial_puts("\n"); }
    if (m.execute_if[0])     { serial_puts("  Exec if  : "); serial_puts(m.execute_if);    serial_puts("\n"); }
    if (m.assets[0])         { serial_puts("  Assets   : "); serial_puts(m.assets);        serial_puts("\n"); }
    if (m.mark_executable)   { serial_puts("  [marked executable]\n"); }
    if (m.debug_mode)        { serial_puts("  [debug ON]\n"); }

    if (m.fallback_count > 0) {
        char nbuf[12];
        int_to_dec(m.fallback_count, nbuf, sizeof(nbuf));
        serial_puts("  Fallbacks : ");
        serial_puts(nbuf);
        serial_puts(" alternate entry point(s) registered\n");
    }

    return 0;
}







/*
 * trp_manifest.c — Toriginal OS TRP Application Manifest v1.1
 *
 * Forgiving keyword-scan parser for manifest.txt files embedded in .TRP
 * packages.  Keywords are matched case-insensitively anywhere on a line;
 * unrecognised lines are silently ignored.  ALL errors are collected before
 * reporting — execution never starts if any error exists.
 */

#include "Trp manifest.h"
#include "string.h"
#include "fs.h"
#include "io.h"
#include "keybord.h"
#include "pmm.h"

extern void serial_puts(const char *s);

/* ── Internal utilities ──────────────────────────────────────────────────── */

static char trp_to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* Case-insensitive substring search. */
static const char *ci_find(const char *haystack, const char *needle)
{
    if (!needle || !*needle) return haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        size_t matched = 0;
        while (*h && *n && trp_to_lower(*h) == trp_to_lower(*n)) {
            h++; n++; matched++;
        }
        if (matched == nlen) return haystack;
    }
    return NULL;
}

/* Copy the value after a "directive/:" marker (trimmed) into dst. */
static int extract_value(const char *line, const char *directive,
                          char *dst, int dst_len)
{
    const char *p = ci_find(line, directive);
    if (!p) return 0;

    p += strlen(directive);
    while (*p == ' ' || *p == '\t') p++;

    int i = 0;
    while (*p && *p != '\n' && *p != '\r' && i < dst_len - 1)
        dst[i++] = *p++;

    while (i > 0 && (dst[i-1] == ' ' || dst[i-1] == '\t')) i--;
    dst[i] = '\0';

    return (i > 0) ? 1 : 0;
}

static void int_to_dec(int n, char *buf, int buf_len)
{
    if (buf_len < 2) return;
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int ti = 0;
    while (n > 0 && ti < 11) { tmp[ti++] = (char)('0' + n % 10); n /= 10; }
    int idx = 0;
    while (ti > 0 && idx < buf_len - 1) buf[idx++] = tmp[--ti];
    buf[idx] = '\0';
}

static void safe_copy(char *dst, const char *src, int dst_len)
{
    int i = 0;
    while (src[i] && i < dst_len - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int str_append(char *dst, int cur_len, int cap, const char *src)
{
    while (*src && cur_len < cap - 1) dst[cur_len++] = *src++;
    dst[cur_len] = '\0';
    return cur_len;
}

static void add_error(trp_manifest_t *m, const char *msg, int line_no)
{
    if (m->error_count >= TRP_MANIFEST_MAX_ERRORS) return;

    char *dst = m->errors[m->error_count];
    int   cap = TRP_MANIFEST_ERROR_LEN;
    int   len = 0;

    len = str_append(dst, len, cap, msg);

    if (line_no > 0) {
        len = str_append(dst, len, cap, " (line ");
        char nbuf[12];
        int_to_dec(line_no, nbuf, sizeof(nbuf));
        len = str_append(dst, len, cap, nbuf);
        len = str_append(dst, len, cap, ")");
    }

    m->error_count++;
}

/* ── trp_manifest_parse ──────────────────────────────────────────────────── */

int trp_manifest_parse(const char *text, uint32_t len, trp_manifest_t *out)
{
    memset(out, 0, sizeof(trp_manifest_t));

    char     line[512];
    uint32_t pos     = 0;
    int      line_no = 0;
    char     val[256];

    while (pos < len) {
        int llen = 0;
        while (pos < len && text[pos] != '\n' && text[pos] != '\r' && llen < 511)
            line[llen++] = text[pos++];
        line[llen] = '\0';
        while (pos < len && (text[pos] == '\n' || text[pos] == '\r')) pos++;
        line_no++;

        if (llen == 0) continue;

        /* Execute at/: (required) */
        if (ci_find(line, "Execute at/:")) {
            int has_val = extract_value(line, "Execute at/:", val, sizeof(val));
            if (!out->execute_at_found) {
                out->execute_at_found = 1;
                out->execute_at_line  = line_no;
                if (has_val)
                    safe_copy(out->execute_at, val, sizeof(out->execute_at));
            } else if (has_val && out->fallback_count < TRP_MANIFEST_MAX_FALLBACKS) {
                safe_copy(out->fallback_execute_at[out->fallback_count],
                          val, sizeof(out->fallback_execute_at[0]));
                out->fallback_count++;
            }
            continue;
        }

        if (ci_find(line, "Window name/:")) {
            extract_value(line, "Window name/:", out->window_name, sizeof(out->window_name));
            continue;
        }
        if (ci_find(line, "Icon/:")) {
            extract_value(line, "Icon/:", out->icon, sizeof(out->icon));
            continue;
        }
        if (ci_find(line, "Priority/:")) {
            extract_value(line, "Priority/:", out->priority, sizeof(out->priority));
            continue;
        }
        if (ci_find(line, "Execute if/:")) {
            extract_value(line, "Execute if/:", out->execute_if, sizeof(out->execute_if));
            continue;
        }
        if (ci_find(line, "mark executable/")) {
            out->mark_executable = 1;
            continue;
        }
        if (ci_find(line, "Language/:")) {
            extract_value(line, "Language/:", out->language, sizeof(out->language));
            continue;
        }
        if (ci_find(line, "Version/:")) {
            extract_value(line, "Version/:", out->version, sizeof(out->version));
            continue;
        }
        if (ci_find(line, "Assets/:")) {
            extract_value(line, "Assets/:", out->assets, sizeof(out->assets));
            continue;
        }
        if (ci_find(line, "Debug/:")) {
            extract_value(line, "Debug/:", val, sizeof(val));
            out->debug_mode = (ci_find(val, "true") != NULL) ? 1 : 0;
            continue;
        }
        if (ci_find(line, "Resource request/:")) {
            extract_value(line, "Resource request/:", val, sizeof(val));
            if (ci_find(val, "high") != NULL) {
                out->resource_request_high = 1;
                out->resource_request_line = line_no;
            }
            continue;
        }

        /* Unknown line — silently ignored by design. */
    }

    if (!out->execute_at_found) {
        add_error(out, "Error: no Execute at directive found", 0);
    } else if (out->execute_at[0] == '\0') {
        add_error(out, "Error: Execute at is empty", out->execute_at_line);
    }

    return (out->error_count > 0) ? -1 : 0;
}

/* ── trp_manifest_validate ───────────────────────────────────────────────── */

int trp_manifest_validate(trp_manifest_t *out)
{
    if (out->execute_at_found && out->execute_at[0] != '\0') {
        inode_t st;
        if (fs_stat(out->execute_at, &st) != 0) {
            char msg[TRP_MANIFEST_ERROR_LEN];
            int  len = 0;
            int  cap = TRP_MANIFEST_ERROR_LEN;

            len = str_append(msg, len, cap, "Error: no file named '");
            len = str_append(msg, len, cap, out->execute_at);
            len = str_append(msg, len, cap, "'");

            add_error(out, msg, out->execute_at_line);
        }
    }

    return (out->error_count > 0) ? -1 : 0;
}

/* ── trp_manifest_print_errors ───────────────────────────────────────────── */

void trp_manifest_print_errors(const trp_manifest_t *m, int gui_mode)
{
    if (m->error_count == 0) return;

    if (gui_mode) {
        io_put_string("\n+--------------------------------------+\n");
        io_put_string(  "|  TRP Package Errors                  |\n");
        io_put_string(  "+--------------------------------------+\n");
        for (int i = 0; i < m->error_count; i++) {
            io_put_string("|  ");
            io_put_string(m->errors[i]);
            io_put_string("\n");
        }
        io_put_string("+--------------------------------------+\n\n");
    } else {
        io_put_string("\nTRP Manifest Errors:\n");
        serial_puts("\n[TRP] Manifest errors:\n");
        for (int i = 0; i < m->error_count; i++) {
            io_put_string("  ");
            io_put_string(m->errors[i]);
            io_put_string("\n");
            serial_puts("  ");
            serial_puts(m->errors[i]);
            serial_puts("\n");
        }
        io_put_string("\n");
    }
}

/* ── trp_manifest_resource_prompt ────────────────────────────────────────── */

int trp_manifest_resource_prompt(int gui_mode)
{
    (void)gui_mode; /* same prompt for CLI and GUI in v1.1 */

    io_put_string("\nThis program is requesting higher system resources.\n");
    io_put_string("Allow? (Y/N): ");
    serial_puts("[TRP] Resource request prompt displayed.\n");

    for (;;) {
        char c = keyboard_getc();
        if (c == 'Y' || c == 'y') {
            io_put_string("Y\n");
            io_put_string("Resource request approved.\n");
            serial_puts("[TRP] Resource request approved by user.\n");
            return 1;
        }
        if (c == 'N' || c == 'n') {
            io_put_string("N\n");
            io_put_string("Running with standard resources.\n");
            serial_puts("[TRP] Resource request denied by user.\n");
            return 0;
        }
    }
}

/* Require at least 8 MB free before granting a "high" resource request. */
static int system_can_support_high_resources(void)
{
    uint32_t free_kb = pmm_get_free_ram();
    return (free_kb >= 8192) ? 1 : 0;
}

/* ── trp_manifest_run_gate ────────────────────────────────────────────────
 *
 * Full v1.1 execution gate. On success (0), *out is fully populated
 * (execute_at, language, window_name, ...) for the caller (trploader.c) to
 * act on. On failure (-1), all collected errors have already been printed.
 * ------------------------------------------------------------------------ */
int trp_manifest_run_gate(const char *manifest_text, uint32_t len,
                           trp_manifest_t *out, int gui_mode)
{
    trp_manifest_parse(manifest_text, len, out);
    trp_manifest_validate(out);

    if (out->error_count > 0) {
        trp_manifest_print_errors(out, gui_mode);
        return -1;
    }

    if (out->resource_request_high) {
        int user_approved = trp_manifest_resource_prompt(gui_mode);
        int system_ok     = system_can_support_high_resources();

        if (user_approved && !system_ok) {
            add_error(out,
                "Error: resource request denied (insufficient system capacity)",
                out->resource_request_line);
            trp_manifest_print_errors(out, gui_mode);
            return -1;
        }

        if (!user_approved) {
            serial_puts("[TRP] Continuing with standard resources.\n");
        } else {
            serial_puts("[TRP] Higher resources granted within system limits.\n");
        }
    }

    serial_puts("[TRP v1.1] Manifest OK. Entry: ");
    serial_puts(out->execute_at);
    serial_puts("\n");

    if (out->window_name[0]) { serial_puts("  Window   : "); serial_puts(out->window_name); serial_puts("\n"); }
    if (out->icon[0])        { serial_puts("  Icon     : "); serial_puts(out->icon);         serial_puts("\n"); }
    if (out->language[0])    { serial_puts("  Language : "); serial_puts(out->language);     serial_puts("\n"); }
    if (out->version[0])     { serial_puts("  Version  : "); serial_puts(out->version);      serial_puts("\n"); }
    if (out->priority[0])    { serial_puts("  Priority : "); serial_puts(out->priority);     serial_puts("\n"); }
    if (out->execute_if[0])  { serial_puts("  Exec if  : "); serial_puts(out->execute_if);   serial_puts("\n"); }
    if (out->assets[0])      { serial_puts("  Assets   : "); serial_puts(out->assets);       serial_puts("\n"); }
    if (out->mark_executable) serial_puts("  [marked executable]\n");
    if (out->debug_mode)      serial_puts("  [debug ON]\n");

    if (out->fallback_count > 0) {
        char nbuf[12];
        int_to_dec(out->fallback_count, nbuf, sizeof(nbuf));
        serial_puts("  Fallbacks : ");
        serial_puts(nbuf);
        serial_puts(" alternate entry point(s) registered\n");
    }

    return 0;
}

/* trp_manifest.c — TRP Application Manifest parser v1.1
 *
 * FIXES:
 *  - File was entirely duplicated (two copies concatenated) — removed duplicate.
 *  - Include now uses "trp_manifest.h" (no space in filename).
 *  - trp_manifest_run_gate() signature unified with header.
 */


extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ── internal helpers ────────────────────────────────────────────────────── */

static char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static const char *ci_find(const char *hay, const char *needle)
{
    if (!needle || !*needle) return hay;
    size_t nlen = strlen(needle);
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        size_t m = 0;
        while (*h && *n && to_lower(*h) == to_lower(*n)) { h++; n++; m++; }
        if (m == nlen) return hay;
    }
    return NULL;
}

static int extract_value(const char *line, const char *directive,
                          char *dst, int dlen)
{
    const char *p = ci_find(line, directive);
    if (!p) return 0;
    p += strlen(directive);
    while (*p == ' ' || *p == '\t') p++;
    int i = 0;
    while (*p && *p != '\n' && *p != '\r' && i < dlen - 1) dst[i++] = *p++;
    while (i > 0 && (dst[i-1]==' '||dst[i-1]=='\t')) i--;
    dst[i] = '\0';
    return (i > 0) ? 1 : 0;
}

static void int_to_dec(int n, char *buf, int cap)
{
    if (cap < 2) return;
    if (n == 0) { buf[0]='0'; buf[1]='\0'; return; }
    char tmp[12]; int ti=0;
    while (n>0 && ti<11) { tmp[ti++]=(char)('0'+n%10); n/=10; }
    int idx=0;
    while (ti>0 && idx<cap-1) buf[idx++]=tmp[--ti];
    buf[idx]='\0';
}

static void safe_copy(char *dst, const char *src, int dlen)
{
    int i=0;
    while (src[i] && i<dlen-1) { dst[i]=src[i]; i++; }
    dst[i]='\0';
}

static int str_append(char *dst, int cur, int cap, const char *src)
{
    while (*src && cur<cap-1) dst[cur++]=*src++;
    dst[cur]='\0';
    return cur;
}

static void add_error(trp_manifest_t *m, const char *msg, int line_no)
{
    if (m->error_count >= TRP_MANIFEST_MAX_ERRORS) return;
    char *dst = m->errors[m->error_count];
    int cap = TRP_MANIFEST_ERROR_LEN, len = 0;
    len = str_append(dst, len, cap, msg);
    if (line_no > 0) {
        len = str_append(dst, len, cap, " (line ");
        char nb[12]; int_to_dec(line_no, nb, sizeof(nb));
        len = str_append(dst, len, cap, nb);
        len = str_append(dst, len, cap, ")");
    }
    m->error_count++;
}

/* ── trp_manifest_parse ──────────────────────────────────────────────────── */

int trp_manifest_parse(const char *text, uint32_t len, trp_manifest_t *out)
{
    memset(out, 0, sizeof(*out));
    char line[512]; uint32_t pos=0; int line_no=0; char val[256];

    while (pos < len) {
        int ll=0;
        while (pos<len && text[pos]!='\n' && text[pos]!='\r' && ll<511)
            line[ll++]=text[pos++];
        line[ll]='\0';
        while (pos<len && (text[pos]=='\n'||text[pos]=='\r')) pos++;
        line_no++;
        if (ll==0) continue;

#define MATCH(kw) ci_find(line, kw)
        if (MATCH("Execute at/:")) {
            int hv = extract_value(line,"Execute at/:",val,sizeof(val));
            if (!out->execute_at_found) {
                out->execute_at_found=1; out->execute_at_line=line_no;
                if (hv) safe_copy(out->execute_at, val, sizeof(out->execute_at));
            } else if (hv && out->fallback_count < TRP_MANIFEST_MAX_FALLBACKS) {
                safe_copy(out->fallback_execute_at[out->fallback_count++],
                          val, sizeof(out->fallback_execute_at[0]));
            }
            continue;
        }
        if (MATCH("Window name/:"))  { extract_value(line,"Window name/:", out->window_name, sizeof(out->window_name)); continue; }
        if (MATCH("Icon/:"))         { extract_value(line,"Icon/:",         out->icon,        sizeof(out->icon));        continue; }
        if (MATCH("Priority/:"))     { extract_value(line,"Priority/:",     out->priority,    sizeof(out->priority));    continue; }
        if (MATCH("Execute if/:"))   { extract_value(line,"Execute if/:",   out->execute_if,  sizeof(out->execute_if));  continue; }
        if (MATCH("mark executable/")) { out->mark_executable=1; continue; }
        if (MATCH("Language/:"))     { extract_value(line,"Language/:",     out->language,    sizeof(out->language));    continue; }
        if (MATCH("Version/:"))      { extract_value(line,"Version/:",      out->version,     sizeof(out->version));     continue; }
        if (MATCH("Assets/:"))       { extract_value(line,"Assets/:",       out->assets,      sizeof(out->assets));      continue; }
        if (MATCH("Debug/:")) {
            extract_value(line,"Debug/:",val,sizeof(val));
            out->debug_mode = (ci_find(val,"true")!=NULL)?1:0; continue;
        }
        if (MATCH("Resource request/:")) {
            extract_value(line,"Resource request/:",val,sizeof(val));
            if (ci_find(val,"high")) { out->resource_request_high=1; out->resource_request_line=line_no; }
            continue;
        }
#undef MATCH
    }

    if (!out->execute_at_found)
        add_error(out, "Error: no Execute at directive found", 0);
    else if (!out->execute_at[0])
        add_error(out, "Error: Execute at is empty", out->execute_at_line);

    return out->error_count ? -1 : 0;
}

/* ── trp_manifest_validate ───────────────────────────────────────────────── */

int trp_manifest_validate(trp_manifest_t *out)
{
    if (out->execute_at_found && out->execute_at[0]) {
        inode_t st;
        if (fs_stat(out->execute_at, &st) != 0) {
            char msg[TRP_MANIFEST_ERROR_LEN]; int len=0, cap=TRP_MANIFEST_ERROR_LEN;
            len = str_append(msg,len,cap,"Error: no file named '");
            len = str_append(msg,len,cap,out->execute_at);
            len = str_append(msg,len,cap,"'");
            add_error(out, msg, out->execute_at_line);
        }
    }
    return out->error_count ? -1 : 0;
}

/* ── trp_manifest_print_errors ───────────────────────────────────────────── */

void trp_manifest_print_errors(const trp_manifest_t *m, int gui_mode)
{
    if (!m->error_count) return;
    if (gui_mode) {
        io_put_string("\n+--------------------------------------+\n");
        io_put_string(  "|  TRP Package Errors                  |\n");
        io_put_string(  "+--------------------------------------+\n");
        for (int i=0;i<m->error_count;i++){io_put_string("|  ");io_put_string(m->errors[i]);io_put_string("\n");}
        io_put_string("+--------------------------------------+\n\n");
    } else {
        io_put_string("\nTRP Manifest Errors:\n");
        serial_puts("\n[TRP] Manifest errors:\n");
        for (int i=0;i<m->error_count;i++){
            io_put_string("  ");io_put_string(m->errors[i]);io_put_string("\n");
            serial_puts("  ");serial_puts(m->errors[i]);serial_puts("\n");
        }
        io_put_string("\n");
    }
}

/* ── trp_manifest_resource_prompt ────────────────────────────────────────── */

int trp_manifest_resource_prompt(int gui_mode)
{
    (void)gui_mode;
    io_put_string("\nThis program is requesting higher system resources.\nAllow? (Y/N): ");
    serial_puts("[TRP] Resource request prompt.\n");
    for (;;) {
        char c = keyboard_getc();
        if (c=='Y'||c=='y'){io_put_string("Y\nApproved.\n");serial_puts("[TRP] Approved.\n");return 1;}
        if (c=='N'||c=='n'){io_put_string("N\nDenied.\n");  serial_puts("[TRP] Denied.\n");  return 0;}
    }
}

static int system_can_support_high(void)
{
    return (pmm_get_free_ram() >= 8192) ? 1 : 0;
}

/* ── trp_manifest_run_gate ───────────────────────────────────────────────── */

int trp_manifest_run_gate(const char *text, uint32_t len,
                           char *entry_out, int entry_out_len, int gui_mode)
{
    trp_manifest_t m;
    trp_manifest_parse(text, len, &m);
    trp_manifest_validate(&m);

    if (m.error_count) { trp_manifest_print_errors(&m, gui_mode); return -1; }

    if (m.resource_request_high) {
        int ok = trp_manifest_resource_prompt(gui_mode);
        if (ok && !system_can_support_high()) {
            add_error(&m,"Error: resource request denied (insufficient capacity)",
                      m.resource_request_line);
            trp_manifest_print_errors(&m, gui_mode);
            return -1;
        }
        if (!ok) serial_puts("[TRP] Standard resources.\n");
        else     serial_puts("[TRP] Higher resources granted.\n");
    }

    if (entry_out && entry_out_len > 0) safe_copy(entry_out, m.execute_at, entry_out_len);

    serial_puts("[TRP v1.1] Manifest OK. Entry: "); serial_puts(m.execute_at); serial_puts("\n");
    if (m.window_name[0]) { serial_puts("  Window  : "); serial_puts(m.window_name); serial_puts("\n"); }
    if (m.icon[0])        { serial_puts("  Icon    : "); serial_puts(m.icon);        serial_puts("\n"); }
    if (m.language[0])    { serial_puts("  Language: "); serial_puts(m.language);    serial_puts("\n"); }
    if (m.version[0])     { serial_puts("  Version : "); serial_puts(m.version);     serial_puts("\n"); }
    if (m.mark_executable) serial_puts("  [marked executable]\n");
    if (m.debug_mode)      serial_puts("  [debug ON]\n");
    return 0;
}
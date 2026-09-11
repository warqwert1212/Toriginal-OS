#include "trp_manifest.h"
#include "string.h"
#include "fs.h"
#include "io.h"
#include "keyboard.h"
#include "pmm.h"
#include "serial.h"
#include "pit.h"
#include "sha256.h"

/* trp_manifest.c — TRP Application Manifest parser v1.1
 *
 * FIXES:
 *  - File was entirely duplicated (two copies concatenated) — removed duplicate.
 *  - Include now uses "trp_manifest.h" (no space in filename).
 *  - trp_manifest_run_gate() signature unified with header.
 *  - keybord.h -> keyboard.h (typo fix), serial_puts/putc via serial.h
 *    instead of ad-hoc externs.
 */

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

static void trim_value(char *s)
{
    int start = 0;
    while (s[start] == ' ' || s[start] == '\t') start++;
    if (start > 0) {
        int i = 0;
        while (s[start]) s[i++] = s[start++];
        s[i] = '\0';
    }
    int end = (int)strlen(s);
    while (end > 0 && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\r' || s[end-1] == '\n')) s[--end] = '\0';
}

static int split_list(char *text, char dst[][256], int max_items)
{
    int count = 0;
    char *p = text;
    while (*p && count < max_items) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != ',' && *p != '\n' && *p != '\r') p++;
        if (p > start) {
            size_t len = (size_t)(p - start);
            if (len >= sizeof(dst[0])) len = sizeof(dst[0]) - 1;
            memcpy(dst[count], start, len);
            dst[count][len] = '\0';
            trim_value(dst[count]);
            if (dst[count][0]) count++;
        }
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
    }
    return count;
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
        if (MATCH("Description/:")) { extract_value(line,"Description/:", out->description, sizeof(out->description)); continue; }
        if (MATCH("Args/:"))        { extract_value(line,"Args/:",        out->args,        sizeof(out->args));        continue; }
        if (MATCH("Depends/:")) {
            char val[512];
            if (extract_value(line, "Depends/:", val, sizeof(val))) {
                out->depends_count = split_list(val, out->depends, TRP_MANIFEST_MAX_DEPENDS);
            }
            continue;
        }
        if (MATCH("Conflicts/:")) {
            char val[512];
            if (extract_value(line, "Conflicts/:", val, sizeof(val))) {
                out->conflicts_count = split_list(val, out->conflicts, TRP_MANIFEST_MAX_CONFLICTS);
            }
            continue;
        }
        if (MATCH("Repository/:")) { extract_value(line,"Repository/:", out->repository, sizeof(out->repository)); continue; }
        if (MATCH("Signer/:"))     { extract_value(line,"Signer/:",     out->signer,     sizeof(out->signer));     continue; }
        if (MATCH("Signature/:"))  { extract_value(line,"Signature/:",  out->signature,  sizeof(out->signature));  continue; }
        if (MATCH("Checksum/:")) {
            extract_value(line,"Checksum/:", val, sizeof(val));
            /* Normalize to lowercase and only accept it if it looks like a
             * plausible 64-hex-char sha256 digest - a malformed value here
             * should be a manifest error, not a silently-ignored no-op. */
            size_t vl = strlen(val);
            if (vl == 64) {
                int ok = 1;
                for (size_t i = 0; i < vl; i++) {
                    char c = val[i];
                    int is_hex = (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
                    if (!is_hex) { ok = 0; break; }
                    if (c>='A'&&c<='F') val[i] = (char)(c + 32);
                }
                if (ok) safe_copy(out->checksum, val, sizeof(out->checksum));
                else add_error(out, "Error: Checksum is not valid hex", line_no);
            } else if (vl > 0) {
                add_error(out, "Error: Checksum must be a 64-character sha256 hex digest", line_no);
            }
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

int trp_manifest_validate_ex(trp_manifest_t *out, int skip_file_check)
{
    if (!skip_file_check && out->execute_at_found && out->execute_at[0]) {
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

int trp_manifest_validate(trp_manifest_t *out)
{
    return trp_manifest_validate_ex(out, 0);
}

/* ── trp_manifest_verify_checksum ───────────────────────────────────────── */

static int hexval(char c) {
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return -1;
}

int trp_manifest_verify_checksum(const trp_manifest_t *m,
                                  const uint8_t *payload, uint32_t payload_len)
{
    if (!m->checksum[0]) return 1; /* nothing to check */

    uint8_t want[32];
    for (int i = 0; i < 32; i++) {
        int hi = hexval(m->checksum[i*2]);
        int lo = hexval(m->checksum[i*2+1]);
        if (hi < 0 || lo < 0) return 0;
        want[i] = (uint8_t)((hi << 4) | lo);
    }

    uint8_t got[32];
    sha256(payload, payload_len, got);
    return sha256_digest_equal(want, got);
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

#define TRP_PROMPT_TIMEOUT_MS 30000

int trp_manifest_resource_prompt(int gui_mode)
{
    (void)gui_mode;
    io_put_string("\nThis program is requesting higher system resources.\nAllow? (Y/N): ");
    serial_puts("[TRP] Resource request prompt.\n");

    /* FIX: was keyboard_getc() in an unbounded loop - fine while this is
     * only ever reached from an interactive shell command with a live
     * keyboard (the only current caller, `run`, is exactly that), but a
     * silent permanent hang the moment anything invokes trp_load() from
     * an unattended path. Poll non-blockingly with a timeout and default
     * to Denied on timeout - a human answering normally never notices. */
    uint32_t waited = 0;
    for (;;) {
        char c = keyboard_getc_nb();
        if (c=='Y'||c=='y'){io_put_string("Y\nApproved.\n");serial_puts("[TRP] Approved.\n");return 1;}
        if (c=='N'||c=='n'){io_put_string("N\nDenied.\n");  serial_puts("[TRP] Denied.\n");  return 0;}
        if (waited >= TRP_PROMPT_TIMEOUT_MS) {
            io_put_string("\n(no response - denying by default)\n");
            serial_puts("[TRP] Resource prompt timed out - denied.\n");
            return 0;
        }
        pit_sleep(50);
        waited += 50;
    }
}

static int system_can_support_high(void)
{
    return (pmm_get_free_ram() >= 8192) ? 1 : 0;
}

/* ── trp_manifest_run_gate ───────────────────────────────────────────────── */

int trp_manifest_run_gate2(const char *text, uint32_t len,
                            trp_manifest_t *manifest_out,
                            char *entry_out, int entry_out_len, int gui_mode,
                            int has_embedded_payload)
{
    trp_manifest_t m;
    trp_manifest_parse(text, len, &m);
    trp_manifest_validate_ex(&m, has_embedded_payload);

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

    serial_puts("[TRP v1.2] Manifest OK. Entry: "); serial_puts(m.execute_at); serial_puts("\n");
    if (m.window_name[0])  { serial_puts("  Window     : "); serial_puts(m.window_name);  serial_puts("\n"); }
    if (m.icon[0])         { serial_puts("  Icon       : "); serial_puts(m.icon);         serial_puts("\n"); }
    if (m.language[0])     { serial_puts("  Language   : "); serial_puts(m.language);     serial_puts("\n"); }
    if (m.version[0])      { serial_puts("  Version    : "); serial_puts(m.version);      serial_puts("\n"); }
    if (m.description[0])  { serial_puts("  Description: "); serial_puts(m.description);  serial_puts("\n"); }
    if (m.args[0])         { serial_puts("  Args       : "); serial_puts(m.args);         serial_puts("\n"); }
    if (m.checksum[0])     { serial_puts("  Checksum   : "); serial_puts(m.checksum);     serial_puts("\n"); }
    if (m.mark_executable) serial_puts("  [marked executable]\n");
    if (m.debug_mode)      serial_puts("  [debug ON]\n");

    if (manifest_out) *manifest_out = m;
    return 0;
}

int trp_manifest_run_gate(const char *text, uint32_t len,
                           char *entry_out, int entry_out_len, int gui_mode)
{
    return trp_manifest_run_gate2(text, len, NULL, entry_out, entry_out_len, gui_mode, 0);
}

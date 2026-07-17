#include "trploader.h"
#include "trp_manifest.h"
#include "fs.h"
#include "mm.h"
#include "io.h"
#include "string.h"
#include "process.h"
#include "serial.h"

/* trploader.c - TRP package loader
 *
 * FIXES:
 *  - Removed orphaned block at top of trp_load() that used buf/hdr before
 *    they were declared (was copy-paste wreckage).
 *  - Local trp_manifest_t renamed to trp_pkg_manifest_t to avoid shadowing
 *    the global typedef from trp_manifest.h.
 *  - trp_manifest_run_gate() call uses correct 5-arg signature.
 *  - Normalized to #include "serial.h" instead of ad-hoc externs.
 *  - TRP_MAGIC / trp_file_header_t now come from trploader.h (single
 *    shared definition — shell.c's trpbuild uses the same one, so the
 *    packer and loader can never silently drift apart on layout again).
 */

typedef struct {
    int  is_executable;
    char window_name[128];
    char icon_path[256];
    char lang[16];
    char version[32];
    char author[64];
    char min_os_version[16];
} trp_pkg_manifest_t;

static void trp_log(const char *msg)
{
    serial_puts("[TRP] "); serial_puts(msg); serial_puts("\n");
}

static int pkg_extract(const char *line, const char *dir, char *dst, int dlen)
{
    int dl = (int)strlen(dir);
    if (strncmp(line, dir, (size_t)dl) != 0) return 0;
    const char *v = line + dl;
    int vl = (int)strlen(v);
    if (vl > 0 && v[vl-1]=='/') vl--;
    if (vl >= dlen) vl = dlen-1;
    strncpy(dst, v, (size_t)vl);
    dst[vl]='\0';
    return 1;
}

static void pkg_parse_line(const char *line, trp_pkg_manifest_t *m)
{
    while (*line==' '||*line=='\t') line++;
    if (*line!='/') return;
    line++;
    if (strncmp(line,"this is executable/",19)==0){m->is_executable=1;return;}
    pkg_extract(line,"window_name:",    m->window_name,    sizeof(m->window_name));
    pkg_extract(line,"icon:",           m->icon_path,      sizeof(m->icon_path));
    pkg_extract(line,"lang:",           m->lang,           sizeof(m->lang));
    pkg_extract(line,"version:",        m->version,        sizeof(m->version));
    pkg_extract(line,"author:",         m->author,         sizeof(m->author));
    pkg_extract(line,"min_os_version:", m->min_os_version, sizeof(m->min_os_version));
}

static void pkg_parse(const char *text, uint32_t len, trp_pkg_manifest_t *m)
{
    memset(m, 0, sizeof(*m));
    strncpy(m->lang, "bin", sizeof(m->lang));
    char line[256]; uint32_t i=0;
    while (i < len) {
        int ll=0;
        while (i<len && text[i]!='\n' && text[i]!='\r' && ll<255) line[ll++]=text[i++];
        line[ll]='\0';
        while (i<len&&(text[i]=='\n'||text[i]=='\r')) i++;
        if (ll>0) pkg_parse_line(line,m);
    }
}

/* Must match kernel64.ld's .trp_code_area (pinned address, not
 * computed) and the trpc host-side compiler's -Ttext flag. All three
 * have to agree — see the comment in kernel64.ld for why this is a
 * literal constant rather than "wherever the linker put it", and why
 * it moved from 0x300000 to 0x500000. */
#define TRP_CODE_LOAD_ADDR   ((uintptr_t)0x500000ULL)
#define TRP_CODE_MAX_SIZE    ((uintptr_t)0x100000ULL) /* 1 MiB */

static int trp_exec_bin(const uint8_t *payload, uint32_t plen, pid_t pid)
{
    if (!plen) { trp_log("Empty binary payload."); return -1; }
    if (plen > TRP_CODE_MAX_SIZE) { trp_log("Binary payload too large for fixed load area."); return -1; }

    uint8_t *mem = (uint8_t *)(uintptr_t)TRP_CODE_LOAD_ADDR;
    memcpy(mem, payload, plen);
    process_t *proc = process_get_by_pid(pid);
    if (proc) proc->context.rip = (uint64_t)(uintptr_t)mem;
    trp_log("Binary payload loaded at fixed address.");
    return 0;
}

static int trp_exec_text(const uint8_t *payload, uint32_t plen, const char *lang)
{
    serial_puts("[TRP] Source payload (lang="); serial_puts(lang); serial_puts("):\n");
    uint32_t n = plen < 128 ? plen : 128;
    for (uint32_t i=0;i<n;i++) if(payload[i]) serial_putc((char)payload[i]);
    serial_puts("\n[TRP] Source execution not yet implemented.\n");
    return -1;
}

int trp_load(const char *filename, pid_t pid)
{
    inode_t stat;
    if (fs_stat(filename, &stat) != 0) { trp_log("File not found."); return -1; }

    uint32_t file_size = (uint32_t)stat.size;
    if (file_size < (uint32_t)sizeof(trp_file_header_t)) {
        trp_log("Too small to be TRP."); return -1;
    }

    fd_t fd = fs_open(filename, O_RDONLY, 0);
    if (fd < 0) { trp_log("Open failed."); return -1; }

    uint8_t *buf = (uint8_t *)kmalloc(file_size);
    if (!buf) { trp_log("OOM."); fs_close(fd); return -1; }

    ssize_t r = fs_read(fd, buf, file_size);
    fs_close(fd);
    if (r < 0 || (uint32_t)r != file_size) {
        trp_log("Read error."); kfree(buf); return -1;
    }

    trp_file_header_t *hdr = (trp_file_header_t *)buf;
    if (hdr->magic != TRP_MAGIC) { trp_log("Invalid magic."); kfree(buf); return -1; }

    /* FIX: manifest_offset/len and payload_offset/len are uint32_t fields
     * read straight from the (attacker-controlled) file. `offset + len`
     * can wrap around 2^32 and pass a "> file_size" check while the
     * actual region is nowhere near valid - do the arithmetic widened to
     * 64 bits, and reject an offset that's already out of range before
     * even adding the length. */
    uint64_t mo = hdr->manifest_offset, ml = hdr->manifest_len;
    if (mo > file_size || mo + ml > file_size) { trp_log("Manifest OOB."); kfree(buf); return -1; }

    if (hdr->payload_len) {
        uint64_t po = hdr->payload_offset, pl = hdr->payload_len;
        if (po > file_size || po + pl > file_size) { trp_log("Payload OOB."); kfree(buf); return -1; }
    }

    char entry_file[256];
    int gate = trp_manifest_run_gate(
        (const char *)(buf + hdr->manifest_offset),
        hdr->manifest_len,
        entry_file, sizeof(entry_file),
        0
    );
    if (gate != 0) { kfree(buf); return -1; }

    trp_pkg_manifest_t manifest;
    pkg_parse((const char *)(buf + hdr->manifest_offset), hdr->manifest_len, &manifest);

    serial_puts("[TRP] Manifest:\n");
    serial_puts("  executable: "); serial_puts(manifest.is_executable?"yes":"no"); serial_puts("\n");
    if (manifest.window_name[0]){serial_puts("  window: ");serial_puts(manifest.window_name);serial_puts("\n");}
    if (manifest.lang[0])       {serial_puts("  lang:   ");serial_puts(manifest.lang);        serial_puts("\n");}

    if (!manifest.is_executable) { trp_log("Not executable - refusing."); kfree(buf); return -1; }
    if (!hdr->payload_len)        { trp_log("No payload.");                kfree(buf); return -1; }

    const uint8_t *payload = buf + hdr->payload_offset;
    uint32_t       plen    = hdr->payload_len;
    int result = -1;

    if (strcmp(manifest.lang,"bin")==0 || !manifest.lang[0])
        result = trp_exec_bin(payload, plen, pid);
    else if (strcmp(manifest.lang,"c")==0 || strcmp(manifest.lang,"py")==0)
        result = trp_exec_text(payload, plen, manifest.lang);
    else {
        serial_puts("[TRP] Unknown lang: "); serial_puts(manifest.lang); serial_puts("\n");
    }

    kfree(buf);
    trp_log(result==0?"Load successful.":"Load failed.");
    return result;
}

int trp_create_package(fd_t out_fd, const char *manifest_text,
                        const uint8_t *payload, uint32_t payload_len)
{
    if (!manifest_text || !payload) return -1;
    uint32_t mlen = (uint32_t)strlen(manifest_text);
    trp_file_header_t hdr;
    hdr.magic           = TRP_MAGIC;
    hdr.version         = 1;
    hdr.manifest_offset = (uint32_t)sizeof(hdr);
    hdr.manifest_len    = mlen;
    hdr.payload_offset  = hdr.manifest_offset + mlen;
    hdr.payload_len     = payload_len;
    ssize_t w = 0;
    w += fs_write(out_fd, &hdr,          sizeof(hdr));
    w += fs_write(out_fd, manifest_text, mlen);
    w += fs_write(out_fd, payload,       payload_len);
    return (int)w;
}

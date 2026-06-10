// ==============================================================================
// TRP_LOADER.C - Toriginal Runtime Package Loader
//
// Flat TRP binary layout:
//   [0..3]   magic            'T','R','P','K'  (0x4B505254)
//   [4..7]   version          uint32 (1)
//   [8..11]  manifest_offset  uint32 — byte offset to manifest text
//   [12..15] manifest_len     uint32 — byte length of manifest
//   [16..19] payload_offset   uint32 — byte offset to payload
//   [20..23] payload_len      uint32 — byte length of payload
//   [24..]   data             manifest text then payload bytes
//
// Manifest directives (one per line):
//   /this is executable/
//   /window_name:My App/
//   /icon:/myapp/icons/app.ico/
//   /lang:bin/        (bin, c, py)
//   /version:1.0/
//   /author:warqwert/
//   /min_os_version:1.0/
// ==============================================================================

#include "trp_loader.h"
#include "../include/fs.h"
#include "../include/mm.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../include/process.h"

#define TRP_MAGIC 0x4B505254u   // 'TRPK'

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t manifest_offset;
    uint32_t manifest_len;
    uint32_t payload_offset;
    uint32_t payload_len;
} __attribute__((packed)) trp_header_t;

typedef struct {
    int  is_executable;
    char window_name[128];
    char icon_path[256];
    char lang[16];
    char version[32];
    char author[64];
    char min_os_version[16];
} trp_manifest_t;

void serial_puts(const char *s);
void serial_putc(char c);

static void trp_log(const char *msg) {
    serial_puts("[TRP] "); serial_puts(msg); serial_puts("\n");
}

// Extract value from "/directive:value/" line
static int manifest_extract(const char *line, const char *directive,
                             char *dst, int dst_len) {
    int dlen = (int)strlen(directive);
    if (strncmp(line, directive, (size_t)dlen) != 0) return 0;
    const char *val = line + dlen;
    int vlen = (int)strlen(val);
    if (vlen > 0 && val[vlen-1] == '/') vlen--;
    if (vlen >= dst_len) vlen = dst_len - 1;
    strncpy(dst, val, (size_t)vlen);
    dst[vlen] = '\0';
    return 1;
}

static void manifest_parse_line(const char *line, trp_manifest_t *m) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line != '/') return;
    line++;
    if (strncmp(line, "this is executable/", 19) == 0) { m->is_executable = 1; return; }
    manifest_extract(line, "window_name:",      m->window_name,    sizeof(m->window_name));
    manifest_extract(line, "icon:",             m->icon_path,      sizeof(m->icon_path));
    manifest_extract(line, "lang:",             m->lang,           sizeof(m->lang));
    manifest_extract(line, "version:",          m->version,        sizeof(m->version));
    manifest_extract(line, "author:",           m->author,         sizeof(m->author));
    manifest_extract(line, "min_os_version:",   m->min_os_version, sizeof(m->min_os_version));
}

static void manifest_parse(const char *text, uint32_t len, trp_manifest_t *m) {
    memset(m, 0, sizeof(trp_manifest_t));
    strncpy(m->lang, "bin", sizeof(m->lang));   // default lang

    char line[256];
    uint32_t i = 0;
    while (i < len) {
        int llen = 0;
        while (i < len && text[i] != '\n' && text[i] != '\r' && llen < 255)
            line[llen++] = text[i++];
        line[llen] = '\0';
        while (i < len && (text[i] == '\n' || text[i] == '\r')) i++;
        if (llen > 0) manifest_parse_line(line, m);
    }
}

// Execute raw binary payload
static int trp_execute_bin(const uint8_t *payload, uint32_t len,
                            pid_t pid) {
    if (len == 0) { trp_log("Empty binary payload."); return -1; }

    uint8_t *exec_mem = (uint8_t *)kmalloc(((len + 4095) / 4096) * 4096);
    if (!exec_mem) { trp_log("OOM for payload."); return -1; }

    memcpy(exec_mem, payload, len);

    process_t *proc = process_get_by_pid(pid);
    if (proc) proc->context.rip = (uint64_t)(uintptr_t)exec_mem;

    trp_log("Binary payload loaded.");
    return 0;
}

// Source payload — log for now, execute in future update
static int trp_execute_text(const uint8_t *payload, uint32_t len,
                             const char *lang) {
    serial_puts("[TRP] Source payload received (lang=");
    serial_puts(lang);
    serial_puts("). First 128 chars:\n");
    uint32_t n = len < 128 ? len : 128;
    for (uint32_t i = 0; i < n; i++) {
        if (payload[i]) serial_putc((char)payload[i]);
    }
    serial_puts("\n[TRP] C/Python execution coming in a future update.\n");
    return -1;
}

int trp_load(const char *filename, pid_t pid) {
    serial_puts("[TRP] Loading: "); serial_puts(filename); serial_puts("\n");

    inode_t stat;
    if (fs_stat(filename, &stat) != 0) { trp_log("File not found."); return -1; }

    uint32_t file_size = (uint32_t)stat.size;
    if (file_size < (uint32_t)sizeof(trp_header_t)) {
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

    trp_header_t *hdr = (trp_header_t *)buf;

    if (hdr->magic != TRP_MAGIC) {
        trp_log("Invalid magic."); kfree(buf); return -1;
    }
    if (hdr->manifest_offset + hdr->manifest_len > file_size) {
        trp_log("Manifest OOB."); kfree(buf); return -1;
    }
    if (hdr->payload_len > 0 &&
        hdr->payload_offset + hdr->payload_len > file_size) {
        trp_log("Payload OOB."); kfree(buf); return -1;
    }

    trp_manifest_t manifest;
    manifest_parse((const char *)(buf + hdr->manifest_offset),
                   hdr->manifest_len, &manifest);

    serial_puts("[TRP] Manifest:\n");
    serial_puts("  executable: "); serial_puts(manifest.is_executable ? "yes" : "no"); serial_puts("\n");
    if (manifest.window_name[0]) { serial_puts("  window: "); serial_puts(manifest.window_name); serial_puts("\n"); }
    if (manifest.lang[0])        { serial_puts("  lang:   "); serial_puts(manifest.lang);        serial_puts("\n"); }
    if (manifest.version[0])     { serial_puts("  ver:    "); serial_puts(manifest.version);     serial_puts("\n"); }
    if (manifest.author[0])      { serial_puts("  author: "); serial_puts(manifest.author);      serial_puts("\n"); }

    if (!manifest.is_executable) {
        trp_log("Not marked executable — refusing."); kfree(buf); return -1;
    }

    if (hdr->payload_len == 0) {
        trp_log("No payload."); kfree(buf); return -1;
    }

    const uint8_t *payload = buf + hdr->payload_offset;
    uint32_t       plen    = hdr->payload_len;
    int result = -1;

    if (strcmp(manifest.lang, "bin") == 0 || manifest.lang[0] == '\0')
        result = trp_execute_bin(payload, plen, pid);
    else if (strcmp(manifest.lang, "c") == 0 || strcmp(manifest.lang, "py") == 0)
        result = trp_execute_text(payload, plen, manifest.lang);
    else {
        serial_puts("[TRP] Unknown lang: "); serial_puts(manifest.lang); serial_puts("\n");
    }

    kfree(buf);
    trp_log(result == 0 ? "Load successful." : "Load failed.");
    return result;
}

// Helper to build a TRP package programmatically (for future packager tool)
int trp_create_package(fd_t out_fd, const char *manifest_text,
                        const uint8_t *payload, uint32_t payload_len) {
    if (!manifest_text || !payload) return -1;
    uint32_t mlen = (uint32_t)strlen(manifest_text);

    trp_header_t hdr;
    hdr.magic           = TRP_MAGIC;
    hdr.version         = 1;
    hdr.manifest_offset = (uint32_t)sizeof(trp_header_t);
    hdr.manifest_len    = mlen;
    hdr.payload_offset  = hdr.manifest_offset + mlen;
    hdr.payload_len     = payload_len;

    ssize_t w = 0;
    w += fs_write(out_fd, &hdr,          sizeof(hdr));
    w += fs_write(out_fd, manifest_text, mlen);
    w += fs_write(out_fd, payload,       payload_len);
    return (int)w;
}
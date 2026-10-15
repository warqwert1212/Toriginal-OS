#include "trploader.h"
#include "trp_manifest.h"
#include "sha256.h"
#include "fs.h"
#include "mm.h"
#include "io.h"
#include "string.h"
#include "process.h"
#include "serial.h"
#include "paging.h"  /* mm_map_page_in() - trp_exec_bin() now maps its
                       * payload into the TARGET PROCESS's own address
                       * space instead of memcpy-ing into one shared
                       * physical region every process on the system
                       * used to alias. See this function's own
                       * comment below for what changed and why. */

/* trploader.c - TRP package loader
 *
 * FIXES:
 *  - Removed orphaned block at top of trp_load() that used buf/hdr before
 *    they were declared (was copy-paste wreckage).
 *  - Normalized to #include "serial.h" instead of ad-hoc externs.
 *  - TRP_MAGIC / trp_file_header_t now come from trploader.h (single
 *    shared definition — shell.c's trpbuild uses the same one, so the
 *    packer and loader can never silently drift apart on layout again).
 *  - v1.2: removed the SECOND, entirely independent manifest grammar that
 *    used to live here (trp_pkg_manifest_t / pkg_parse / pkg_extract, a
 *    "/directive:value" per-line format with a leading slash). That
 *    parser had nothing to do with the "Execute at/:", "Window name/:",
 *    etc. grammar trp_manifest_parse() (trp_manifest.c) actually
 *    implements and that trpbuild/trpc actually write - so a manifest
 *    that passed trp_manifest_run_gate()'s validation (the real grammar)
 *    would then get re-parsed here with the WRONG grammar, find no
 *    "/this is executable/" line (because that line never existed in any
 *    manifest anyone actually generates), and refuse to execute every
 *    single time. Every package built by trpbuild or trpc was therefore
 *    unrunnable via `run`/`trpm install` - this was the actual "does
 *    nothing when I run it" bug. Fixed by parsing the manifest exactly
 *    once, with the one real grammar, via trp_manifest_run_gate2(), and
 *    reading is-it-executable / language / checksum straight off the
 *    trp_manifest_t it returns.
 *  - v1.2: added Checksum/: verification - if the manifest carries one,
 *    the payload's actual sha256 must match it or the load is refused.
 */

static void trp_log(const char *msg)
{
    serial_puts("[TRP] "); serial_puts(msg); serial_puts("\n");
}

/* Must match kernel64.ld's .trp_code_area (pinned address, not
 * computed) and the trpc host-side compiler's -Ttext flag. All three
 * have to agree — see the comment in kernel64.ld for why this is a
 * literal constant rather than "wherever the linker put it", and why
 * it moved from 0x300000 to 0x500000. */
#define TRP_CODE_LOAD_ADDR   ((uintptr_t)0x500000ULL)
#define TRP_CODE_MAX_SIZE    ((uintptr_t)0x100000ULL) /* 1 MiB */

/* CHANGED: this used to memcpy() straight into ONE fixed PHYSICAL
 * address (0x500000) shared by the entire system - every process
 * that ever ran a "bin" TRP was reading and writing the exact same
 * underlying RAM at that address, just at different times (the
 * previous process's code/data still sitting there, plus no
 * isolation between whichever process happened to be "current" and
 * that memory). That was fine, in the narrow sense of "didn't crash",
 * ONLY because nothing before this milestone ever ran more than one
 * TRP-loaded process truly concurrently with working memory
 * isolation to violate - there was no isolation to violate yet.
 *
 * Now: TRP_CODE_LOAD_ADDR is still the fixed VIRTUAL address every
 * flat binary is linked for (trpc's -Ttext, kernel64.ld's
 * .trp_code_area comment - unchanged, still has to agree across all
 * three), but the PHYSICAL frames backing that virtual address are
 * now fresh, per-process, and mapped ONLY into the target process's
 * own PML4 (proc->page_table_root, a real address space as of
 * paging.c). Two processes can each be "running at 0x500000" at the
 * same time now without touching each other's memory at all - same
 * virtual address, different physical RAM, exactly the way every
 * real OS lets every process link its main executable at the same
 * conventional address.
 *
 * KNOWN LIMITATION, stated plainly rather than glossed over: a flat
 * binary carries no segment/section info (no split between code and
 * data/bss the way ELF has via PT_LOAD entries), so there's no way
 * for this loader to know which bytes are instructions and which are
 * data. The pages are mapped PAGING_WRITE (so a data/bss region
 * embedded in the blob is actually writable) which ALSO makes the
 * code pages writable, and there's no NX bit set on top of that
 * either - so this is not W^X and a memory-corruption bug in a TRP
 * binary could still overwrite its own code. This is a direct,
 * inherent consequence of TRPC's current flat-binary output format,
 * not something this loader change could fix by itself - the real
 * fix is TRPC (or a future version of it) emitting real ELF with
 * PT_LOAD segments carrying their own R/W/X flags per segment, which
 * this loader would then honor per-segment instead of mapping one
 * blob with one uniform permission set. Tracked, not silently
 * ignored - what ring-3 + real paging DOES already buy you even
 * without that: this code can no longer read or corrupt the KERNEL's
 * memory, or another process's memory. It can still, in principle,
 * corrupt itself. */
static int trp_exec_bin(const uint8_t *payload, uint32_t plen, pid_t pid)
{
    if (!plen) { trp_log("Empty binary payload."); return -1; }
    if (plen > TRP_CODE_MAX_SIZE) { trp_log("Binary payload too large for fixed load area."); return -1; }

    process_t *proc = process_get_by_pid(pid);
    if (!proc || !proc->page_table_root) {
        trp_log("No valid process/address space for this pid.");
        return -1;
    }

    uint32_t num_pages = (plen + 0xFFF) / 0x1000;
    for (uint32_t i = 0; i < num_pages; i++) {
        paddr_t frame = (paddr_t)(uintptr_t)mm_alloc_pages(1);
        if (!frame) { trp_log("OOM mapping TRP code pages."); return -1; }

        uint64_t va = TRP_CODE_LOAD_ADDR + (uint64_t)i * 0x1000ULL;
        /* Zero the frame first - the last page is very likely a
         * partial page (plen isn't page-aligned in general), and an
         * unzeroed physical frame fresh out of the allocator can
         * contain a previous process's leftover memory. Leaving that
         * tail unzeroed would leak whatever a PRIOR process (or the
         * kernel itself, if this frame was ever kernel-owned) last
         * had in that RAM into this process's bss-equivalent tail -
         * an information-disclosure bug into a process that has every
         * reason to expect fresh, zeroed memory there. */
        memset((void *)(uintptr_t)frame, 0, 0x1000);

        uint32_t copy_len = plen - i * 0x1000 < 0x1000 ? plen - i * 0x1000 : 0x1000;
        memcpy((void *)(uintptr_t)frame, payload + (size_t)i * 0x1000, copy_len);

        mm_map_page_in(proc->page_table_root, va, (uint64_t)frame,
                        PAGING_WRITE | PAGING_USER);
    }

    proc->context.rip = TRP_CODE_LOAD_ADDR;
    /* NOTE: does NOT flush/invalidate here even if this happens to be
     * called while `proc` is the currently-running process (it never
     * is, in every call site today - trp_load() runs during process
     * SETUP, before the process is ever scheduled in) - see
     * mm_map_page_in()'s own comment on why that's correct: the next
     * paging_switch_to() for this process's PML4 (scheduler_yield(),
     * on this process's very first run) flushes the entire TLB as an
     * unconditional side effect of loading %cr3, which is a superset
     * of what any number of per-page invlpg calls here would achieve. */
    trp_log("Binary payload mapped into process address space.");
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

    /* manifest_offset/len and payload_offset/len are uint32_t fields read
     * straight from the (attacker-controlled) file. `offset + len` can
     * wrap around 2^32 and pass a "> file_size" check while the actual
     * region is nowhere near valid - do the arithmetic widened to 64
     * bits, and reject an offset that's already out of range before even
     * adding the length. */
    uint64_t mo = hdr->manifest_offset, ml = hdr->manifest_len;
    if (mo > file_size || mo + ml > file_size) { trp_log("Manifest OOB."); kfree(buf); return -1; }

    if (hdr->payload_len) {
        uint64_t po = hdr->payload_offset, pl = hdr->payload_len;
        if (po > file_size || po + pl > file_size) { trp_log("Payload OOB."); kfree(buf); return -1; }
    }

    /* Parse the manifest exactly once, with the one real grammar
     * ("Execute at/:", "Window name/:", "Language/:", "Checksum/:", ...).
     * has_embedded_payload=1 tells the gate to skip the "does this file
     * already exist on the VFS" check for Execute at - this package
     * carries its own payload right here, it doesn't need one to
     * separately pre-exist elsewhere. */
    char entry_file[256];
    trp_manifest_t manifest;
    int gate = trp_manifest_run_gate2(
        (const char *)(buf + hdr->manifest_offset),
        hdr->manifest_len,
        &manifest,
        entry_file, sizeof(entry_file),
        0,
        hdr->payload_len ? 1 : 0
    );
    if (gate != 0) { kfree(buf); return -1; }

    if (!hdr->payload_len) { trp_log("No payload."); kfree(buf); return -1; }

    const uint8_t *payload = buf + hdr->payload_offset;
    uint32_t       plen    = hdr->payload_len;

    /* Executable rule: a package with an embedded binary payload is
     * runnable by default (that's the entire point of shipping a
     * payload) - `mark executable/` is an explicit override for the
     * asset-only / non-bin case, not a mandatory prerequisite that a
     * normal compiled package would otherwise silently fail without. */
    if (!plen && !manifest.mark_executable) {
        trp_log("Not executable - refusing."); kfree(buf); return -1;
    }

    if (manifest.checksum[0]) {
        if (!trp_manifest_verify_checksum(&manifest, payload, plen)) {
            trp_log("Checksum mismatch - payload does not match manifest Checksum/: - refusing.");
            kfree(buf);
            return -1;
        }
        trp_log("Checksum OK.");
    }

    const char *lang = manifest.language[0] ? manifest.language : "bin";
    int result = -1;

    if (strcmp(lang,"bin")==0)
        result = trp_exec_bin(payload, plen, pid);
    else if (strcmp(lang,"c")==0 || strcmp(lang,"py")==0)
        result = trp_exec_text(payload, plen, lang);
    else {
        serial_puts("[TRP] Unknown lang: "); serial_puts(lang); serial_puts("\n");
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

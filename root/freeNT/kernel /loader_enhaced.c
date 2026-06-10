// ==============================================================================
// LOADER_ENHANCED.C - Executable Format Router
// Routes .exe -> PE loader, .trp -> TRP loader, .elf -> ELF loader
// ==============================================================================

#include "loader.h"
#include "trp_loader.h"
#include "../include/process.h"
#include "../include/mm.h"
#include "../include/io.h"
#include "../include/string.h"
#include "../include/fs.h"

void serial_puts(const char *s);

// ---------------------------------------------------------------------------
// PE (.exe) loader — reads MZ/PE header, maps sections, sets entry point
// ---------------------------------------------------------------------------
int loader_load_exe(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc) return -1;

    inode_t st;
    if (fs_stat(filename, &st) != 0) { serial_puts("[PE] stat failed\n"); return -1; }

    uint32_t size = (uint32_t)st.size;
    uint8_t *buf  = (uint8_t *)kmalloc(size);
    if (!buf) { serial_puts("[PE] OOM\n"); return -1; }

    fd_t fd = fs_open(filename, O_RDONLY, 0);
    if (fd < 0) { serial_puts("[PE] open failed\n"); kfree(buf); return -1; }
    ssize_t r = fs_read(fd, buf, size);
    fs_close(fd);
    if ((uint32_t)r != size) { serial_puts("[PE] read failed\n"); kfree(buf); return -1; }

    // Validate MZ
    if ((buf[0] != 'M' || buf[1] != 'Z')) {
        serial_puts("[PE] not MZ\n"); kfree(buf); return -1;
    }

    uint32_t e_lfanew = *(uint32_t *)(buf + 0x3C);
    if (e_lfanew + 24 >= size) { serial_puts("[PE] bad e_lfanew\n"); kfree(buf); return -1; }

    uint8_t *coff         = buf + e_lfanew + 4;
    uint16_t num_sections = *(uint16_t *)(coff + 2);
    uint16_t opt_size     = *(uint16_t *)(coff + 16);
    uint8_t *opt          = coff + 20;
    uint32_t entry_rva    = *(uint32_t *)(opt + 16);
    uint8_t *sec_hdr      = opt + opt_size;

    // Find image size
    uint32_t image_size = 0;
    for (int i = 0; i < num_sections; i++) {
        uint8_t *sh    = sec_hdr + i * 40;
        uint32_t vaddr = *(uint32_t *)(sh + 12);
        uint32_t vsz   = *(uint32_t *)(sh + 8);
        if (vaddr + vsz > image_size) image_size = vaddr + vsz;
    }
    image_size = (image_size + 4095) & ~4095u;
    if (image_size == 0) image_size = 0x1000;

    uint8_t *image = (uint8_t *)kmalloc(image_size);
    if (!image) { serial_puts("[PE] OOM image\n"); kfree(buf); return -1; }
    memset(image, 0, image_size);

    for (int i = 0; i < num_sections; i++) {
        uint8_t *sh      = sec_hdr + i * 40;
        uint32_t raw_sz  = *(uint32_t *)(sh + 16);
        uint32_t raw_ptr = *(uint32_t *)(sh + 20);
        uint32_t vaddr   = *(uint32_t *)(sh + 12);
        if (raw_ptr + raw_sz <= size && vaddr + raw_sz <= image_size)
            memcpy(image + vaddr, buf + raw_ptr, raw_sz);
    }

    if (entry_rva >= image_size) {
        serial_puts("[PE] entry RVA OOB\n"); kfree(buf); kfree(image); return -1;
    }

    proc->context.rip = (uint64_t)(uintptr_t)(image + entry_rva);
    serial_puts("[PE] Loaded OK\n");
    kfree(buf);
    return 0;
}

// ---------------------------------------------------------------------------
// TRP (.trp) loader — delegates to trp_loader.c
// ---------------------------------------------------------------------------
int loader_load_trp(const char *filename, pid_t pid) {
    return trp_load(filename, pid);
}

// ---------------------------------------------------------------------------
// ELF loader stub
// ---------------------------------------------------------------------------
int loader_load_elf(const char *filename, pid_t pid) {
    (void)pid;
    serial_puts("[ELF] Loading "); serial_puts(filename); serial_puts(" (stub)\n");
    return -1;
}

int loader_apply_relocations(pid_t pid, relocation_entry_t *relocs, size_t count) {
    (void)pid; (void)relocs; (void)count; return 0;
}

int loader_init(void) { return 1; }

const char *loader_format_to_string(int fmt) {
    switch (fmt) {
        case EXEC_FORMAT_PE:  return "PE (.exe)";
        case EXEC_FORMAT_TRP: return "TRP (.trp)";
        case EXEC_FORMAT_ELF: return "ELF";
        default:              return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// loader_load_executable — detect format by extension and dispatch
// ---------------------------------------------------------------------------
int loader_load_executable(const char *filename, pid_t pid) {
    if (!filename) return -1;

    size_t len = strlen(filename);

    if (len > 4 && strcmp(filename + len - 4, ".exe") == 0) {
        serial_puts("[LOADER] -> PE loader\n");
        return loader_load_exe(filename, pid);
    }
    if (len > 4 && strcmp(filename + len - 4, ".trp") == 0) {
        serial_puts("[LOADER] -> TRP loader\n");
        return loader_load_trp(filename, pid);
    }
    if (len > 4 && strcmp(filename + len - 4, ".elf") == 0) {
        serial_puts("[LOADER] -> ELF loader\n");
        return loader_load_elf(filename, pid);
    }

    serial_puts("[LOADER] Unknown extension: "); serial_puts(filename); serial_puts("\n");
    return -1;
}
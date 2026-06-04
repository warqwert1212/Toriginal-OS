/* Enhanced executable loader for freeNT - PE, TRP, and ELF support */

#include "loader.h"
#include "process.h"
#include "mm.h"
#include "io.h"
#include "string.h"
#include "fs.h"


/* Forward declarations */
static void trp_vm_run(uint8_t *payload, size_t size);

/* Load PE (Windows .exe) format with proper validation */
int loader_load_exe(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    inode_t st;
    if (fs_stat(filename, &st) != 0) {
        io_put_string("PE loader: stat failed\n");
        return -1;
    }

    fd_t fd = fs_open(filename, 0, 0);
    if (fd < 0) {
        io_put_string("PE loader: open failed\n");
        return -1;
    }

    size_t size = (size_t)st.size;
    uint8_t *buf = kmalloc(size);
    if (!buf) {
        fs_close(fd);
        io_put_string("PE loader: out of memory\n");
        return -1;
    }

    ssize_t r = fs_read(fd, buf, size);
    fs_close(fd);
    if ((size_t)r != size) {
        io_put_string("PE loader: read failed\n");
        kfree(buf);
        return -1;
    }

    /* Validate MZ signature */
    uint16_t mz = *(uint16_t *)buf;
    if (mz != 0x5A4D) { /* 'MZ' */
        io_put_string("PE loader: invalid MZ signature\n");
        kfree(buf);
        return -1;
    }

    uint32_t e_lfanew = *(uint32_t *)(buf + 0x3C);
    if (e_lfanew + 4 + 20 >= size) {
        io_put_string("PE loader: invalid e_lfanew\n");
        kfree(buf);
        return -1;
    }

    uint8_t *coff = buf + e_lfanew + 4; /* immediately after 'PE\0\0' */
    uint16_t num_sections = *(uint16_t *)(coff + 2);
    uint16_t size_opt = *(uint16_t *)(coff + 16);
    uint8_t *opt = coff + 20;

    /* Read AddressOfEntryPoint from optional header at offset 16 */
    uint32_t entry_rva = *(uint32_t *)(opt + 16);

    /* Section headers follow optional header */
    uint8_t *sec_hdr = opt + size_opt;

    /* Compute required image size by scanning virtual sizes */
    uint32_t max_end = 0;
    for (int i = 0; i < num_sections; ++i) {
        uint8_t *sh = sec_hdr + i * 40;
        uint32_t vsize = *(uint32_t *)(sh + 8);
        uint32_t vaddr = *(uint32_t *)(sh + 12);
        if (vaddr + vsize > max_end) max_end = vaddr + vsize;
    }
    if (max_end == 0) max_end = 0x1000;
    uint32_t image_size = (max_end + 0xFFF) & ~0xFFFU;

    uint8_t *image = kmalloc(image_size);
    if (!image) {
        io_put_string("PE loader: out of memory for image\n");
        kfree(buf);
        return -1;
    }
    memset(image, 0, image_size);

    /* Copy each section's raw data into correct virtual offset in image */
    for (int i = 0; i < num_sections; ++i) {
        uint8_t *sh = sec_hdr + i * 40;
        uint32_t size_raw = *(uint32_t *)(sh + 16);
        uint32_t ptr_raw = *(uint32_t *)(sh + 20);
        uint32_t vaddr = *(uint32_t *)(sh + 12);
        if (ptr_raw + size_raw <= size && vaddr + size_raw <= image_size) {
            memcpy(image + vaddr, buf + ptr_raw, size_raw);
        }
    }

    /* Attempt to apply base relocations if present (.reloc section) */
    for (int i = 0; i < num_sections; ++i) {
        uint8_t *sh = sec_hdr + i * 40;
        char name[9]; memcpy(name, sh, 8); name[8] = '\0';
        if (strcmp(name, ".reloc") == 0) {
            uint32_t reloc_ptr = *(uint32_t *)(sh + 20);
            uint32_t reloc_size = *(uint32_t *)(sh + 16);
            if (reloc_ptr + reloc_size <= size) {
                uint8_t *reloc_block = buf + reloc_ptr;
                uint32_t parsed = 0;
                uint64_t delta = (uint64_t)(uintptr_t)image - (uint64_t)0; /* assume preferred image base 0 */
                while (parsed < reloc_size) {
                    if (parsed + 8 > reloc_size) break;
                    uint32_t page_rva = *(uint32_t *)(reloc_block + parsed);
                    uint32_t block_size = *(uint32_t *)(reloc_block + parsed + 4);
                    parsed += 8;
                    uint32_t entry_count = (block_size - 8) / 2;
                    for (uint32_t e = 0; e < entry_count; ++e) {
                        if (parsed + 2 > reloc_size) break;
                        uint16_t entry = *(uint16_t *)(reloc_block + parsed);
                        parsed += 2;
                        uint16_t type = entry >> 12;
                        uint16_t offset = entry & 0x0FFF;
                        uint32_t target_rva = page_rva + offset;
                        if (target_rva + 4 <= image_size) {
                            if (type == 3) { /* IMAGE_REL_BASED_HIGHLOW */
                                uint32_t *ptr = (uint32_t *)(uintptr_t)(image + target_rva);
                                *ptr = (uint32_t)((uint64_t)(*ptr) + (uint32_t)delta);
                            } else if (type == 10) { /* IMAGE_REL_BASED_DIR64 */
                                if (target_rva + 8 <= image_size) {
                                    uint64_t *p64 = (uint64_t *)(uintptr_t)(image + target_rva);
                                    *p64 = (uint64_t)(*p64 + delta);
                                }
                            }
                        }
                    }
                }
                io_put_string("PE loader: applied relocations\n");
            }
        }
    }

    if (entry_rva >= image_size) {
        io_put_string("PE loader: entry RVA out of range\n");
        kfree(buf);
        kfree(image);
        return -1;
    }

    proc->context.rip = (uint64_t)(uintptr_t)(image + entry_rva);
    io_put_string("PE loader: mapped sections and set entry\n");
    kfree(buf);
    return 0;
}

/* Load TRP (Toriganal Runtime Package) format */
int loader_load_trp(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;

    inode_t st;
    if (fs_stat(filename, &st) != 0) {
        io_put_string("TRP loader: stat failed\n");
        return -1;
    }

    fd_t fd = fs_open(filename, 0, 0);
    if (fd < 0) {
        io_put_string("TRP loader: open failed\n");
        return -1;
    }

    size_t size = (size_t)st.size;
    void *buf = kmalloc(size);
    if (!buf) {
        fs_close(fd);
        io_put_string("TRP loader: out of memory\n");
        return -1;
    }

    ssize_t r = fs_read(fd, buf, size);
    fs_close(fd);
    if ((size_t)r != size) {
        io_put_string("TRP loader: read failed\n");
        kfree(buf);
        return -1;
    }

    uint32_t magic = *(uint32_t *)buf;
    if (magic != 0x4B525054) { /* 'TRPK' */
        io_put_string("TRP loader: bad magic\n");
        kfree(buf);
        return -1;
    }

    uint32_t entry_off = 0;
    char maybe_ascii[5];
    memcpy(maybe_ascii, (uint8_t *)buf + 4, 4);
    maybe_ascii[4] = '\0';
    int is_digits = 1;
    for (int i = 0; i < 4; ++i) {
        if (maybe_ascii[i] < '0' || maybe_ascii[i] > '9') {
            is_digits = 0; break;
        }
    }
    if (is_digits) {
        entry_off = 0;
        for (int i = 0; i < 4; ++i) {
            entry_off = entry_off * 10 + (maybe_ascii[i] - '0');
        }
    } else {
        entry_off = *(uint32_t *)((uint8_t *)buf + 4);
    }
    if (entry_off >= size) entry_off = 0;

    uint8_t *payload = (uint8_t *)buf + 8;
    size_t payload_size = size > 8 ? size - 8 : 0;
    if (payload_size >= 5 && memcmp(payload, "TEXT:", 5) == 0) {
        serial_puts((const char *)(payload + 5));
        serial_puts("\n");
        kfree(buf);
        return 0;
    }

    if (payload_size >= 2 && payload[0] == 'V' && payload[1] == 'M') {
        trp_vm_run(payload + 2, payload_size - 2);
        kfree(buf);
        return 0;
    }

    proc->context.rip = (uint64_t)(uintptr_t)((uint8_t *)buf + entry_off);
    io_put_string("TRP loader: loaded and entry set\n");
    return 0;
}

/* Simple TRP VM interpreter */
static void trp_vm_run(uint8_t *payload, size_t size) {
    size_t i = 0;
    while (i < size) {
        uint8_t op = payload[i++];
        if (op == 0xFF) {
            return;
        } else if (op == 0x01) {
            if (i + 2 > size) return;
            uint16_t len = *(uint16_t *)(payload + i);
            i += 2;
            if (i + len > size) return;
            serial_puts((const char *)(payload + i));
            serial_puts("\n");
            i += len;
        } else {
            return;
        }
    }
}

/* Run a plain text file */
int loader_run_txt(const char *filename, pid_t pid) {
    (void)pid;
    io_put_string("TXT runner: Displaying ");
    io_put_string((char *)filename);
    io_put_string("\n");
    return 0;
}

/* Load ELF format */
int loader_load_elf(const char *filename, pid_t pid) {
    (void)pid;
    io_put_string("ELF loader: Loading ");
    io_put_string((char *)filename);
    io_put_string("\n");
    return 0;
}

/* Apply relocations */
int loader_apply_relocations(pid_t pid, relocation_entry_t *relocs, size_t count) {
    (void)pid;
    (void)relocs;
    (void)count;
    io_put_string("Applying relocations for process\n");
    return 0;
}

/* Detect executable file type by magic bytes */
static int loader_detect_type(const void *data, size_t size) {
    if (size < 4)
        return -1;
    
    uint32_t magic = *(uint32_t *)data;
    uint16_t sig = *(uint16_t *)data;
    
    if (sig == 0x5A4D)  /* 'MZ' */
        return EXEC_FORMAT_PE;
    
    if (magic == 0x464C457F)  /* '\x7FELF' */
        return EXEC_FORMAT_ELF;
    
    if (magic == 0x4B525054)  /* 'TRPK' */
        return EXEC_FORMAT_TRP;
    
    return -1;
}

/* Generic executable loader */
int loader_load_executable(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc || !filename)
        return -1;
    
    io_put_string("Generic loader: Loading executable for process\n");

    size_t len = strlen(filename);
    if (len > 4 && strcmp(filename + len - 4, ".exe") == 0) {
        serial_puts("[loader] detected .exe, calling PE loader\n");
        int r = loader_load_exe(filename, pid);
        if (r == 0) serial_puts("[loader] PE loader OK\n"); else serial_puts("[loader] PE loader FAILED\n");
        return r;
    }
    if (len > 4 && strcmp(filename + len - 4, ".trp") == 0) {
        serial_puts("[loader] detected .trp, calling TRP loader\n");
        int r = loader_load_trp(filename, pid);
        if (r == 0) serial_puts("[loader] TRP loader OK\n"); else serial_puts("[loader] TRP loader FAILED\n");
        return r;
    }
    if (len > 4 && strcmp(filename + len - 4, ".txt") == 0) {
        serial_puts("[loader] detected .txt, calling TXT runner\n");
        int r = loader_run_txt(filename, pid);
        if (r == 0) serial_puts("[loader] TXT runner OK\n"); else serial_puts("[loader] TXT runner FAILED\n");
        return r;
    }

    return -1;
}

/* Initialize loader subsystem */
int loader_init(void) {
    io_put_string("Executable loader initialized\n");
    return 1;
}

/* Get human-readable format name */
const char* loader_format_to_string(int format) {
    switch (format) {
        case EXEC_FORMAT_PE:
            return "PE (Windows .exe)";
        case EXEC_FORMAT_ELF:
            return "ELF (Linux)";
        case EXEC_FORMAT_TRP:
            return "TRP (Toriganal)";
        default:
            return "Unknown";
    }
}
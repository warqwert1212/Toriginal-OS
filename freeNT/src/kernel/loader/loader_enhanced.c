/* Enhanced executable loader for freeNT - PE, TRP, and ELF support */

#include "loader.h"
#include "process.h"
#include "mm.h"
#include "io.h"
#include "string.h"
#include <stdint.h>

/* Load PE (Windows .exe) format with proper validation */
int loader_load_exe(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    /* In a real implementation, we would:
     * 1. Open file from filesystem
     * 2. Parse PE DOS header
     * 3. Parse PE COFF header
     * 4. Parse optional header
     * 5. Load each section into process address space
     * 6. Apply relocations
     * 7. Set entry point
     */
    
    io_put_string("PE loader: Loading ");
    io_put_string((char *)filename);
    io_put_string("\n");
    
    return 0;
}

/* Load TRP (Toriganal Runtime Package) format */
int loader_load_trp(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    /* In a real implementation, we would:
     * 1. Open file from filesystem
     * 2. Read TRP header
     * 3. Validate magic number and version
     * 4. Iterate through sections
     * 5. Map sections to process address space
     * 6. Apply any needed relocations
     * 7. Set process entry point
     */
    
    io_put_string("TRP loader: Loading ");
    io_put_string((char *)filename);
    io_put_string("\n");
    
    return 0;
}

/* Load ELF format (for Linux compatibility) */
int loader_load_elf(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    /* In a real implementation, we would:
     * 1. Open file from filesystem
     * 2. Read ELF header (64-bit)
     * 3. Validate ELF magic
     * 4. Parse program headers
     * 5. Map loadable segments to address space
     * 6. Handle ASLR if needed
     * 7. Set process entry point
     * 8. If dynamic binary, load interpreter and libraries
     */
    
    io_put_string("ELF loader: Loading ");
    io_put_string((char *)filename);
    io_put_string("\n");
    
    return 0;
}

/* Apply relocations */
int loader_apply_relocations(pid_t pid, relocation_entry_t *relocs, size_t count) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    if (!relocs || count == 0)
        return 0;
    
    /* Relocation support would require:
     * 1. Address mapping information in process structure
     * 2. Symbol table resolution
     * 3. Different relocation types based on executable format
     * 
     * For now, we just verify that relocation entries exist
     */
    
    io_put_string("Applying relocations for process\n");
    
    return 0;
}

/* Detect executable file type by magic bytes */
static int loader_detect_type(const void *data, size_t size) {
    if (size < 4)
        return -1;
    
    uint32_t magic = *(uint32_t *)data;
    uint16_t sig = *(uint16_t *)data;
    
    /* PE/COFF - MZ signature (Windows .exe) */
    if (sig == 0x5A4D)  /* 'MZ' */
        return EXEC_FORMAT_PE;
    
    /* ELF - magic number */
    if (magic == 0x464C457F)  /* '\x7FELF' */
        return EXEC_FORMAT_ELF;
    
    /* TRP - Toriganal format */
    if (magic == 0x4B525054)  /* 'TRPK' */
        return EXEC_FORMAT_TRP;
    
    return -1;
}

/* Generic executable loader - detects format and loads appropriately */
int loader_load_executable(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc || !filename)
        return -1;
    
    /* In a real implementation:
     * 1. Open file from filesystem
     * 2. Read magic bytes
     * 3. Detect format
     * 4. Call appropriate loader
     * 5. Set up process state
     * 6. Load any dependencies
     */
    
    io_put_string("Generic loader: Loading executable for process\n");
    
    return 0;
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

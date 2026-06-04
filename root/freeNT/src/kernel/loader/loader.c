#include "loader.h"
#include "process.h"
#include "mm.h"
#include "io.h"
#include "string.h"

/* Load PE (Windows .exe) format */
int loader_load_exe(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    /* TODO: Read PE header from file */
    /* TODO: Parse PE sections */
    /* TODO: Map sections into process address space */
    /* TODO: Perform relocations */
    /* TODO: Set entry point */
    
    return 0;
}

/* Load TRP (Toriganal Runtime Package) format */
int loader_load_trp(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    /* TODO: Read TRP header from file */
    trp_header_t *trp_hdr = (trp_header_t *)kmalloc(sizeof(trp_header_t));
    if (!trp_hdr)
        return -1;
    
    /* Validate TRP magic */
    if (trp_hdr->magic != 0x4B525054)  /* 'TRPK' */
        return -1;
    
    /* TODO: Load sections */
    /* TODO: Perform relocations */
    /* TODO: Set entry point */
    
    kfree(trp_hdr);
    return 0;
}

/* Load ELF format (for Linux compatibility) */
int loader_load_elf(const char *filename, pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    /* TODO: Read ELF header */
    /* TODO: Parse program headers */
    /* TODO: Map segments into process space */
    /* TODO: Handle dynamic linking */
    
    return 0;
}

/* Apply relocations */
int loader_apply_relocations(pid_t pid, relocation_entry_t *relocs, size_t count) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    /* TODO: Apply relocations to process address space */
    
    return 0;
}

/* Helper: Detect file type by magic bytes */
static int loader_detect_type(const void *data, size_t size) {
    if (size < 4)
        return -1;
    
    uint32_t magic = *(uint32_t *)data;
    
    /* PE/COFF - MZ signature */
    if ((magic & 0xFFFF) == 0x5A4D)
        return 0;  /* .exe */
    
    /* ELF */
    if (magic == 0x464C457F)
        return 1;  /* ELF */
    
    /* TRP */
    if (magic == 0x4B525054)
        return 2;  /* .trp */
    
    return -1;
}

/* Generic executable loader */
int loader_load_executable(const char *filename, pid_t pid) {
    /* TODO: Open and read file header */
    /* TODO: Detect file type */
    /* TODO: Call appropriate loader */
    
    return 0;
}

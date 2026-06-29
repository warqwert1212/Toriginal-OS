#ifndef _KERNEL_LOADER_H
#define _KERNEL_LOADER_H

#include "types.h"

/* PE (Portable Executable) header - for .exe support */
typedef struct {
    uint32_t signature;
    uint16_t machine;
    uint16_t num_sections;
    uint32_t time_date_stamp;
    uint32_t ptr_to_symbol_table;
    uint32_t num_symbols;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
} pe_file_header_t;

typedef struct {
    uint16_t magic;
    uint8_t major_linker_version;
    uint8_t minor_linker_version;
    uint32_t size_of_code;
    uint32_t size_of_initialized_data;
    uint32_t size_of_uninitialized_data;
    uint32_t address_of_entry_point;
    uint32_t base_of_code;
    uint32_t base_of_data;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
} pe_optional_header_t;

typedef struct {
    char name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t ptr_to_raw_data;
    uint32_t ptr_to_relocations;
    uint32_t ptr_to_line_numbers;
    uint16_t num_relocations;
    uint16_t num_line_numbers;
    uint32_t characteristics;
} pe_section_header_t;

/* TRP (Toriganal Runtime Package) header - custom binary format */
typedef struct {
    uint32_t magic;         /* 'TRPK' */
    uint32_t version;
    uint32_t flags;
    uint32_t num_sections;
    uint64_t entry_point;
    uint32_t checksum;
} trp_header_t;

typedef struct {
    char name[32];
    uint32_t type;          /* 0=code, 1=data, 2=bss */
    uint32_t flags;
    uint64_t vaddr;
    uint32_t vsize;
    uint32_t foffset;
    uint32_t fsize;
} trp_section_t;

/* Loader functions */
int loader_load_exe(const char *filename, pid_t pid);
int loader_load_trp(const char *filename, pid_t pid);
int loader_load_elf(const char *filename, pid_t pid);
int loader_load_executable(const char *filename, pid_t pid);

/* Dynamic linking */
typedef struct {
    uint64_t addr;
    const char *symbol;
} relocation_entry_t;

/* Executable format types */
#define EXEC_FORMAT_PE  0    /* Windows PE .exe */
#define EXEC_FORMAT_TRP 1    /* Toriganal Runtime Package */
#define EXEC_FORMAT_ELF 2    /* Linux ELF */

/* Relocation types */
#define RELOC_ABSOLUTE  0
#define RELOC_RELATIVE  1
#define RELOC_SYMBOL    2

typedef struct {
    uint32_t offset;
    uint32_t type;
    uint64_t value;
} relocation_entry_extended_t;

int loader_apply_relocations(pid_t pid, relocation_entry_t *relocs, size_t count);
int loader_init(void);
const char* loader_format_to_string(int format);

#endif /* _KERNEL_LOADER_H */

# TRP (Toriganal Runtime Package) Format Specification

## Header Format

```c
typedef struct {
    uint32_t magic;         /* 'TRPK' = 0x4B525054 */
    uint32_t version;       /* Current version: 1 */
    uint32_t flags;         /* Execution flags */
    uint32_t num_sections;  /* Number of sections */
    uint64_t entry_point;   /* Entry point RIP */
    uint32_t checksum;      /* CRC32 checksum */
} trp_header_t;
```

## Section Header Format

```c
typedef struct {
    char name[32];          /* Section name */
    uint32_t type;          /* 0=code, 1=data, 2=bss, 3=debug */
    uint32_t flags;         /* Permission flags */
    uint64_t vaddr;         /* Virtual address */
    uint32_t vsize;         /* Virtual size */
    uint32_t foffset;       /* File offset */
    uint32_t fsize;         /* File size */
} trp_section_t;
```

## Flags

### Execution Flags
- `0x00000001`: Position Independent Code (PIC)
- `0x00000002`: Address Space Layout Randomization (ASLR)
- `0x00000004`: Data Execution Prevention (DEP)
- `0x00000008`: Stack Overflow Protection

### Section Flags
- `0x00000001`: Readable
- `0x00000002`: Writable
- `0x00000004`: Executable
- `0x00000008`: Allocate in memory

## File Structure

```
Offset  Size    Description
------  ----    -----------
0x00    0x14    TRP Header (20 bytes)
0x14    n*0x40  Section Headers (64 bytes each)
0x??    *       Section Data (aligned to 4KB)
```

## Minimal TRP Example

```
0x00:  4B 52 50 4B        Magic: 'TRPK'
0x04:  01 00 00 00        Version: 1
0x08:  00 00 00 00        Flags: 0
0x0C:  01 00 00 00        Num Sections: 1
0x10:  00 10 00 00 00 00 00 00  Entry Point: 0x1000
0x18:  00 00 00 00        Checksum: 0
0x1C:  (Section data follows)
```

## Creation Example

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int create_trp_executable(const char *output_file, 
                         const void *code, size_t code_size) {
    FILE *f = fopen(output_file, "wb");
    
    trp_header_t header = {
        .magic = 0x4B525054,  /* 'TRPK' */
        .version = 1,
        .flags = 0x04,         /* DEP enabled */
        .num_sections = 1,
        .entry_point = 0x1000,
        .checksum = 0
    };
    
    trp_section_t section = {
        .name = ".text",
        .type = 0,             /* Code */
        .flags = 0x0D,         /* R+W+X */
        .vaddr = 0x1000,
        .vsize = code_size,
        .foffset = sizeof(trp_header_t) + sizeof(trp_section_t),
        .fsize = code_size
    };
    
    fwrite(&header, 1, sizeof(header), f);
    fwrite(&section, 1, sizeof(section), f);
    fwrite(code, 1, code_size, f);
    
    fclose(f);
    return 0;
}
```

## Advantages Over PE

- **Smaller**: No Windows-specific structures
- **Faster**: Optimized for freeNT kernel
- **Simpler**: Straightforward section model
- **Direct**: Native x86-64 code without COFF overhead

## Loading Process

1. Read and validate header
2. Verify checksum
3. Read all section headers
4. Allocate memory for each section
5. Load section data from file
6. Apply relocations if needed
7. Jump to entry point

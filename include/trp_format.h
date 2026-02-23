#ifndef TRP_FORMAT_H
#define TRP_FORMAT_H

#include <stdint.h>

// TRP (Toriganal Runtime Program) File Format Header
struct TRP_Header {
    uint32_t magic;           // Magic number: 0x54524700 ("TRP\0")
    uint32_t version;         // Format version
    uint32_t entry_point;     // Entry point offset in the file
    uint32_t text_size;       // Size of code section
    uint32_t data_size;       // Size of data section
    uint32_t bss_size;        // Size of uninitialized data
    uint32_t total_size;      // Total program size
    uint32_t flags;           // Program flags
    uint32_t checksum;        // CRC32 checksum
};

// TRP Program Flags
#define TRP_FLAG_EXECUTABLE 0x01
#define TRP_FLAG_RELOCATABLE 0x02
#define TRP_FLAG_DEBUG 0x04

// TRP Magic number
#define TRP_MAGIC 0x54524700  // "TRP\0"

// Function pointer type for TRP programs
typedef int (*trp_main_t)(int argc, char** argv);

#endif // TRP_FORMAT_H
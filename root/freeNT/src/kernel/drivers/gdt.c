// ==============================================================================
// GDT.C - Long Mode Global Descriptor Table & Task State Segment Matrix
// ==============================================================================
#include <stdint.h>

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;      // Kernel Stack Pointer for Ring 3 transitions
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_tss_entry {
    struct gdt_entry common;
    uint32_t base_highest;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// GDT Matrix Layout: Null, Kernel Code, Kernel Data, User Code, User Data, TSS (16 bytes)
static struct {
    struct gdt_entry entries[5];
    struct gdt_tss_entry tss;
} __attribute__((packed)) gdt;

static struct gdt_ptr gp;
static struct tss_entry tss_inst;

extern void flush_gdt(uint64_t gdt_ptr_addr);

void init_gdt(uint64_t kernel_stack_top) {
    // 1. Null Descriptor
    gdt.entries[0] = (struct gdt_entry){0, 0, 0, 0, 0, 0};
    
    // 2. Kernel Code (0x08): Access 0x9A (Present, Ring 0, Executable, Read) | Granularity 0x20 (Long Mode)
    gdt.entries[1] = (struct gdt_entry){0, 0, 0, 0x9A, 0x20, 0};
    
    // 3. Kernel Data (0x10): Access 0x92 (Present, Ring 0, Writable)
    gdt.entries[2] = (struct gdt_entry){0, 0, 0, 0x92, 0x00, 0};
    
    // 4. User Code   (0x1B): Access 0xFA (Present, Ring 3, Executable, Read) | Granularity 0x20
    gdt.entries[3] = (struct gdt_entry){0, 0, 0, 0xFA, 0x20, 0};
    
    // 5. User Data   (0x23): Access 0xF2 (Present, Ring 3, Writable)
    gdt.entries[4] = (struct gdt_entry){0, 0, 0, 0xF2, 0x00, 0};

    // 6. Set up Task State Segment Instance
    tss_inst.rsp0 = kernel_stack_top;
    tss_inst.iomap_base = sizeof(struct tss_entry);

    // 7. Install TSS Descriptor into GDT (Takes slots 5 & 6)
    uint64_t tss_base = (uint64_t)&tss_inst;
    uint32_t tss_limit = sizeof(struct tss_entry) - 1;

    gdt.tss.common.limit_low   = tss_limit & 0xFFFF;
    gdt.tss.common.base_low    = tss_base & 0xFFFF;
    gdt.tss.common.base_middle = (tss_base >> 16) & 0xFF;
    gdt.tss.common.access      = 0x89; // Present, Ring 0, Available 64-bit TSS Descriptor
    gdt.tss.common.granularity = (tss_limit >> 16) & 0x0F;
    gdt.tss.common.base_high   = (tss_base >> 24) & 0xFF;
    gdt.tss.base_highest       = (tss_base >> 32) & 0xFFFFFFFF;
    gdt.tss.reserved           = 0;

    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint64_t)&gdt;

    flush_gdt((uint64_t)&gp);
}
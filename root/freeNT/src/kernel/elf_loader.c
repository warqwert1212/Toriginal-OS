// ==============================================================================
// ELF_LOADER.C - Native 64-Bit System Binary Executable Loader Module
// ==============================================================================
#include <stdint.h>
#include <stddef.h>

#define ELF_MAGIC 0x464C457F // "\x7FELF" Layout Identification Mark
#define PAGE_SIZE 4096

// Flag configuration markers for memory mapping flags matching your kernel permissions
#define PERM_PRESENT  (1 << 0)
#define PERM_WRITABLE (1 << 1)
#define PERM_USER     (1 << 2)

struct elf_header {
    uint32_t magic;
    uint8_t  e_ident[12];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;         // Program execution entry point (User space RIP)
    uint64_t phoff;         // Program headers offset position
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;         // Quantified structure count of program map segments
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf_program_header {
    uint32_t type;          // Loadable section match indicator = 1
    uint32_t flags;
    uint64_t offset;        // Position block where bytes reside inside structural binary arrays
    uint64_t vaddr;         // Virtual space layout destination matching execution specifications
    uint64_t paddr;
    uint64_t filesz;        // Physical byte sizes written inside storage structures
    uint64_t memsz;         // Total deployment area footprint width size inside RAM boundaries
    uint64_t align;
} __attribute__((packed));

// External declarations mapping exactly to your underlying kernel architectures
extern void jump_to_user(uint64_t user_entry, uint64_t user_stack);
extern void* mm_alloc_pages(int count);
extern void mm_map_page(uint64_t virtual_addr, uint64_t physical_addr, uint32_t flags);

int load_and_execute_user_elf(uint8_t* raw_elf_data) {
    struct elf_header* header = (struct elf_header*)raw_elf_data;

    // 1. Authenticate file integrity signatures
    if (header->magic != ELF_MAGIC) {
        return -1; // File type mismatch fault code
    }

    // 2. Loop through execution headers to unpack segments into virtual addresses
    struct elf_program_header* ph = (struct elf_program_header*)(raw_elf_data + header->phoff);
    for (int i = 0; i < header->phnum; i++) {
        if (ph[i].type == 1) { // Segment Type 1 = PT_LOAD (Loadable Data or Code)
            
            // Calculate size requirement parameters
            size_t page_count = (ph[i].memsz + PAGE_SIZE - 1) / PAGE_SIZE;
            uint64_t virt_dest = ph[i].vaddr;

            // Map isolated segments into the Virtual Memory space with User permissions
            for (size_t p = 0; p < page_count; p++) {
                // Request 1 single page frame from your memory manager
                void* phys_frame = mm_alloc_pages(1);
                
                // Map with Present, Writable, and User access bits (0x07)
                uint32_t map_flags = PERM_PRESENT | PERM_WRITABLE | PERM_USER;
                mm_map_page(virt_dest + (p * PAGE_SIZE), (uint64_t)phys_frame, map_flags);
            }

            // Copy raw segment bytes from storage payloads directly into newly mapped memory segments
            uint8_t* dest_ptr = (uint8_t*)virt_dest;
            uint8_t* src_ptr  = raw_elf_data + ph[i].offset;
            for (uint64_t b = 0; b < ph[i].filesz; b++) {
                dest_ptr[b] = src_ptr[b];
            }
            // Zero out remaining memory spaces if memsz is larger than filesz (e.g., .bss section)
            for (uint64_t b = ph[i].filesz; b < ph[i].memsz; b++) {
                dest_ptr[b] = 0;
            }
        }
    }

    // 3. Allocate an isolated, dedicated User Space execution Stack virtual window
    uint64_t user_stack_virtual_limit = 0x00007FFFF0000000;
    void* stack_frame_phys = mm_alloc_pages(1);
    
    uint32_t stack_flags = PERM_PRESENT | PERM_WRITABLE | PERM_USER;
    mm_map_page(user_stack_virtual_limit, (uint64_t)stack_frame_phys, stack_flags);

    // 4. Fire execution sequence boundaries over privilege borders into Ring 3
    jump_to_user(header->entry, user_stack_virtual_limit + PAGE_SIZE - 8);

    return 0; // Execution handover complete
}
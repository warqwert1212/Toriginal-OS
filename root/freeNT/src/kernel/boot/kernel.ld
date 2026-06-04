/* Linker script for freeNT x86-64 kernel and toriginal OS */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    /* Bootloader section in low memory for GRUB multiboot (1MB physical) */
    . = 0x100000;
    
    .text.boot : ALIGN(0x1000) {
        *(.text.boot)
        *(.text.boot.*)
        *(.bss.boot)
        *(.data.boot)
    }
    
    /* Save the current physical load address directly following the boot section */
    _kernel_physical_start = .;

    /* Kernel virtual address offset (Higher Half Canonical -2GB Mapping Offset) */
    . = 0xFFFFFFFF80000000 + 0x100000;
    
    .text ALIGN(0x1000) : AT(_kernel_physical_start) {
        *(.text)
        *(.text.*)
    }
    
    .rodata ALIGN(0x1000) : AT(LOADADDR(.text) + SIZEOF(.text)) {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data ALIGN(0x1000) : AT(LOADADDR(.rodata) + SIZEOF(.rodata)) {
        *(.data)
        *(.data.*)
    }
    
    .bss ALIGN(0x1000) : AT(LOADADDR(.data) + SIZEOF(.data)) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}

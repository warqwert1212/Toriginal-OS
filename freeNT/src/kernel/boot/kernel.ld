/* Linker script for freeNT x86-64 kernel */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    /* Bootloader section in low memory for GRUB multiboot */
    . = 0x100000;
    
    .text.boot : ALIGN(0x1000) {
        *(.text.boot)
        *(.text.boot.*)
        *(.bss.boot)
    }
    
    /* Kernel in high virtual memory */
    . = 0xFFFF800000100000;
    
    .text ALIGN(0x1000) : {
        *(.text)
        *(.text.*)
    }
    
    .rodata ALIGN(0x1000) : {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data ALIGN(0x1000) : {
        *(.data)
        *(.data.*)
    }
    
    .bss ALIGN(0x1000) : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}

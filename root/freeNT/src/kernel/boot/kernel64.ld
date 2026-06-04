/* 64-bit kernel linker script for toriginal OS/the freeNT kernel*/
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    /* Boot section (must be loaded at 1MB by GRUB multiboot) */
    . = 0x100000;
    
    .text.boot : {
        *(.text.boot)
    }
    
    .data.boot : {
        *(.data.boot)
    }
    
    .bss.boot : {
        *(.bss.boot)
    }
    
    /* Kernel in high virtual memory - 64-bit canonical -2GB addressing alignment */
    . = 0xFFFFFFFF80000000 + 0x200000;  
    
    /* Explicitly lock the text section to load at physical 2MB */
    .text ALIGN(4096) : AT(0x200000) {
        *(.text)
        *(.text.*)
    }
    
    .rodata ALIGN(4096) : AT(LOADADDR(.text) + SIZEOF(.text)) {
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(8);
        /* String tables */
        *(.rodata.str1.1)
        *(.rodata.str1.8)
        . = ALIGN(8);
    }
    
    .data ALIGN(4096) : AT(LOADADDR(.rodata) + SIZEOF(.rodata)) {
        *(.data)
        *(.data.*)
    }
    
    .bss ALIGN(4096) : AT(LOADADDR(.data) + SIZEOF(.data)) {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }
    
    /* Discard sections */
    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.note.gnu.property)
        *(.gnu_debuglink)
        *(.gnu_debugaltlink)
        *(.eh_frame)
    }
}
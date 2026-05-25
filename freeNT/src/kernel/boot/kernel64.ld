/* 64-bit kernel linker script - freeNT */
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
    
    /* Kernel in high virtual memory - 64-bit addressing */
    . = 0xFFFF800000000000 + 0x200000;  /* 0x200000 (2MB) offset for kernel */
    
    .text ALIGN(4096) : AT(0x200000) {
        *(.text)
        *(.text.*)
    }
    
    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(8);
        /* String tables */
        *(.rodata.str1.1)
        *(.rodata.str1.8)
        . = ALIGN(8);
    }
    
    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    }
    
    .bss ALIGN(4096) : {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }
    
    /* Discard sections */
    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.gnu_debuglink)
        *(.gnu_debugaltlink)
    }
}

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

SECTIONS
{
    . = 0x200000;

    /* Multiboot header MUST be first, within first 8KB of file */
    .multiboot . : {
        KEEP(*(.multiboot))
    }

    /* _start must come right after the multiboot header */
    .text . : {
        __text_start = .;
        *(.text*)
        __text_end = .;
    }

    .rodata ALIGN(4K) : {
        __rodata_start = .;
        *(.rodata*)
        __rodata_end = .;
    }

    .data ALIGN(4K) : {
        __data_start = .;
        *(.data*)
        __data_end = .;
    }

    .bss ALIGN(4K) : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }
}

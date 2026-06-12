 /* =========================================================================
 * FreeNT Linker Script
 * Multiboot2 x86_64
 * ========================================================================= */

ENTRY(_start)

SECTIONS
{
    /* GRUB loads kernel at 2MB */

    . = 0x200000;

    .text ALIGN(4K) :
    {
        KEEP(*(.multiboot))
        *(.text*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata*)
    }

    .data ALIGN(4K) :
    {
        *(.data*)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }
}
/* =========================================================================
 * linker.ld — FreeNT / Toriginal OS kernel linker script
 *
 * This is an ALIAS of kernel64.ld / linker.ld for use by build.sh.
 * Place this file at root/freeNT/kernel/boot/linker.ld
 * (it is already there in your repo; this copy is provided for reference).
 * ========================================================================= */

ENTRY(_start)

SECTIONS
{
    /* GRUB loads the kernel at the 2 MiB mark in physical memory */
    . = 0x200000;

    .text ALIGN(4K) :
    {
        KEEP(*(.multiboot))   /* Multiboot2 header must be within first 8 KiB */
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

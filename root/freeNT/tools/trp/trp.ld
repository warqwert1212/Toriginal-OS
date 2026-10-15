/* Flat-binary TRP layout: the loader copies the payload to 0x500000 and
 * jumps to its first byte, so _start must come first. .bss is folded into
 * the image (the loader only maps payload bytes). */
ENTRY(_start)
SECTIONS
{
    . = 0x500000;
    .text   : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

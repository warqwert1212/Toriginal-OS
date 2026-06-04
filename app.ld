ENTRY(main_explorer_executable)

SECTIONS
{
    /* This must match the address your kernel's binary loader jumps to */
    . = 0x00400000; 

    .text : {
        *(.text)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }
}
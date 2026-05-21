ENTRY(kernel_main)

SECTIONS
{
    . = 0xFFFF800000000000;
    
    .boot ALIGN(0x1000) :
    {
        *(.multiboot*)
        *(.text.boot)
    }
    
    .text ALIGN(0x1000) :
    {
        *(.text)
        *(.text.*)
    }
    
    .rodata ALIGN(0x1000) :
    {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data ALIGN(0x1000) :
    {
        *(.data)
        *(.data.*)
    }
    
    .bss ALIGN(0x1000) :
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
}

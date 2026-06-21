ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text ALIGN(4096) : {
        *(.text*)
        *(.rodata*)
    }

    .data ALIGN(4096) : {
        *(.data*)
    }

    .bss ALIGN(4096) : {
        *(.bss*)
        *(COMMON)
    }
}

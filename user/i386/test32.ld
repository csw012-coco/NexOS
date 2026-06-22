ENTRY(_start)

SECTIONS
{
    . = 0x40020000;

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

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}

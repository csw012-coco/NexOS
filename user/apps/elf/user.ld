ENTRY(_start)

SECTIONS
{
    . = 0x0000008000000000;

    .text : ALIGN(4096) {
        *(.text*)
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(.bss*)
        *(COMMON)
    }
}

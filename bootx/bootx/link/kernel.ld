ENTRY(kernel_main)

SECTIONS
{
    . = 0x00100000;

    .text : ALIGN(4096) {
        *(.text*)
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }
}

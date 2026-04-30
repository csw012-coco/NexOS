ENTRY(kernel_main64)

SECTIONS
{
    . = 0xffffffff80000000;

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

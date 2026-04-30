ENTRY(kernel_main64)

PHDRS
{
    kernel_text PT_LOAD FLAGS(5);
    kernel_data PT_LOAD FLAGS(6);
    user_text PT_LOAD FLAGS(5);
    user_data PT_LOAD FLAGS(6);
}

SECTIONS
{
    __kernel_start = 0xffffffff80000000;
    . = __kernel_start;

    __kernel_text_start = .;
    .text : ALIGN(4096) {
        *(.text*)
        *(.rodata*)
        *(.eh_frame*)
    } :kernel_text
    __kernel_text_end = ALIGN(4096);

    __kernel_data_start = .;
    .data : ALIGN(4096) {
        *(.data*)
    } :kernel_data
    __kernel_data_end = ALIGN(4096);

    .userelf (NOLOAD) : ALIGN(4096) {
        __userelf_start = .;
        . = . + 0x20000;
        __userelf_end = .;
        __userelf_stack_bottom = .;
        . = . + 0x10000;
        __userelf_stack_top = .;
    } :user_data

    __kernel_bss_start = .;
    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :kernel_data
    __kernel_bss_end = ALIGN(4096);

    __kernel_end = .;
}

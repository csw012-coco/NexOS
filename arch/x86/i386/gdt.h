#pragma once

typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

enum {
    I386_GDT_KERNEL_CODE = 0x08,
    I386_GDT_KERNEL_DATA = 0x10,
    I386_GDT_USER_CODE = 0x1b,
    I386_GDT_USER_DATA = 0x23,
    I386_GDT_TSS = 0x28
};

struct i386_gdtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void i386_gdt_init(void);
void i386_gdt_read(struct i386_gdtr *gdtr);
uint32_t i386_gdt_base(void);
uint16_t i386_gdt_limit(void);
uint32_t i386_gdt_kernel_stack_top(void);

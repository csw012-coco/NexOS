#pragma once

#include <stdint.h>

enum {
    GDT64_KERNEL_CODE = 0x08,
    GDT64_KERNEL_DATA = 0x10,
    GDT64_USER_CODE = 0x1b,
    GDT64_USER_DATA = 0x23,
    GDT64_TSS = 0x28
};

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

void gdt64_init(void);
uint16_t gdt64_kernel_code_selector(void);
uint16_t gdt64_kernel_data_selector(void);
uint16_t gdt64_user_code_selector(void);
uint16_t gdt64_user_data_selector(void);
uint64_t gdt64_kernel_rsp0(void);
void gdt64_set_kernel_rsp0(uint64_t rsp0);

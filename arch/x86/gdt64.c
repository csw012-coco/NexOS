#include "arch/x86/gdt64.h"

struct gdt64_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt64_tss_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt64_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct gdt64_table {
    struct gdt64_entry null_entry;
    struct gdt64_entry kernel_code;
    struct gdt64_entry kernel_data;
    struct gdt64_entry user_code;
    struct gdt64_entry user_data;
    struct gdt64_tss_entry tss;
} __attribute__((packed));

static struct gdt64_table gdt64;
static struct tss64 kernel_tss;
static uint8_t kernel_rsp0_stack[16384];

extern void gdt64_flush(const struct gdt64_ptr *ptr);
extern void tss64_flush(uint16_t selector);

static void gdt64_set_entry(struct gdt64_entry *entry,
                            uint32_t base,
                            uint32_t limit,
                            uint8_t access,
                            uint8_t granularity) {
    entry->limit_low = (uint16_t)(limit & 0xffffu);
    entry->base_low = (uint16_t)(base & 0xffffu);
    entry->base_mid = (uint8_t)((base >> 16) & 0xffu);
    entry->access = access;
    entry->granularity = (uint8_t)(((limit >> 16) & 0x0fu) | (granularity & 0xf0u));
    entry->base_high = (uint8_t)((base >> 24) & 0xffu);
}

static void gdt64_set_tss(struct gdt64_tss_entry *entry, uint64_t base, uint32_t limit) {
    entry->limit_low = (uint16_t)(limit & 0xffffu);
    entry->base_low = (uint16_t)(base & 0xffffu);
    entry->base_mid = (uint8_t)((base >> 16) & 0xffu);
    entry->access = 0x89;
    entry->granularity = (uint8_t)((limit >> 16) & 0x0fu);
    entry->base_high = (uint8_t)((base >> 24) & 0xffu);
    entry->base_upper = (uint32_t)(base >> 32);
    entry->reserved = 0;
}

void gdt64_init(void) {
    struct gdt64_ptr ptr;

    gdt64_set_entry(&gdt64.null_entry, 0, 0, 0, 0);
    gdt64_set_entry(&gdt64.kernel_code, 0, 0, 0x9a, 0x20);
    gdt64_set_entry(&gdt64.kernel_data, 0, 0, 0x92, 0x00);
    gdt64_set_entry(&gdt64.user_code, 0, 0, 0xfa, 0x20);
    gdt64_set_entry(&gdt64.user_data, 0, 0, 0xf2, 0x00);

    kernel_tss.reserved0 = 0;
    kernel_tss.rsp0 = (uint64_t)(uintptr_t)&kernel_rsp0_stack[sizeof(kernel_rsp0_stack)];
    kernel_tss.rsp1 = 0;
    kernel_tss.rsp2 = 0;
    kernel_tss.reserved1 = 0;
    kernel_tss.ist1 = 0;
    kernel_tss.ist2 = 0;
    kernel_tss.ist3 = 0;
    kernel_tss.ist4 = 0;
    kernel_tss.ist5 = 0;
    kernel_tss.ist6 = 0;
    kernel_tss.ist7 = 0;
    kernel_tss.reserved2 = 0;
    kernel_tss.reserved3 = 0;
    kernel_tss.iomap_base = sizeof(kernel_tss);

    gdt64_set_tss(&gdt64.tss, (uint64_t)(uintptr_t)&kernel_tss, sizeof(kernel_tss) - 1u);

    ptr.limit = (uint16_t)(sizeof(gdt64) - 1u);
    ptr.base = (uint64_t)(uintptr_t)&gdt64;

    gdt64_flush(&ptr);
    tss64_flush(GDT64_TSS);
}

uint16_t gdt64_kernel_code_selector(void) {
    return GDT64_KERNEL_CODE;
}

uint16_t gdt64_kernel_data_selector(void) {
    return GDT64_KERNEL_DATA;
}

uint16_t gdt64_user_code_selector(void) {
    return GDT64_USER_CODE;
}

uint16_t gdt64_user_data_selector(void) {
    return GDT64_USER_DATA;
}

uint64_t gdt64_kernel_rsp0(void) {
    return kernel_tss.rsp0;
}

void gdt64_set_kernel_rsp0(uint64_t rsp0) {
    kernel_tss.rsp0 = rsp0;
}

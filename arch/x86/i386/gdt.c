#include "gdt.h"

struct i386_gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed));

struct i386_tss {
    uint32_t previous_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static struct i386_gdt_entry gdt[6];
static struct i386_gdtr gdtr;
static struct i386_tss tss;
static unsigned char kernel_ring0_stack[16384] __attribute__((aligned(16)));

extern void i386_gdt_load(const struct i386_gdtr *gdtr);
extern void i386_tss_load(uint16_t selector);

static void gdt_set(uint32_t index,
                    uint32_t base,
                    uint32_t limit,
                    unsigned char access,
                    unsigned char granularity) {
    gdt[index].base_low = (uint16_t)(base & 0xffffu);
    gdt[index].base_middle = (unsigned char)((base >> 16) & 0xffu);
    gdt[index].base_high = (unsigned char)((base >> 24) & 0xffu);
    gdt[index].limit_low = (uint16_t)(limit & 0xffffu);
    gdt[index].granularity =
        (unsigned char)(((limit >> 16) & 0x0fu) | (granularity & 0xf0u));
    gdt[index].access = access;
}

void i386_gdt_init(void) {
    gdtr.limit = (uint16_t)(sizeof(gdt) - 1u);
    gdtr.base = (uint32_t)gdt;

    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xffffffffu, 0x9au, 0xcfu);
    gdt_set(2, 0, 0xffffffffu, 0x92u, 0xcfu);
    gdt_set(3, 0, 0xffffffffu, 0xfau, 0xcfu);
    gdt_set(4, 0, 0xffffffffu, 0xf2u, 0xcfu);
    for (uint32_t i = 0; i < sizeof(tss); i++) {
        ((unsigned char *)&tss)[i] = 0;
    }
    tss.esp0 = (uint32_t)&kernel_ring0_stack[sizeof(kernel_ring0_stack)];
    tss.ss0 = I386_GDT_KERNEL_DATA;
    tss.iomap_base = sizeof(tss);
    gdt_set(5,
            (uint32_t)&tss,
            sizeof(tss) - 1u,
            0x89u,
            0x00u);

    i386_gdt_load(&gdtr);
    i386_tss_load(I386_GDT_TSS);
}

void i386_gdt_read(struct i386_gdtr *current) {
    __asm__ volatile("sgdt %0" : "=m"(*current));
}

uint32_t i386_gdt_base(void) {
    return gdtr.base;
}

uint16_t i386_gdt_limit(void) {
    return gdtr.limit;
}

uint32_t i386_gdt_kernel_stack_top(void) {
    return tss.esp0;
}

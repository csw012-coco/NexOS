#include "gdt.h"

struct gdt_entry gdt[3];
struct gdt_ptr gp;

extern void gdt_flush(uint32_t);

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].base_high = (base >> 24) & 0xFF;

    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].gran = (limit >> 16) & 0x0F;

    gdt[i].gran |= gran & 0xF0;
    gdt[i].access = access;
}

void gdt_init(void) {
    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint32_t)&gdt;

    gdt_set(0, 0, 0, 0, 0);                // null
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // code
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // data

    gdt_flush((uint32_t)&gp);
}
#include "scheduler_internal.h"

static uint32_t i386_read_cr0(void) {
    uint32_t value;

    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static void i386_write_cr0(uint32_t value) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(value) : "memory");
}

void i386_scheduler_fpu_enable(void) {
    enum {
        CR0_MP = 1u << 1,
        CR0_EM = 1u << 2,
        CR0_TS = 1u << 3,
        CR0_NE = 1u << 5
    };
    uint32_t cr0 = i386_read_cr0();

    cr0 &= ~(CR0_EM | CR0_TS);
    cr0 |= CR0_MP | CR0_NE;
    i386_write_cr0(cr0);
    __asm__ volatile("fninit" : : : "memory");
}

void i386_scheduler_fpu_save(struct i386_fpu_state *state) {
    if (state == 0) {
        return;
    }
    __asm__ volatile("fnsave %0\n\tfwait" : "=m"(*state) : : "memory");
}

void i386_scheduler_fpu_restore(const struct i386_fpu_state *state,
                                uint32_t valid) {
    if (state == 0 || valid == 0u) {
        __asm__ volatile("fninit" : : : "memory");
        return;
    }
    __asm__ volatile("frstor %0" : : "m"(*state) : "memory");
}

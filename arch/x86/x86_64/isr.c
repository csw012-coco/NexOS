#include <stdint.h>
#include "idt.h"

extern void isr0();
extern void isr1();
// ... isr31까지 필요

void isr_install(void) {
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
}

void isr_handler() {
    // 디버그용
}
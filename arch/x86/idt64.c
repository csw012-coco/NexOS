#include "arch/x86/idt64.h"
#include "arch/x86/io.h"

static struct idt_entry64 idt[256];

static inline void io_wait(void) {
    outb(0x80, 0);
}

void idt64_set_gate(uint8_t vector, void (*handler)(void), uint8_t type_attr) {
    uint64_t address = (uint64_t)handler;

    idt[vector].offset_low = (uint16_t)(address & 0xffff);
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_mid = (uint16_t)((address >> 16) & 0xffff);
    idt[vector].offset_high = (uint32_t)((address >> 32) & 0xffffffffu);
    idt[vector].zero = 0;
}

void idt64_init(void) {
    struct idt_ptr64 idtr;

    for (uint32_t i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].zero = 0;
    }

    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base = (uint64_t)&idt[0];

    __asm__ __volatile__("lidt %0" : : "m"(idtr));
}

void pic_remap(void) {
    uint8_t master_mask = inb(0x21);
    uint8_t slave_mask = inb(0xa1);

    outb(0x20, 0x11);
    io_wait();
    outb(0xa0, 0x11);
    io_wait();

    outb(0x21, 0x20);
    io_wait();
    outb(0xa1, 0x28);
    io_wait();

    outb(0x21, 0x04);
    io_wait();
    outb(0xa1, 0x02);
    io_wait();

    outb(0x21, 0x01);
    io_wait();
    outb(0xa1, 0x01);
    io_wait();

    outb(0x21, master_mask);
    outb(0xa1, slave_mask);
}

void pic_set_mask(uint8_t irq_line, int masked) {
    uint16_t port;
    uint8_t value;
    uint8_t bit;

    if (irq_line < 8) {
        port = 0x21;
        bit = irq_line;
    } else {
        port = 0xa1;
        bit = irq_line - 8;
    }

    value = inb(port);
    if (masked) {
        value = (uint8_t)(value | (1u << bit));
    } else {
        value = (uint8_t)(value & ~(1u << bit));
    }
    outb(port, value);
}

void pic_send_eoi(uint8_t irq_line) {
    if (irq_line >= 8) {
        outb(0xa0, 0x20);
    }
    outb(0x20, 0x20);
}

void pit_init(uint32_t frequency_hz) {
    uint32_t divisor;

    if (frequency_hz == 0) {
        frequency_hz = 100;
    }

    divisor = 1193182u / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    }

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xff));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xff));
}

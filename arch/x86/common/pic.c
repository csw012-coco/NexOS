#include "arch/x86/common/io.h"
#include "pic.h"

enum {
    PIC_MASTER_COMMAND = 0x20,
    PIC_MASTER_DATA = 0x21,
    PIC_SLAVE_COMMAND = 0xa0,
    PIC_SLAVE_DATA = 0xa1,
    PIC_EOI = 0x20,
    PIC_ICW1_INIT = 0x10,
    PIC_ICW1_ICW4 = 0x01,
    PIC_ICW4_8086 = 0x01,
    PIT_CHANNEL0 = 0x40,
    PIT_COMMAND = 0x43,
    PIT_INPUT_HZ = 1193182
};

static void io_wait(void) {
    outb(0x80, 0);
}

void i386_pic_init(void) {
    outb(PIC_MASTER_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();
    outb(PIC_SLAVE_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();

    outb(PIC_MASTER_DATA, I386_PIC_MASTER_OFFSET);
    io_wait();
    outb(PIC_SLAVE_DATA, I386_PIC_SLAVE_OFFSET);
    io_wait();

    outb(PIC_MASTER_DATA, 0x04);
    io_wait();
    outb(PIC_SLAVE_DATA, 0x02);
    io_wait();

    outb(PIC_MASTER_DATA, PIC_ICW4_8086);
    io_wait();
    outb(PIC_SLAVE_DATA, PIC_ICW4_8086);
    io_wait();

    outb(PIC_MASTER_DATA, 0xff);
    outb(PIC_SLAVE_DATA, 0xff);
}

void i386_pic_set_mask(uint8_t irq, int masked) {
    uint16_t port;
    uint8_t bit;
    uint8_t value;

    if (irq >= I386_PIC_IRQ_COUNT) {
        return;
    }
    if (irq < 8u) {
        port = PIC_MASTER_DATA;
        bit = irq;
    } else {
        port = PIC_SLAVE_DATA;
        bit = (uint8_t)(irq - 8u);
    }

    value = inb(port);
    if (masked) {
        value = (uint8_t)(value | (uint8_t)(1u << bit));
    } else {
        value = (uint8_t)(value & (uint8_t)~(1u << bit));
    }
    outb(port, value);
}

void i386_pic_send_eoi(uint8_t irq) {
    if (irq >= 8u) {
        outb(PIC_SLAVE_COMMAND, PIC_EOI);
    }
    outb(PIC_MASTER_COMMAND, PIC_EOI);
}

void i386_pit_init(uint32_t frequency_hz) {
    uint32_t divisor;

    if (frequency_hz == 0u) {
        frequency_hz = 100u;
    }
    divisor = PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0u) {
        divisor = 1u;
    } else if (divisor > 0xffffu) {
        divisor = 0xffffu;
    }

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xffu));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xffu));
}

uint8_t i386_pic_master_mask(void) {
    return inb(PIC_MASTER_DATA);
}

uint8_t i386_pic_slave_mask(void) {
    return inb(PIC_SLAVE_DATA);
}

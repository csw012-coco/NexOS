#pragma once

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;

enum {
    I386_PIC_MASTER_OFFSET = 0x20,
    I386_PIC_SLAVE_OFFSET = 0x28,
    I386_PIC_IRQ_COUNT = 16
};

void i386_pic_init(void);
void i386_pic_set_mask(uint8_t irq, int masked);
void i386_pic_send_eoi(uint8_t irq);
void i386_pit_init(uint32_t frequency_hz);
uint8_t i386_pic_master_mask(void);
uint8_t i386_pic_slave_mask(void);

#pragma once

#include <stdint.h>

struct idt_entry64 {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void idt64_init(void);
void idt64_set_gate(uint8_t vector, void (*handler)(void), uint8_t type_attr);
void pic_remap(void);
void pic_set_mask(uint8_t irq_line, int masked);
void pic_send_eoi(uint8_t irq_line);
void pit_init(uint32_t frequency_hz);

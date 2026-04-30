#include "hal/x86/platform.h"

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq2_stub(void);
extern void irq3_stub(void);
extern void irq4_stub(void);
extern void irq5_stub(void);
extern void irq6_stub(void);
extern void irq7_stub(void);
extern void irq8_stub(void);
extern void irq9_stub(void);
extern void irq10_stub(void);
extern void irq11_stub(void);
extern void irq12_stub(void);
extern void irq13_stub(void);
extern void irq14_stub(void);
extern void irq15_stub(void);
extern void divide_error_stub(void);
extern void invalid_opcode_stub(void);

void hal_x86_platform_init_impl(const struct hal_interrupt_handlers *handlers) {
    static void (*const irq_stubs[16])(void) = {
        irq0_stub, irq1_stub, irq2_stub, irq3_stub,
        irq4_stub, irq5_stub, irq6_stub, irq7_stub,
        irq8_stub, irq9_stub, irq10_stub, irq11_stub,
        irq12_stub, irq13_stub, irq14_stub, irq15_stub
    };

    gdt64_init();
    idt64_init();

    if (handlers != 0) {
        idt64_set_gate(0, handlers->divide_error, 0x8e);
        idt64_set_gate(6, handlers->invalid_opcode, 0x8e);
        idt64_set_gate(8, handlers->double_fault, 0x8e);
        idt64_set_gate(13, handlers->general_protection_fault, 0x8e);
        idt64_set_gate(14, handlers->page_fault, 0x8e);
        idt64_set_gate(0x40, handlers->syscall, 0xef);
    }
    for (uint8_t irq = 0; irq < 16; irq++) {
        idt64_set_gate((uint8_t)(32u + irq), irq_stubs[irq], 0x8e);
    }

    pic_remap();
    pic_set_mask(0, 0);
    pic_set_mask(1, 0);
    for (uint8_t irq = 2; irq < 16; irq++) {
        pic_set_mask(irq, 1);
    }
}

void hal_x86_timer_init_impl(uint32_t pit_hz) {
    pit_init(pit_hz);
}

void hal_x86_irq_ack_impl(uint8_t irq) {
    pic_send_eoi(irq);
}

void hal_x86_irq_set_mask_impl(uint8_t irq, int masked) {
    pic_set_mask(irq, masked);
}

uint8_t hal_x86_keyboard_read_scancode_impl(void) {
    return inb(0x60);
}

uint8_t hal_x86_io_in8_impl(uint16_t port) {
    return inb(port);
}

uint16_t hal_x86_io_in16_impl(uint16_t port) {
    return inw(port);
}

void hal_x86_io_out8_impl(uint16_t port, uint8_t value) {
    outb(port, value);
}

void hal_x86_io_out16_impl(uint16_t port, uint16_t value) {
    outw(port, value);
}

uint64_t hal_x86_kernel_stack_top_impl(void) {
    return gdt64_kernel_rsp0();
}

void hal_x86_set_kernel_stack_top_impl(uint64_t rsp0) {
    gdt64_set_kernel_rsp0(rsp0);
}

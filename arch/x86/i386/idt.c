#include "gdt.h"
#include "idt.h"

struct i386_idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t offset_high;
} __attribute__((packed));

static struct i386_idt_gate idt[256];
static struct i386_idtr idtr;

extern void i386_irq0(void);
extern void i386_irq1(void);
extern void i386_irq2(void);
extern void i386_irq3(void);
extern void i386_irq4(void);
extern void i386_irq5(void);
extern void i386_irq6(void);
extern void i386_irq7(void);
extern void i386_irq8(void);
extern void i386_irq9(void);
extern void i386_irq10(void);
extern void i386_irq11(void);
extern void i386_irq12(void);
extern void i386_irq13(void);
extern void i386_irq14(void);
extern void i386_irq15(void);

static void idt_set_gate(uint8_t vector, uint32_t handler, uint16_t selector, uint8_t flags) {
    idt[vector].offset_low = (uint16_t)(handler & 0xffffu);
    idt[vector].selector = selector;
    idt[vector].zero = 0;
    idt[vector].flags = flags;
    idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xffffu);
}

void i386_idt_init(void) {
    static void (*const irq_handlers[16])(void) = {
        i386_irq0, i386_irq1, i386_irq2, i386_irq3,
        i386_irq4, i386_irq5, i386_irq6, i386_irq7,
        i386_irq8, i386_irq9, i386_irq10, i386_irq11,
        i386_irq12, i386_irq13, i386_irq14, i386_irq15
    };

    for (uint32_t i = 0; i < 256u; i++) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
    }

    idt_set_gate(I386_IDT_VECTOR_BREAKPOINT,
                 (uint32_t)i386_isr3,
                 I386_GDT_KERNEL_CODE,
                 I386_IDT_GATE_INTERRUPT32);
    idt_set_gate(I386_IDT_VECTOR_PAGE_FAULT,
                 (uint32_t)i386_isr14,
                 I386_GDT_KERNEL_CODE,
                 I386_IDT_GATE_INTERRUPT32);
    for (uint32_t irq = 0; irq < 16u; irq++) {
        idt_set_gate((uint8_t)(I386_IDT_VECTOR_IRQ_BASE + irq),
                     (uint32_t)irq_handlers[irq],
                     I386_GDT_KERNEL_CODE,
                     I386_IDT_GATE_INTERRUPT32);
    }
    idt_set_gate(I386_IDT_VECTOR_SYSCALL,
                 (uint32_t)i386_syscall_stub,
                 I386_GDT_KERNEL_CODE,
                 I386_IDT_GATE_USER_INTERRUPT32);

    idtr.limit = (uint16_t)(sizeof(idt) - 1u);
    idtr.base = (uint32_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

void i386_idt_read(struct i386_idtr *current) {
    __asm__ volatile("sidt %0" : "=m"(*current));
}

uint32_t i386_idt_base(void) {
    return idtr.base;
}

uint16_t i386_idt_limit(void) {
    return idtr.limit;
}

int i386_idt_gate_matches(uint8_t vector, uint32_t handler, uint16_t selector, uint8_t flags) {
    uint32_t installed_handler =
        (uint32_t)idt[vector].offset_low | ((uint32_t)idt[vector].offset_high << 16);

    return installed_handler == handler &&
           idt[vector].selector == selector &&
           idt[vector].zero == 0 &&
           idt[vector].flags == flags;
}

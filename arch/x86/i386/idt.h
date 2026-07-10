#pragma once

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

enum {
    I386_IDT_VECTOR_BREAKPOINT = 3,
    I386_IDT_VECTOR_PAGE_FAULT = 14,
    I386_IDT_VECTOR_IRQ_BASE = 0x20,
    I386_IDT_VECTOR_SYSCALL = 0x40,
    I386_IDT_GATE_INTERRUPT32 = 0x8e,
    I386_IDT_GATE_USER_INTERRUPT32 = 0xee
};

struct i386_exception_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t saved_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;
    uint32_t user_ss;
} __attribute__((packed));

struct i386_syscall_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t saved_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;
    uint32_t user_ss;
} __attribute__((packed));

struct i386_irq_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t saved_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;
    uint32_t user_ss;
} __attribute__((packed));

struct i386_idtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void i386_idt_init(void);
void i386_idt_read(struct i386_idtr *idtr);
uint32_t i386_idt_base(void);
uint16_t i386_idt_limit(void);
int i386_idt_gate_matches(uint8_t vector, uint32_t handler, uint16_t selector, uint8_t flags);

void i386_isr3(void);
void i386_isr14(void);
void i386_syscall_stub(void);
void i386_irq0(void);
void i386_irq15(void);
void i386_trigger_page_fault(void);
int i386_usermode_enter(uint32_t entry, uint32_t stack_top);
int i386_usermode_enter_context(struct i386_irq_frame *frame);
extern char i386_page_fault_test_fault[];
extern char i386_page_fault_test_recovery[];

#include "context.h"
#include "gdt.h"
#include "idt.h"
#include "kernel/public/proc/context.h"

static struct i386_irq_frame i386_resume_frame;

static void i386_context_from_frame(struct process_context *context,
                                    const struct i386_irq_frame *frame) {
    if (context == 0 || frame == 0) {
        return;
    }
    process_context_reset(context);
    context->registers[PROCESS_CONTEXT_RETURN] = frame->eax;
    context->registers[PROCESS_CONTEXT_ARG0] = frame->ebx;
    context->registers[PROCESS_CONTEXT_ARG1] = frame->ecx;
    context->registers[PROCESS_CONTEXT_ARG2] = frame->edx;
    context->registers[PROCESS_CONTEXT_ARG3] = frame->esi;
    context->registers[PROCESS_CONTEXT_GENERAL0] = frame->edi;
    context->registers[PROCESS_CONTEXT_GENERAL1] = frame->ebp;
    context->registers[PROCESS_CONTEXT_STACK_SNAPSHOT] = frame->saved_esp;
    context->instruction_pointer = frame->eip;
    context->stack_pointer = frame->user_esp;
    context->flags = frame->eflags;
    context->code_selector = (uint16_t)frame->cs;
    context->stack_selector = (uint16_t)frame->user_ss;
    context->user_mode = (frame->cs & 3u) == 3u;
}

void i386_context_init_user(struct process_context *context,
                            uint32_t entry,
                            uint32_t stack,
                            uint32_t first_argument) {
    process_context_reset(context);
    context->registers[PROCESS_CONTEXT_ARG0] = first_argument;
    context->instruction_pointer = entry;
    context->stack_pointer = stack;
    context->flags = 0x202u;
    context->code_selector = I386_GDT_USER_CODE;
    context->stack_selector = I386_GDT_USER_DATA;
    context->user_mode = 1u;
}

void i386_context_from_irq(struct process_context *context,
                           const struct i386_irq_frame *frame) {
    i386_context_from_frame(context, frame);
}

void i386_context_from_syscall(struct process_context *context,
                               const struct i386_syscall_frame *frame) {
    i386_context_from_frame(context,
                            (const struct i386_irq_frame *)frame);
}

struct i386_irq_frame *i386_context_to_irq(
    const struct process_context *context) {
    if (context == 0) {
        return 0;
    }
    i386_resume_frame.eax =
        (uint32_t)context->registers[PROCESS_CONTEXT_RETURN];
    i386_resume_frame.ebx =
        (uint32_t)context->registers[PROCESS_CONTEXT_ARG0];
    i386_resume_frame.ecx =
        (uint32_t)context->registers[PROCESS_CONTEXT_ARG1];
    i386_resume_frame.edx =
        (uint32_t)context->registers[PROCESS_CONTEXT_ARG2];
    i386_resume_frame.esi =
        (uint32_t)context->registers[PROCESS_CONTEXT_ARG3];
    i386_resume_frame.edi =
        (uint32_t)context->registers[PROCESS_CONTEXT_GENERAL0];
    i386_resume_frame.ebp =
        (uint32_t)context->registers[PROCESS_CONTEXT_GENERAL1];
    i386_resume_frame.saved_esp =
        (uint32_t)context->registers[PROCESS_CONTEXT_STACK_SNAPSHOT];
    i386_resume_frame.eip = (uint32_t)context->instruction_pointer;
    i386_resume_frame.cs = context->code_selector;
    i386_resume_frame.eflags = (uint32_t)context->flags;
    i386_resume_frame.user_esp = (uint32_t)context->stack_pointer;
    i386_resume_frame.user_ss = context->stack_selector;
    return &i386_resume_frame;
}

uint32_t i386_context_task_result(const struct process_context *context) {
    return context != 0
        ? (uint32_t)context->registers[PROCESS_CONTEXT_ARG2]
        : 0u;
}

int i386_context_enter(const struct process_context *context) {
    struct i386_irq_frame *frame = i386_context_to_irq(context);

    return frame != 0 ? i386_usermode_enter_context(frame) : 0;
}

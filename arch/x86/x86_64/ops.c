#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/sys/syscall.h"

static void x86_64_halt(void) {
    __asm__ volatile("hlt");
}

static void x86_64_wait_for_interrupt(void) {
    __asm__ volatile("sti; hlt" : : : "memory");
}

static uint64_t x86_64_current_address_space(void) {
    return vmm_current_root();
}

static void x86_64_switch_address_space(uint64_t root) {
    vmm_switch_root(root);
}

static void x86_64_syscall_decode(const void *arch_frame,
                                  struct kernel_syscall_request *out) {
    const struct syscall_frame *frame = (const struct syscall_frame *)arch_frame;

    if (frame == 0 || out == 0) {
        return;
    }
    out->number = (uint32_t)frame->rax;
    out->user_bits = 64u;
    out->args[0] = frame->rbx;
    out->args[1] = frame->rcx;
    out->args[2] = frame->rdx;
    out->args[3] = frame->rsi;
    out->args[4] = frame->rdi;
    out->args[5] = frame->rbp;
    out->instruction_pointer = frame->instruction_pointer;
    out->stack_pointer = frame->stack_pointer;
}

static void x86_64_syscall_set_return(void *arch_frame, uintptr_t value) {
    struct syscall_frame *frame = (struct syscall_frame *)arch_frame;

    if (frame != 0) {
        frame->rax = (uint64_t)value;
    }
}

static int x86_64_copy_from_user(void *dst, uintptr_t src, uint32_t size) {
    return vmm_copy_from_user(dst, (uint64_t)src, size);
}

static int x86_64_copy_to_user(uintptr_t dst, const void *src, uint32_t size) {
    return vmm_copy_to_user((uint64_t)dst, src, size);
}

static int x86_64_copy_user_cstr(char *dst, uintptr_t src, uint32_t size) {
    return vmm_copy_user_cstr(dst, (uint64_t)src, size);
}

const struct arch_ops arch_x86_64_ops = {
    .name = "x86_64",
    .word_bits = 64u,
    .halt = x86_64_halt,
    .wait_for_interrupt = x86_64_wait_for_interrupt,
    .current_address_space = x86_64_current_address_space,
    .switch_address_space = x86_64_switch_address_space,
    .syscall_decode = x86_64_syscall_decode,
    .syscall_set_return = x86_64_syscall_set_return,
    .copy_from_user = x86_64_copy_from_user,
    .copy_to_user = x86_64_copy_to_user,
    .copy_user_cstr = x86_64_copy_user_cstr,
};

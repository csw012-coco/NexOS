#include "arch/x86/i386/idt.h"
#include "arch/x86/i386/paging.h"
#include "kernel/public/arch/arch_ops.h"

static void x86_32_halt(void) {
    __asm__ volatile("hlt");
}

static void x86_32_wait_for_interrupt(void) {
    __asm__ volatile("sti; hlt" : : : "memory");
}

static uint64_t x86_32_current_address_space(void) {
    return i386_paging_root();
}

static void x86_32_switch_address_space(uint64_t root) {
    i386_paging_switch((uint32_t)root);
}

static void x86_32_syscall_decode(const void *arch_frame,
                                  struct kernel_syscall_request *out) {
    const struct i386_syscall_frame *frame =
        (const struct i386_syscall_frame *)arch_frame;

    if (frame == 0 || out == 0) {
        return;
    }
    out->number = frame->eax;
    out->user_bits = 32u;
    out->args[0] = frame->ebx;
    out->args[1] = frame->ecx;
    out->args[2] = frame->edx;
    out->args[3] = frame->esi;
    out->args[4] = frame->edi;
    out->args[5] = frame->ebp;
    out->instruction_pointer = frame->eip;
    out->stack_pointer = frame->user_esp;
}

static void x86_32_syscall_set_return(void *arch_frame, uintptr_t value) {
    struct i386_syscall_frame *frame = (struct i386_syscall_frame *)arch_frame;

    if (frame != 0) {
        frame->eax = (uint32_t)value;
    }
}

static int x86_32_user_buffer_valid(uint32_t address,
                                    uint32_t size,
                                    int writable) {
    uint32_t end;
    uint32_t page;
    uint32_t user_root;
    uint32_t kernel_root;
    int valid = 1;

    if (size == 0u ||
        address < I386_PAGING_IDENTITY_LIMIT ||
        address >= 0xc0000000u ||
        address + size < address ||
        address + size > 0xc0000000u) {
        return 0;
    }
    user_root = i386_paging_root();
    kernel_root = i386_paging_kernel_root();
    if (user_root == 0u || kernel_root == 0u || user_root == kernel_root) {
        return 0;
    }
    end = address + size - 1u;
    page = address & ~(I386_PAGE_SIZE - 1u);
    i386_paging_switch(kernel_root);
    for (;;) {
        if (!i386_paging_user_accessible_in(user_root, page, writable)) {
            valid = 0;
            break;
        }
        if (page >= (end & ~(I386_PAGE_SIZE - 1u))) {
            break;
        }
        page += I386_PAGE_SIZE;
    }
    i386_paging_switch(user_root);
    return valid;
}

static int x86_32_copy_from_user(void *dst, uintptr_t src, uint32_t size) {
    const uint8_t *user = (const uint8_t *)src;
    uint8_t *out = (uint8_t *)dst;

    if (dst == 0 || size == 0u ||
        !x86_32_user_buffer_valid((uint32_t)src, size, 0)) {
        return 0;
    }
    for (uint32_t i = 0u; i < size; i++) {
        out[i] = user[i];
    }
    return 1;
}

static int x86_32_copy_to_user(uintptr_t dst, const void *src, uint32_t size) {
    uint8_t *user = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;

    if (src == 0 || size == 0u ||
        !x86_32_user_buffer_valid((uint32_t)dst, size, 1)) {
        return 0;
    }
    for (uint32_t i = 0u; i < size; i++) {
        user[i] = in[i];
    }
    return 1;
}

static int x86_32_copy_user_cstr(char *dst, uintptr_t src, uint32_t size) {
    const char *user = (const char *)src;

    if (dst == 0 || size == 0u) {
        return 0;
    }
    for (uint32_t i = 0u; i + 1u < size; i++) {
        if (!x86_32_user_buffer_valid((uint32_t)src + i, 1u, 0)) {
            return 0;
        }
        dst[i] = user[i];
        if (dst[i] == '\0') {
            return 1;
        }
    }
    dst[size - 1u] = '\0';
    return 0;
}

const struct arch_ops arch_x86_32_ops = {
    .name = "i386",
    .word_bits = 32u,
    .halt = x86_32_halt,
    .wait_for_interrupt = x86_32_wait_for_interrupt,
    .current_address_space = x86_32_current_address_space,
    .switch_address_space = x86_32_switch_address_space,
    .syscall_decode = x86_32_syscall_decode,
    .syscall_set_return = x86_32_syscall_set_return,
    .copy_from_user = x86_32_copy_from_user,
    .copy_to_user = x86_32_copy_to_user,
    .copy_user_cstr = x86_32_copy_user_cstr,
};

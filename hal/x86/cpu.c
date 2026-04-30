#include "hal/x86/platform.h"

void hal_x86_cpu_cli_impl(void) {
    __asm__ __volatile__("cli");
}

void hal_x86_cpu_sti_impl(void) {
    __asm__ __volatile__("sti");
}

void hal_x86_cpu_halt_impl(void) {
    __asm__ __volatile__("hlt");
}

uint64_t hal_x86_cpu_current_sp_impl(void) {
    uint64_t rsp;

    __asm__ __volatile__("mov %%rsp, %0" : "=r"(rsp));
    return rsp;
}

uint64_t hal_x86_cpu_read_tsc_impl(void) {
    uint32_t lo;
    uint32_t hi;

    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void hal_x86_cpu_cpuid_impl(uint32_t leaf,
                            uint32_t subleaf,
                            uint32_t *eax,
                            uint32_t *ebx,
                            uint32_t *ecx,
                            uint32_t *edx) {
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;

    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(leaf), "c"(subleaf));
    if (eax != 0) {
        *eax = a;
    }
    if (ebx != 0) {
        *ebx = b;
    }
    if (ecx != 0) {
        *ecx = c;
    }
    if (edx != 0) {
        *edx = d;
    }
}

void hal_x86_usermode_enter_impl(uint64_t entry, uint64_t user_stack) {
    usermode_enter(entry, user_stack);
}

void hal_x86_usermode_resume_impl(const struct syscall_frame *frame) {
    usermode_resume_frame(frame);
}

#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/sys/syscall_native_request_core.h"
#include "kernel/internal/core/runtime_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/core/tty.h"

struct tty *g_syscall_tty;
volatile uint32_t *g_syscall_ticks;
struct vfs *g_syscall_vfs;
const struct bootx_boot_info *g_syscall_boot_info;
const struct bootx_memmap_entry *g_syscall_memmap;
uint32_t g_syscall_memmap_count;

uint8_t g_syscall_copy_buffer[SYSCALL_COPY_CHUNK] __attribute__((aligned(sizeof(uint32_t))));
char g_syscall_path_buffer[SYSCALL_PATH_MAX + 1];
char g_syscall_path_buffer2[SYSCALL_PATH_MAX + 1];
char g_syscall_name_buffer[NOS_TTY_LINE_MAX + 1];
struct syscall_trace g_last_syscall_trace;

static void syscall_decode_frame64(const struct syscall_frame *frame,
                                   struct kernel_syscall_request *request) {
    if (frame == 0 || request == 0) {
        return;
    }
    request->number = (uint32_t)frame->rax;
    request->user_bits = 64u;
    request->args[0] = frame->rbx;
    request->args[1] = frame->rcx;
    request->args[2] = frame->rdx;
    request->args[3] = frame->rsi;
    request->args[4] = frame->rdi;
    request->args[5] = frame->rbp;
    request->instruction_pointer = frame->rip;
    request->stack_pointer = frame->rsp;
}

void syscall_init(struct tty *tty,
                  volatile uint32_t *timer_ticks,
                  struct vfs *vfs,
                  const struct bootx_boot_info *boot_info,
                  const struct bootx_memmap_entry *memmap,
                  uint32_t memmap_count) {
    g_syscall_tty = tty;
    g_syscall_ticks = timer_ticks;
    g_syscall_vfs = vfs;
    g_syscall_boot_info = boot_info;
    g_syscall_memmap = memmap;
    g_syscall_memmap_count = memmap_count;
}

uint64_t syscall_dispatch(struct syscall_frame *frame) {
    struct kernel_syscall_request request = {0};
    struct kernel_syscall_result result;
    const struct process *trace_proc = process_current();

#define SYSCALL_RETURN(value) do { \
        uint64_t syscall_result__ = (uint64_t)(value); \
        g_last_syscall_trace.result = syscall_result__; \
        g_last_syscall_trace.returned = syscall_result__ != SYSCALL_EXIT_TO_KERNEL; \
        kernel_runtime_display_service_pending(); \
        return syscall_result__; \
    } while (0)

    syscall_decode_frame64(frame, &request);
    g_last_syscall_trace.valid = 1u;
    g_last_syscall_trace.number = request.number;
    g_last_syscall_trace.arg0 = kernel_syscall_arg_u64(&request, 0);
    g_last_syscall_trace.arg1 = kernel_syscall_arg_u64(&request, 1);
    g_last_syscall_trace.arg2 = kernel_syscall_arg_u64(&request, 2);
    g_last_syscall_trace.arg3 = kernel_syscall_arg_u64(&request, 3);
    g_last_syscall_trace.rip = request.instruction_pointer;
    g_last_syscall_trace.rsp = request.stack_pointer;
    g_last_syscall_trace.result = 0;
    g_last_syscall_trace.returned = 0u;
    g_last_syscall_trace.pid = trace_proc != 0 ? trace_proc->pid : 0u;

    if (syscall_native_dispatch_request(&request, frame, &result)) {
        SYSCALL_RETURN(result.value);
    }

    SYSCALL_RETURN(0);

#undef SYSCALL_RETURN
}

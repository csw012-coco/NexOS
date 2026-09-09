#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/public/sys/syscall.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/internal/sys/syscall_native_request_core.h"
#include "kernel/internal/core/runtime_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/core/tty.h"
#include "hal/hal.h"

volatile uint32_t *g_syscall_ticks;
struct vfs *g_syscall_vfs;
const struct bootx_boot_info *g_syscall_boot_info;

struct syscall_trace g_last_syscall_trace;

static uint64_t syscall_result_value_for_action(const struct kernel_syscall_result *result) {
    if (result == 0) {
        return 0;
    }
    switch (result->action) {
        case SYSCALL_RESULT_YIELD:
        case SYSCALL_RESULT_EXIT:
        case SYSCALL_RESULT_EXEC:
        case SYSCALL_RESULT_WAIT:
        case SYSCALL_RESULT_SLEEP:
        case SYSCALL_RESULT_IO_WAIT:
            return SYSCALL_EXIT_TO_KERNEL;
        case SYSCALL_RESULT_RETURN:
        default:
            return result->value;
    }
}

void syscall_init(struct tty *tty,
                  volatile uint32_t *timer_ticks,
                  struct vfs *vfs,
                  const struct bootx_boot_info *boot_info,
                  const struct bootx_memmap_entry *memmap,
                  uint32_t memmap_count) {
    (void)tty;
    g_syscall_ticks = timer_ticks;
    g_syscall_vfs = vfs;
    g_syscall_boot_info = boot_info;
    syscall_common_request_core_query_state_init(vfs,
                                                 boot_info,
                                                 memmap,
                                                 memmap_count);
}

uint64_t syscall_dispatch(struct syscall_frame *frame) {
    struct kernel_syscall_request request = {0};
    struct kernel_syscall_result result = {0};
    const struct process *trace_proc = process_current();

#define SYSCALL_RETURN(value) do { \
        uint64_t syscall_result__ = (uint64_t)(value); \
        g_last_syscall_trace.result = syscall_result__; \
        g_last_syscall_trace.returned = syscall_result__ != SYSCALL_EXIT_TO_KERNEL; \
        kernel_runtime_display_service_pending(); \
        return syscall_result__; \
    } while (0)

    hal_syscall_decode_request(frame, &request);
    g_last_syscall_trace.valid = 1u;
    g_last_syscall_trace.number = request.number;
    g_last_syscall_trace.arg0 = kernel_syscall_arg_u64(&request, 0);
    g_last_syscall_trace.arg1 = kernel_syscall_arg_u64(&request, 1);
    g_last_syscall_trace.arg2 = kernel_syscall_arg_u64(&request, 2);
    g_last_syscall_trace.arg3 = kernel_syscall_arg_u64(&request, 3);
    g_last_syscall_trace.instruction_pointer = request.instruction_pointer;
    g_last_syscall_trace.stack_pointer = request.stack_pointer;
    g_last_syscall_trace.result = 0;
    g_last_syscall_trace.returned = 0u;
    g_last_syscall_trace.pid = trace_proc != 0 ? trace_proc->pid : 0u;

    if (syscall_native_dispatch_request(&request, frame, &result)) {
        SYSCALL_RETURN(syscall_result_value_for_action(&result));
    }

    SYSCALL_RETURN(0);

#undef SYSCALL_RETURN
}

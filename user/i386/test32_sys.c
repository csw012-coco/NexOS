#include "test32_sys.h"

__attribute__((noinline)) static uint32_t test32_stack_grow_depth(
    uint32_t depth,
    volatile uint32_t *sink) {
    volatile uint8_t scratch[1024];

    scratch[0] = (uint8_t)depth;
    scratch[sizeof(scratch) - 1u] = (uint8_t)(depth ^ 0xa5u);
    *sink += scratch[0] + scratch[sizeof(scratch) - 1u];
    if (depth == 0u) {
        return *sink;
    }
    return test32_stack_grow_depth(depth - 1u, sink);
}

int test32_stack_grow_case(void) {
    volatile uint32_t stack_sink = 0u;

    if (test32_stack_grow_depth(8u, &stack_sink) == 0u) {
        return 154;
    }
    if (puts("[test32] libc32 page-fault stack grow OK") == EOF) {
        return 155;
    }
    return 0;
}

int test32_syscall_helper_case(void) {
    char input_ch;

    if (write_str("[test32] write_str OK\n") != 22u ||
        write_err_str("[test32] write_err_str OK\n") != 26u ||
        read_char_nonblock(&input_ch) != 0u) {
        return 44;
    }
    if (puts("[test32] libc32 syscall helper wrappers OK") == EOF) {
        return 45;
    }
    return 0;
}

int test32_ticks_yield_sleep_case(void) {
    (void)ticks();
    yield();
    (void)ticks();
    sleep(1u);
    (void)ticks();
    if (puts("[test32] libc32 ticks/yield/sleep OK") == EOF) {
        return 15;
    }
    return 0;
}

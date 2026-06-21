#include <stdint.h>
#include <stdarg.h>

static uint8_t early_io_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t early_io_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint8_t hal_io_in8(uint16_t port) {
    return early_io_in8(port);
}

uint16_t hal_io_in16(uint16_t port) {
    return early_io_in16(port);
}

void hal_io_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void hal_io_out16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

__attribute__((weak))
int kprint_is_ready(void) {
    return 0;
}

__attribute__((weak))
void kprint(const char *format, ...) {
    (void)format;
}

/*
 * The normal kernel provides scheduler timestamps, profiling and block
 * events. Early architecture bring-up links the real block layer before
 * those services exist, so these hooks deliberately degrade to no-ops.
 */
__attribute__((weak))
uint32_t sched_current_ticks(void) {
    return 0u;
}

__attribute__((weak))
uint32_t kernel_profile_register(const char *name) {
    (void)name;
    return 0u;
}

__attribute__((weak))
uint64_t kernel_profile_clock(void) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

__attribute__((weak))
void kernel_profile_record(uint32_t handle, uint64_t cycles, uint64_t units) {
    (void)handle;
    (void)cycles;
    (void)units;
}

#pragma once

/**
 * kprint.h: Kernel Print (like Linux printk)
 * 
 * Provides printf-style logging for kernel code.
 * Usage: kprint("message\n");
 *        kprint("Value: %d\n", 42);
 *        kprint("Addr: 0x%lx\n", ptr);
 */

#include <stdint.h>

/* Forward declaration */
struct tty;

/**
 * Kernel print with printf-style formatting
 * 
 * Outputs to kernel TTY/console with optional newline handling.
 * Max message size: 256 bytes
 * 
 * Examples:
 *   kprint("Boot started\n");
 *   kprint("PID=%u, addr=0x%lx\n", pid, address);
 *   kprint("Error: %s\n", error_msg);
 */
void kprint(const char *fmt, ...);

/**
 * Initialize kernel print system (call once at boot)
 */
void kprint_init(void);

/**
 * Internal: Set TTY for kernel printing
 * Called by kernel.c after TTY initialization
 */
void kprint_set_tty(struct tty *tty);

/**
 * Returns non-zero once kprint has a TTY target.
 */
int kprint_is_ready(void);

/**
 * Internal: Set boot time (timer ticks at boot)
 * Called by kernel.c with initial timer_ticks value
 */
void kprint_set_boot_time(volatile uint32_t *timer_ticks_ptr);

/**
 * Get elapsed time in microseconds since boot.
 */
uint64_t kprint_get_elapsed_time(void);

/**
 * Returns the number of bytes currently retained in the kernel log buffer.
 */
uint32_t kprint_log_size(void);

/**
 * Copy kernel log bytes starting at offset into buf.
 * Returns the number of bytes copied.
 */
uint32_t kprint_log_read(uint32_t offset, char *buf, uint32_t size);

#include "kernel/public/core/kprint.h"
#include "kernel/public/core/tty.h"
#include "drivers/serial/uart.h"
#include "hal/hal.h"
#include <stdarg.h>

/**
 * Kernel Print Implementation
 * 
 * Simple printf-style kernel logging.
 * Sends output to kernel TTY.
 */

/* Kernel TTY (initialized in kernel.c) */
static struct tty *g_kernel_tty = 0;

/* Fallback timer reference for timestamp calculation */
static volatile uint32_t *g_timer_ticks_ptr = 0;

/* TSC-backed boot clock state */
static uint64_t g_kprint_boot_tsc = 0;
static uint64_t g_kprint_tsc_hz = 0;

/* Message buffer */
#define KPRINT_BUFFER_SIZE 256
static char g_kprint_buffer[KPRINT_BUFFER_SIZE];

enum {
    KPRINT_LOG_BUFFER_SIZE = 8192u
};

static char g_kprint_log_buffer[KPRINT_LOG_BUFFER_SIZE];
static uint32_t g_kprint_log_len = 0;

enum {
    KPRINT_PIT_RATE_HZ = 1193182u,
    KPRINT_TSC_CALIBRATE_MS = 50u,
    KPRINT_CALIBRATE_SPIN_LIMIT = 10000000u
};

static int kprint_format_timestamp(char *buf, int size, uint64_t elapsed_us);

static void kprint_log_append(const char *text, uint32_t len) {
    uint32_t overflow;
    uint32_t kept;

    if (text == 0 || len == 0) {
        return;
    }
    if (len >= KPRINT_LOG_BUFFER_SIZE) {
        for (uint32_t i = 0; i < KPRINT_LOG_BUFFER_SIZE; i++) {
            g_kprint_log_buffer[i] = text[len - KPRINT_LOG_BUFFER_SIZE + i];
        }
        g_kprint_log_len = KPRINT_LOG_BUFFER_SIZE;
        return;
    }
    if (g_kprint_log_len + len > KPRINT_LOG_BUFFER_SIZE) {
        overflow = g_kprint_log_len + len - KPRINT_LOG_BUFFER_SIZE;
        kept = g_kprint_log_len - overflow;
        for (uint32_t i = 0; i < kept; i++) {
            g_kprint_log_buffer[i] = g_kprint_log_buffer[i + overflow];
        }
        g_kprint_log_len = kept;
    }
    for (uint32_t i = 0; i < len; i++) {
        g_kprint_log_buffer[g_kprint_log_len + i] = text[i];
    }
    g_kprint_log_len += len;
}

static uint64_t kprint_read_tsc(void) {
    return hal_cpu_read_tsc();
}

static uint64_t kprint_detect_tsc_hz_cpuid(void) {
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t max_leaf = 0;

    hal_cpu_cpuid(0, 0, &max_leaf, 0, 0, 0);
    if (max_leaf >= 0x15u) {
        hal_cpu_cpuid(0x15u, 0, &eax, &ebx, &ecx, 0);
        if (eax != 0 && ebx != 0 && ecx != 0) {
            return ((uint64_t)ecx * (uint64_t)ebx) / (uint64_t)eax;
        }
    }
    if (max_leaf >= 0x16u) {
        hal_cpu_cpuid(0x16u, 0, &eax, 0, 0, 0);
        if (eax != 0) {
            return (uint64_t)eax * 1000000ull;
        }
    }

    return 0;
}

static uint64_t kprint_calibrate_tsc_hz_pit(void) {
    uint16_t reload = (uint16_t)((KPRINT_PIT_RATE_HZ * KPRINT_TSC_CALIBRATE_MS) / 1000u);
    uint8_t speaker = hal_io_in8(0x61);
    uint64_t start_tsc;
    uint64_t end_tsc;
    uint32_t spin = 0;

    if (reload == 0) {
        return 0;
    }

    /* Stop gate2 first, keep speaker disabled, then arm PIT channel 2 in mode 0. */
    hal_io_out8(0x61, (uint8_t)(speaker & ~0x03u));
    hal_io_out8(0x43, 0xb0);
    hal_io_out8(0x42, (uint8_t)(reload & 0xffu));
    hal_io_out8(0x42, (uint8_t)((reload >> 8) & 0xffu));

    /* Start counting with gate2 high and sample TSC immediately. */
    hal_io_out8(0x61, (uint8_t)((speaker & ~0x02u) | 0x01u));
    start_tsc = kprint_read_tsc();
    while ((hal_io_in8(0x61) & 0x20u) != 0 && spin < KPRINT_CALIBRATE_SPIN_LIMIT) {
        spin++;
    }
    spin = 0;
    while ((hal_io_in8(0x61) & 0x20u) == 0 && spin < KPRINT_CALIBRATE_SPIN_LIMIT) {
        spin++;
    }
    end_tsc = kprint_read_tsc();

    hal_io_out8(0x61, speaker);
    if (spin >= KPRINT_CALIBRATE_SPIN_LIMIT || end_tsc <= start_tsc) {
        return 0;
    }

    return ((end_tsc - start_tsc) * 1000ull) / (uint64_t)KPRINT_TSC_CALIBRATE_MS;
}
/**
 * Simple snprintf implementation for kernel use
 * 
 * Supports: %d, %u, %x, %lx, %s, %c, %%
 * Returns number of characters written (excluding null terminator)
 */
static int kprint_snprintf(char *buf, int size, const char *fmt, va_list ap) {
    int count = 0;
    
    if (!buf || size <= 0 || !fmt) {
        return 0;
    }

    while (*fmt && count < size - 1) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++;
            switch (*fmt) {
                case '%':
                    buf[count++] = '%';
                    break;
                case 'd': {
                    int val = va_arg(ap, int);
                    char temp[20];
                    uint32_t abs_val = val < 0 ? -val : val;
                    int i = 0;
                    
                    if (val == 0) {
                        temp[i++] = '0';
                    } else {
                        while (abs_val > 0) {
                            temp[i++] = '0' + (abs_val % 10);
                            abs_val /= 10;
                        }
                    }
                    
                    if (val < 0 && count < size - 1) {
                        buf[count++] = '-';
                    }
                    
                    while (i > 0 && count < size - 1) {
                        buf[count++] = temp[--i];
                    }
                    break;
                }
                case 'u': {
                    uint32_t val = va_arg(ap, uint32_t);
                    char temp[20];
                    int i = 0;
                    
                    if (val == 0) {
                        temp[i++] = '0';
                    } else {
                        while (val > 0) {
                            temp[i++] = '0' + (val % 10);
                            val /= 10;
                        }
                    }
                    
                    while (i > 0 && count < size - 1) {
                        buf[count++] = temp[--i];
                    }
                    break;
                }
                case 'x': {
                    uint32_t val = va_arg(ap, uint32_t);
                    static const char hex[] = "0123456789abcdef";
                    int i;
                    
                    for (i = 28; i >= 0 && count < size - 1; i -= 4) {
                        buf[count++] = hex[(val >> i) & 0xf];
                    }
                    break;
                }
                case 'l': {
                    fmt++;
                    if (*fmt == 'x') {
                        uint64_t val = va_arg(ap, uint64_t);
                        static const char hex[] = "0123456789abcdef";
                        int i;
                        
                        for (i = 60; i >= 0 && count < size - 1; i -= 4) {
                            buf[count++] = hex[(val >> i) & 0xf];
                        }
                    } else if (*fmt == 'd') {
                        int64_t val = va_arg(ap, int64_t);
                        char temp[30];
                        uint64_t abs_val = val < 0 ? -val : val;
                        int i = 0;
                        
                        if (val == 0) {
                            temp[i++] = '0';
                        } else {
                            while (abs_val > 0) {
                                temp[i++] = '0' + (abs_val % 10);
                                abs_val /= 10;
                            }
                        }
                        
                        if (val < 0 && count < size - 1) {
                            buf[count++] = '-';
                        }
                        
                        while (i > 0 && count < size - 1) {
                            buf[count++] = temp[--i];
                        }
                    }
                    break;
                }
                case 's': {
                    const char *str = va_arg(ap, const char *);
                    if (!str) {
                        str = "(null)";
                    }
                    while (*str && count < size - 1) {
                        buf[count++] = *str++;
                    }
                    break;
                }
                case 'c': {
                    char ch = (char)va_arg(ap, int);
                    buf[count++] = ch;
                    break;
                }
                default:
                    buf[count++] = '%';
                    if (count < size - 1) {
                        buf[count++] = *fmt;
                    }
                    break;
            }
            fmt++;
        } else if (*fmt == '\\' && *(fmt + 1) == 'n') {
            buf[count++] = '\n';
            fmt += 2;
        } else {
            buf[count++] = *fmt++;
        }
    }
    
    buf[count] = '\0';
    return count;
}

/**
 * Core kernel print function
 */
void kprint(const char *fmt, ...) {
    va_list ap;
    int len;
    int ts_len;
    char final_buffer[512];  /* Timestamp + message */

    if (!fmt) {
        return;
    }

    /* Format timestamp if a clock source is available */
    ts_len = 0;
    if (g_kprint_tsc_hz != 0 || g_timer_ticks_ptr != 0) {
        uint64_t elapsed = kprint_get_elapsed_time();
        ts_len = kprint_format_timestamp(final_buffer, (int)sizeof(final_buffer), elapsed);
    }

    va_start(ap, fmt);
    len = kprint_snprintf(g_kprint_buffer, (int)sizeof(g_kprint_buffer), fmt, ap);
    va_end(ap);

    if (len > 0) {
        /* Combine timestamp + message */
        if (ts_len > 0) {
            /* Copy message after timestamp */
            int copy_len = len;
            if (ts_len + copy_len >= (int)sizeof(final_buffer)) {
                copy_len = (int)sizeof(final_buffer) - ts_len - 1;
            }
            for (int i = 0; i < copy_len; i++) {
                final_buffer[ts_len + i] = g_kprint_buffer[i];
            }
            final_buffer[ts_len + copy_len] = '\0';
            kprint_log_append(final_buffer, (uint32_t)(ts_len + copy_len));
            if (g_kernel_tty != 0) {
                tty_write_str(g_kernel_tty, final_buffer, 0x0f);
            }
            uart_write(final_buffer);
        } else {
            /* No timestamp, just write message */
            kprint_log_append(g_kprint_buffer, (uint32_t)len);
            if (g_kernel_tty != 0) {
                tty_write_str(g_kernel_tty, g_kprint_buffer, 0x0f);
            }
            uart_write(g_kprint_buffer);
        }
    }
}

/**
 * Initialize kernel print system
 * Called from kernel.c after TTY initialization
 */
void kprint_init(void) {
    uart_init();
    g_kprint_boot_tsc = kprint_read_tsc();
    g_kprint_tsc_hz = kprint_detect_tsc_hz_cpuid();
    if (g_kprint_tsc_hz == 0) {
        g_kprint_tsc_hz = kprint_calibrate_tsc_hz_pit();
    }
}

/**
 * Internal: Set kernel TTY for kprint
 * Called from kernel.c
 */
void kprint_set_tty(struct tty *tty) {
    g_kernel_tty = tty;
}

int kprint_is_ready(void) {
    return g_kernel_tty != 0;
}

/**
 * Internal: Set boot time reference (timer ticks pointer)
 * Called from kernel.c with &timer_ticks
 */
void kprint_set_boot_time(volatile uint32_t *timer_ticks_ptr) {
    g_timer_ticks_ptr = timer_ticks_ptr;
}

/**
 * Get elapsed time in microseconds since boot.
 * Uses TSC when available, otherwise falls back to timer ticks.
 */
uint64_t kprint_get_elapsed_time(void) {
    if (g_kprint_tsc_hz != 0) {
        uint64_t now_tsc = kprint_read_tsc();

        if (now_tsc <= g_kprint_boot_tsc) {
            return 0;
        }
        return ((now_tsc - g_kprint_boot_tsc) * 1000000ull) / g_kprint_tsc_hz;
    }
    if (g_timer_ticks_ptr == 0) {
        return 0;
    }

    return (uint64_t)(*g_timer_ticks_ptr) * 1000ull;
}

uint32_t kprint_log_size(void) {
    return g_kprint_log_len;
}

uint32_t kprint_log_read(uint32_t offset, char *buf, uint32_t size) {
    uint32_t available;

    if (buf == 0 || size == 0 || offset >= g_kprint_log_len) {
        return 0;
    }
    available = g_kprint_log_len - offset;
    if (size > available) {
        size = available;
    }
    for (uint32_t i = 0; i < size; i++) {
        buf[i] = g_kprint_log_buffer[offset + i];
    }
    return size;
}

/**
 * Format timestamp string for logging
 * Input: elapsed_us (microseconds)
 * Output: "[x.xxxxxx]" format
 */
static int kprint_format_timestamp(char *buf, int size, uint64_t elapsed_us) {
    if (!buf || size < 12) {
        return 0;
    }
    
    uint32_t seconds = (uint32_t)(elapsed_us / 1000000ull);
    uint32_t micros = (uint32_t)(elapsed_us % 1000000ull);
    
    int count = 0;
    buf[count++] = '[';
    
    /* Write seconds */
    uint32_t sec_val = seconds;
    if (sec_val == 0) {
        buf[count++] = '0';
    } else {
        char temp[20];
        int i = 0;
        while (sec_val > 0) {
            temp[i++] = '0' + (sec_val % 10);
            sec_val /= 10;
        }
        while (i > 0) {
            buf[count++] = temp[--i];
        }
    }
    
    buf[count++] = '.';
    
    buf[count++] = '0' + ((micros / 100000) % 10);
    buf[count++] = '0' + ((micros / 10000) % 10);
    buf[count++] = '0' + ((micros / 1000) % 10);
    buf[count++] = '0' + ((micros / 100) % 10);
    buf[count++] = '0' + ((micros / 10) % 10);
    buf[count++] = '0' + (micros % 10);
    
    buf[count++] = ']';
    buf[count++] = ' ';
    buf[count] = '\0';
    
    return count;
}

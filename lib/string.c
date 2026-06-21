#include "lib/string.h"

uint32_t str_len(const char *text) {
    uint32_t len = 0;

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

int streq(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0') {
        if (*lhs != *rhs) {
            return 0;
        }
        lhs++;
        rhs++;
    }
    return *lhs == '\0' && *rhs == '\0';
}

int starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

const char *skip_spaces(const char *text) {
    while (*text == ' ') {
        text++;
    }
    return text;
}

static uint8_t g_string_runtime_initialized;
static uint8_t g_string_runtime_erms;
static uint32_t g_string_copy_sse2_threshold = 0xffffffffu;
static uint32_t g_string_set_sse2_threshold = 0xffffffffu;

static void string_cpuid(uint32_t leaf,
                         uint32_t subleaf,
                         uint32_t *eax,
                         uint32_t *ebx,
                         uint32_t *ecx,
                         uint32_t *edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

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

void string_runtime_init(void) {
    uint32_t max_leaf = 0u;
    uint32_t ebx = 0u;

    if (g_string_runtime_initialized) {
        return;
    }
    string_cpuid(0u, 0u, &max_leaf, 0, 0, 0);
    if (max_leaf >= 7u) {
        string_cpuid(7u, 0u, 0, &ebx, 0, 0);
        g_string_runtime_erms = (ebx & (1u << 9)) != 0u ? 1u : 0u;
    }
    g_string_runtime_initialized = 1u;
}

int string_runtime_has_erms(void) {
    string_runtime_init();
    return g_string_runtime_erms != 0u;
}

void string_runtime_set_legacy_thresholds(uint32_t copy_threshold, uint32_t set_threshold) {
    g_string_copy_sse2_threshold = copy_threshold;
    g_string_set_sse2_threshold = set_threshold;
}

uint32_t string_runtime_copy_threshold(void) {
    return g_string_copy_sse2_threshold;
}

uint32_t string_runtime_set_threshold(void) {
    return g_string_set_sse2_threshold;
}

void *memcpy_scalar(void *dst, const void *src, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
#if __SIZEOF_POINTER__ == 8
    uint64_t qwords = size / sizeof(uint64_t);
    uint64_t bytes = size % sizeof(uint64_t);

    __asm__ volatile (
        "cld\n\t"
        "rep movsq\n\t"
        "mov %[bytes], %%rcx\n\t"
        "rep movsb"
        : "+D"(out), "+S"(in), "+c"(qwords)
        : [bytes] "r"(bytes)
        : "memory", "cc"
    );
#else
    uint32_t dwords = size / sizeof(uint32_t);
    uint32_t bytes = size % sizeof(uint32_t);

    __asm__ volatile (
        "cld\n\t"
        "rep movsl\n\t"
        "mov %[bytes], %%ecx\n\t"
        "rep movsb"
        : "+D"(out), "+S"(in), "+c"(dwords)
        : [bytes] "r"(bytes)
        : "memory", "cc"
    );
#endif
    return dst;
}

void *memset_scalar(void *dst, int value, uint32_t size) {
    uint8_t *out = dst;

    __asm__ volatile(
        "cld\n\t"
        "rep stosb"
        : "+D"(out), "+c"(size)
        : "a"((uint8_t)value)
        : "memory", "cc"
    );

    return dst;
}

/*
 * Host tools also link this file. The kernel's assembly implementation
 * overrides these weak fallbacks.
 */
__attribute__((weak))
void *memcpy_sse2(void *dst, const void *src, uint32_t size) {
    return memcpy_scalar(dst, src, size);
}

__attribute__((weak))
void *memcpy_erms(void *dst, const void *src, uint32_t size) {
    return memcpy_scalar(dst, src, size);
}

__attribute__((weak))
void *memset_erms(void *dst, int value, uint32_t size) {
    return memset_scalar(dst, value, size);
}

__attribute__((weak))
void *memset_sse2(void *dst, int value, uint32_t size) {
    return memset_scalar(dst, value, size);
}

__attribute__((weak))
void *memcpy_wc(void *dst, const void *src, uint32_t size) {
    return memcpy_scalar(dst, src, size);
}

void *memcpy(void *dst, const void *src, uint32_t size) {
    if (!g_string_runtime_initialized) {
        string_runtime_init();
    }
    if (g_string_runtime_erms) {
        return memcpy_erms(dst, src, size);
    }
    if (size >= g_string_copy_sse2_threshold) {
        return memcpy_sse2(dst, src, size);
    }
    return memcpy_scalar(dst, src, size);
}

void *memset(void *dst, int value, uint32_t size) {
    if (!g_string_runtime_initialized) {
        string_runtime_init();
    }
    if (g_string_runtime_erms) {
        return memset_erms(dst, value, size);
    }
    if (size >= g_string_set_sse2_threshold) {
        return memset_sse2(dst, value, size);
    }
    return memset_scalar(dst, value, size);
}

void *memmove(void *dst, const void *src, uint32_t size) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    if (out == in || size == 0u) {
        return dst;
    }

    if (out < in || out >= in + size) {
        return memcpy(dst, src, size);
    }

#if __SIZEOF_POINTER__ == 8
    uint32_t units = size / sizeof(uint64_t);
    uint32_t bytes = size % sizeof(uint64_t);
    if (units != 0u) {
        uint8_t *out_end = out + size - sizeof(uint64_t);
        const uint8_t *in_end = in + size - sizeof(uint64_t);

        __asm__ volatile (
            "std\n\t"
            "rep movsq\n\t"
            "cld"
            : "+D"(out_end), "+S"(in_end), "+c"(units)
            :
            : "memory", "cc"
        );
    }
#else
    uint32_t units = size / sizeof(uint32_t);
    uint32_t bytes = size % sizeof(uint32_t);
    if (units != 0u) {
        uint8_t *out_end = out + size - sizeof(uint32_t);
        const uint8_t *in_end = in + size - sizeof(uint32_t);

        __asm__ volatile (
            "std\n\t"
            "rep movsl\n\t"
            "cld"
            : "+D"(out_end), "+S"(in_end), "+c"(units)
            :
            : "memory", "cc"
        );
    }
#endif
    if (bytes != 0u) {
        uint8_t *out_end = out + bytes - 1u;
        const uint8_t *in_end = in + bytes - 1u;

        __asm__ volatile (
            "std\n\t"
            "rep movsb\n\t"
            "cld"
            : "+D"(out_end), "+S"(in_end), "+c"(bytes)
            :
            : "memory", "cc"
        );
    }
    return dst;
}

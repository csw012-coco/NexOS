#include "lib/string.h"

#include "hal/hal.h"
#include "kernel/public/core/kprint.h"

enum {
    STRING_BENCH_BUFFER_SIZE = 64u * 1024u,
    STRING_BENCH_SAMPLE_COUNT = 3u,
    STRING_BENCH_TARGET_BYTES = 64u * 1024u,
    STRING_BENCH_SIZE_COUNT = 8u
};

typedef void *(*copy_fn)(void *, const void *, uint32_t);
typedef void *(*set_fn)(void *, int, uint32_t);

static uint8_t g_string_bench_src[STRING_BENCH_BUFFER_SIZE + 32u]
    __attribute__((aligned(16)));
static uint8_t g_string_bench_dst[STRING_BENCH_BUFFER_SIZE + 32u]
    __attribute__((aligned(16)));

static uint64_t string_bench_tsc_begin(void) {
    uint32_t lo;
    uint32_t hi;

    __asm__ __volatile__("cpuid\n\trdtsc"
                         : "=a"(lo), "=d"(hi)
                         : "a"(0)
                         : "rbx", "rcx", "memory");
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t string_bench_tsc_end(void) {
    uint32_t lo;
    uint32_t hi;

    __asm__ __volatile__("mfence\n\tlfence\n\trdtsc"
                         : "=a"(lo), "=d"(hi)
                         :
                         : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static int string_bytes_equal(const uint8_t *lhs, const uint8_t *rhs, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return 1;
}

int string_sse2_self_test(void) {
    static const uint32_t sizes[] = {
        0u, 1u, 7u, 15u, 16u, 17u, 31u, 63u, 64u, 65u,
        127u, 255u, 1024u, 4095u
    };

    for (uint32_t i = 0; i < sizeof(g_string_bench_src); i++) {
        g_string_bench_src[i] = (uint8_t)(i * 37u + 11u);
    }
    for (uint32_t offset = 0; offset < 16u; offset++) {
        for (uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            uint32_t size = sizes[i];

            memset_scalar(g_string_bench_dst, 0xa5, sizeof(g_string_bench_dst));
            memcpy_sse2(g_string_bench_dst + offset,
                        g_string_bench_src + 15u - offset,
                        size);
            if (!string_bytes_equal(g_string_bench_dst + offset,
                                    g_string_bench_src + 15u - offset,
                                    size)) {
                return 0;
            }

            memset_sse2(g_string_bench_dst + offset, 0x5a, size);
            for (uint32_t byte = 0; byte < size; byte++) {
                if (g_string_bench_dst[offset + byte] != 0x5au) {
                    return 0;
                }
            }

            memset_scalar(g_string_bench_dst, 0xa5, sizeof(g_string_bench_dst));
            memcpy_erms(g_string_bench_dst + offset,
                        g_string_bench_src + 15u - offset,
                        size);
            if (!string_bytes_equal(g_string_bench_dst + offset,
                                    g_string_bench_src + 15u - offset,
                                    size)) {
                return 0;
            }
            memset_erms(g_string_bench_dst + offset, 0x5a, size);
            for (uint32_t byte = 0; byte < size; byte++) {
                if (g_string_bench_dst[offset + byte] != 0x5au) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static uint64_t string_bench_copy(copy_fn fn, uint32_t size, uint32_t offset) {
    uint32_t iterations = STRING_BENCH_TARGET_BYTES / size;
    uint64_t best = ~(uint64_t)0;

    if (iterations < 32u) {
        iterations = 32u;
    }
    for (uint32_t sample = 0; sample < STRING_BENCH_SAMPLE_COUNT; sample++) {
        uint64_t start = string_bench_tsc_begin();

        for (uint32_t i = 0; i < iterations; i++) {
            fn(g_string_bench_dst + offset, g_string_bench_src + offset, size);
        }
        uint64_t elapsed = string_bench_tsc_end() - start;
        if (elapsed < best) {
            best = elapsed;
        }
    }
    return best / iterations;
}

static uint64_t string_bench_set(set_fn fn, uint32_t size, uint32_t offset) {
    uint32_t iterations = STRING_BENCH_TARGET_BYTES / size;
    uint64_t best = ~(uint64_t)0;

    if (iterations < 32u) {
        iterations = 32u;
    }
    for (uint32_t sample = 0; sample < STRING_BENCH_SAMPLE_COUNT; sample++) {
        uint64_t start = string_bench_tsc_begin();

        for (uint32_t i = 0; i < iterations; i++) {
            fn(g_string_bench_dst + offset, 0x5a, size);
        }
        uint64_t elapsed = string_bench_tsc_end() - start;
        if (elapsed < best) {
            best = elapsed;
        }
    }
    return best / iterations;
}

static uint32_t string_bench_speedup_x100(uint64_t scalar, uint64_t sse2) {
    if (sse2 == 0u || scalar > (~(uint64_t)0 / 100u)) {
        return 0u;
    }
    return (uint32_t)((scalar * 100u) / sse2);
}

static uint32_t string_bench_stable_threshold(
    const uint32_t sizes[STRING_BENCH_SIZE_COUNT],
    uint64_t base[2][STRING_BENCH_SIZE_COUNT],
    uint64_t optimized[2][STRING_BENCH_SIZE_COUNT]) {
    for (uint32_t first = 0u; first < STRING_BENCH_SIZE_COUNT; first++) {
        int stable = 1;

        for (uint32_t i = first; i < STRING_BENCH_SIZE_COUNT; i++) {
            uint64_t base_total = base[0][i] + base[1][i];
            uint64_t optimized_total = optimized[0][i] + optimized[1][i];

            if (optimized_total > base_total) {
                stable = 0;
                break;
            }
        }
        if (stable) {
            return sizes[first];
        }
    }
    return 0xffffffffu;
}

void string_memory_benchmark(void) {
    static const uint32_t sizes[STRING_BENCH_SIZE_COUNT] = {
        64u, 128u, 256u, 512u, 1024u, 4096u, 16384u, 65536u
    };
    uint64_t copy_base[2][STRING_BENCH_SIZE_COUNT];
    uint64_t copy_sse2[2][STRING_BENCH_SIZE_COUNT];
    uint64_t set_base[2][STRING_BENCH_SIZE_COUNT];
    uint64_t set_sse2[2][STRING_BENCH_SIZE_COUNT];
    copy_fn base_copy = string_runtime_has_erms() ? memcpy_erms : memcpy_scalar;
    set_fn base_set = string_runtime_has_erms() ? memset_erms : memset_scalar;
    uint32_t copy_threshold = 0xffffffffu;
    uint32_t set_threshold = 0xffffffffu;
    int self_test_ok = string_sse2_self_test();

    kprint("membench: SSE2/ERMS self-test %s\n", self_test_ok ? "passed" : "FAILED");
    kprint("membench: mode=%s cycles/call, speedup=base/SSE2\n",
           string_runtime_has_erms() ? "ERMS" : "legacy");

    for (uint32_t offset = 0; offset <= 1u; offset++) {
        for (uint32_t i = 0; i < STRING_BENCH_SIZE_COUNT; i++) {
            uint32_t size = sizes[i];

            copy_base[offset][i] = string_bench_copy(base_copy, size, offset);
            copy_sse2[offset][i] = string_bench_copy(memcpy_sse2, size, offset);
            set_base[offset][i] = string_bench_set(base_set, size, offset);
            set_sse2[offset][i] = string_bench_set(memset_sse2, size, offset);

            kprint("membench: off=%u size=%u copy=%ld/%ld x%u.%u set=%ld/%ld x%u.%u\n",
                   offset,
                   size,
                   (int64_t)copy_base[offset][i],
                   (int64_t)copy_sse2[offset][i],
                   string_bench_speedup_x100(copy_base[offset][i], copy_sse2[offset][i]) / 100u,
                   string_bench_speedup_x100(copy_base[offset][i], copy_sse2[offset][i]) % 100u,
                   (int64_t)set_base[offset][i],
                   (int64_t)set_sse2[offset][i],
                   string_bench_speedup_x100(set_base[offset][i], set_sse2[offset][i]) / 100u,
                   string_bench_speedup_x100(set_base[offset][i], set_sse2[offset][i]) % 100u);
        }
    }

    if (!string_runtime_has_erms() && self_test_ok) {
        copy_threshold = string_bench_stable_threshold(sizes, copy_base, copy_sse2);
        set_threshold = string_bench_stable_threshold(sizes, set_base, set_sse2);
    }
    string_runtime_set_legacy_thresholds(copy_threshold, set_threshold);
    if (string_runtime_has_erms()) {
        kprint("membench: dispatch copy=rep-movsb set=rep-stosb\n");
    } else {
        kprint("membench: dispatch copy-sse2-threshold=%u set-sse2-threshold=%u\n",
               copy_threshold,
               set_threshold);
    }
}

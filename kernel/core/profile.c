#include "kernel/public/core/profile.h"

#include "hal/hal.h"
#include "lib/string.h"

struct kernel_profile_counter {
    char name[SYS_PROFILE_NAME_MAX];
    uint64_t calls;
    uint64_t cycles;
    uint64_t units;
};

static struct kernel_profile_counter g_profile_counters[KERNEL_PROFILE_MAX_COUNTERS];
static uint32_t g_profile_counter_count;

static void kernel_profile_copy_name_local(char *dst, const char *src) {
    uint32_t i = 0u;

    while (src != 0 && src[i] != '\0' && i + 1u < SYS_PROFILE_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

uint32_t kernel_profile_register(const char *name) {
    uint32_t i;

    if (name == 0 || name[0] == '\0') {
        return 0u;
    }
    for (i = 0u; i < g_profile_counter_count; i++) {
        if (streq(g_profile_counters[i].name, name)) {
            return i + 1u;
        }
    }
    if (g_profile_counter_count >= KERNEL_PROFILE_MAX_COUNTERS) {
        return 0u;
    }
    i = g_profile_counter_count++;
    kernel_profile_copy_name_local(g_profile_counters[i].name, name);
    return i + 1u;
}

uint64_t kernel_profile_clock(void) {
    return hal_cpu_read_tsc();
}

void kernel_profile_record(uint32_t handle, uint64_t cycles, uint64_t units) {
    struct kernel_profile_counter *counter;

    if (handle == 0u || handle > g_profile_counter_count) {
        return;
    }
    counter = &g_profile_counters[handle - 1u];
    __atomic_fetch_add(&counter->calls, 1u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&counter->cycles, cycles, __ATOMIC_RELAXED);
    __atomic_fetch_add(&counter->units, units, __ATOMIC_RELAXED);
}

int kernel_profile_query(uint32_t index, struct syscall_profile_info *info) {
    const struct kernel_profile_counter *counter;

    if (info == 0 || index >= g_profile_counter_count) {
        return 0;
    }
    counter = &g_profile_counters[index];
    kernel_profile_copy_name_local(info->name, counter->name);
    info->calls = __atomic_load_n(&counter->calls, __ATOMIC_RELAXED);
    info->cycles = __atomic_load_n(&counter->cycles, __ATOMIC_RELAXED);
    info->units = __atomic_load_n(&counter->units, __ATOMIC_RELAXED);
    return 1;
}

void kernel_profile_reset(void) {
    uint32_t i;

    for (i = 0u; i < g_profile_counter_count; i++) {
        __atomic_store_n(&g_profile_counters[i].calls, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&g_profile_counters[i].cycles, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&g_profile_counters[i].units, 0u, __ATOMIC_RELAXED);
    }
}

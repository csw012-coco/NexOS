#include "kernel/internal/core/machine_info_internal.h"

#include "hal/hal.h"

#define NOS_KERNEL_VERSION "0.1.0"

static void machine_info_copy_text(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void machine_info_fill_cpu_vendor(char vendor[16], uint32_t ebx, uint32_t edx, uint32_t ecx) {
    ((uint32_t *)vendor)[0] = ebx;
    ((uint32_t *)vendor)[1] = edx;
    ((uint32_t *)vendor)[2] = ecx;
    vendor[12] = '\0';
}

static void machine_info_fill_cpu_brand(struct syscall_machine_info *info) {
    uint32_t max_ext_leaf = 0;
    uint32_t *brand_words;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t leaf;
    uint32_t out_index = 0;

    if (info == 0) {
        return;
    }

    hal_cpu_cpuid(0x80000000u, 0u, &max_ext_leaf, 0, 0, 0);
    if (max_ext_leaf < 0x80000004u) {
        machine_info_copy_text(info->cpu_brand, sizeof(info->cpu_brand), "(unknown)");
        return;
    }

    brand_words = (uint32_t *)info->cpu_brand;
    for (leaf = 0x80000002u; leaf <= 0x80000004u; leaf++) {
        hal_cpu_cpuid(leaf, 0u, &eax, &ebx, &ecx, &edx);
        brand_words[out_index++] = eax;
        brand_words[out_index++] = ebx;
        brand_words[out_index++] = ecx;
        brand_words[out_index++] = edx;
    }
    info->cpu_brand[sizeof(info->cpu_brand) - 1u] = '\0';
}

void kernel_fill_machine_info(struct syscall_machine_info *info) {
    uint32_t i;

    if (info == 0) {
        return;
    }

    for (i = 0; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0;
    }

    machine_info_copy_text(info->os_name, sizeof(info->os_name), "NexOS");
    machine_info_copy_text(info->kernel_name, sizeof(info->kernel_name), "kernel64");
    machine_info_copy_text(info->kernel_version, sizeof(info->kernel_version), NOS_KERNEL_VERSION);
    machine_info_copy_text(info->build_date, sizeof(info->build_date), __DATE__ " " __TIME__);
    machine_info_copy_text(info->arch_name, sizeof(info->arch_name), "x86_64");

    hal_cpu_cpuid(0u,
                  0u,
                  &info->cpuid_leaf0_eax,
                  &info->cpuid_leaf0_ebx,
                  &info->cpuid_leaf0_ecx,
                  &info->cpuid_leaf0_edx);
    hal_cpu_cpuid(1u,
                  0u,
                  &info->cpuid_leaf1_eax,
                  &info->cpuid_leaf1_ebx,
                  &info->cpuid_leaf1_ecx,
                  &info->cpuid_leaf1_edx);
    machine_info_fill_cpu_vendor(info->cpu_vendor,
                                 info->cpuid_leaf0_ebx,
                                 info->cpuid_leaf0_edx,
                                 info->cpuid_leaf0_ecx);
    machine_info_fill_cpu_brand(info);
}

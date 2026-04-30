#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/fs/fs_service_root_query_internal.h"

static void syscall_fat_copy_name(char *dst, uint32_t dst_size, const char *src) {
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

static void syscall_fill_fat_entry_info(struct syscall_fat_entry_info *out,
                                        const struct fs_service_root_entry_info *entry) {
    if (out == 0) {
        return;
    }
    out->name[0] = '\0';
    out->first_cluster = 0;
    out->size = 0;
    out->attributes = 0;
    if (entry == 0) {
        return;
    }
    syscall_fat_copy_name(out->name, sizeof(out->name), entry->name);
    out->first_cluster = entry->native_id;
    out->size = entry->size;
    out->attributes = entry->attributes;
}

static int syscall_prepare_user_output(uint64_t user_info_addr, uint32_t size) {
    if (!syscall_user_writable(user_info_addr, size)) {
        return 0;
    }
    return 1;
}

static uint64_t syscall_finish_user_output(uint64_t user_info_addr, const void *info, uint32_t size) {
    if (!syscall_copy_to_user(user_info_addr, info, size)) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}

uint64_t syscall_handle_root_query(uint32_t index, uint64_t user_info_addr) {
    struct syscall_root_entry_info info;
    struct fs_service_root_entry_info entry;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!fs_service_root_get_entry(g_syscall_vfs, index, &entry)) {
        return 0;
    }
    syscall_fat_copy_name(info.name, sizeof(info.name), entry.name);
    info.native_id = entry.native_id;
    info.size = entry.size;
    info.attributes = entry.attributes;
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_root_find(uint64_t user_name_addr, uint64_t user_info_addr) {
    struct syscall_root_entry_info info;
    struct fs_service_root_entry_info entry;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_user_cstr(g_syscall_name_buffer,
                                user_name_addr,
                                sizeof(g_syscall_name_buffer))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!fs_service_root_find_entry(g_syscall_vfs, g_syscall_name_buffer, &entry)) {
        return 0;
    }
    syscall_fat_copy_name(info.name, sizeof(info.name), entry.name);
    info.native_id = entry.native_id;
    info.size = entry.size;
    info.attributes = entry.attributes;
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_fat_root_query(uint32_t index, uint64_t user_info_addr) {
    struct syscall_fat_entry_info info;
    struct fs_service_root_entry_info entry;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!fs_service_root_get_entry(g_syscall_vfs, index, &entry)) {
        return 0;
    }
    syscall_fill_fat_entry_info(&info, &entry);
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_fat_root_find(uint64_t user_name_addr, uint64_t user_info_addr) {
    struct syscall_fat_entry_info info;
    struct fs_service_root_entry_info entry;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_user_cstr(g_syscall_name_buffer,
                                user_name_addr,
                                sizeof(g_syscall_name_buffer))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!fs_service_root_find_entry(g_syscall_vfs, g_syscall_name_buffer, &entry)) {
        return 0;
    }
    syscall_fill_fat_entry_info(&info, &entry);
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

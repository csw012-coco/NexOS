#include "abi/syscall_abi.h"
#include "drivers/audio/audio.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"
#include "kernel/internal/fs/fs_service_root_query_internal.h"
#include "kernel/internal/fs/fs_service_path_internal.h"
#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/internal/fs/path_resolve_internal.h"
#include "fs/vfs_internal.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/core/runtime_internal.h"
#include "kernel/internal/core/system_query_internal.h"
#include "kernel/internal/core/tty_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/mem/address_space_internal.h"
#include "kernel/internal/proc/process_program_registry_internal.h"
#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/profile.h"
#include "kernel/public/input/input_focus.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/proc/process.h"
#include "lib/string.h"

static const struct bootx_boot_info *g_common_query_boot_info;
static const struct bootx_memmap_entry *g_common_query_memmap;
static uint32_t g_common_query_memmap_count;
static struct syscall_framebuffer_info g_common_query_fb_info;
static struct vfs *g_common_query_vfs;

void syscall_common_request_core_query_state_init(
    struct vfs *vfs,
    const struct bootx_boot_info *boot_info,
    const struct bootx_memmap_entry *memmap,
    uint32_t memmap_count) {
    g_common_query_vfs = vfs;
    g_common_query_boot_info = boot_info;
    g_common_query_memmap = memmap;
    g_common_query_memmap_count = memmap_count;
    syscall_common_request_core_fill_fb_info(boot_info, &g_common_query_fb_info);
}

void syscall_common_request_core_fill_fb_info(
    const struct bootx_boot_info *boot_info,
    struct syscall_framebuffer_info *info) {
    if (info == 0) {
        return;
    }
    memset(info, 0, sizeof(*info));
    if (boot_info == 0) {
        return;
    }
    info->present =
        boot_info->console.type == BOOTX_CONSOLE_FRAMEBUFFER ? 1u : 0u;
    info->type = boot_info->console.type;
    info->addr = boot_info->console.framebuffer_addr;
    info->width = boot_info->console.width;
    info->height = boot_info->console.height;
    info->pitch = boot_info->console.pitch;
    info->bpp = boot_info->console.framebuffer_bpp;
    info->red_mask_size = boot_info->console.red_mask_size;
    info->red_mask_shift = boot_info->console.red_mask_shift;
    info->green_mask_size = boot_info->console.green_mask_size;
    info->green_mask_shift = boot_info->console.green_mask_shift;
    info->blue_mask_size = boot_info->console.blue_mask_size;
    info->blue_mask_shift = boot_info->console.blue_mask_shift;
    info->text_columns = boot_info->console.text_columns;
    info->text_rows = boot_info->console.text_rows;
    info->text_color = boot_info->console.text_color;
}

void syscall_common_request_core_set_root_token(const char *token) {
    (void)token;
}

static void syscall_common_capability_event_sanitize(
    struct syscall_capability_event *event) {
    if (event == 0) {
        return;
    }
    event->source[sizeof(event->source) - 1u] = '\0';
    event->action[sizeof(event->action) - 1u] = '\0';
    event->caps[sizeof(event->caps) - 1u] = '\0';
}

uint64_t syscall_common_request_core_capability_event(
    struct syscall_capability_event *event) {
    if (event == 0) {
        return (uint64_t)-1;
    }
    syscall_common_capability_event_sanitize(event);
    vfs_event_capability_emit(event);
    return 0u;
}

static uint64_t syscall_common_bad_pointer(
    const struct syscall_common_user_input_ops *ops) {
    if (ops != 0 && ops->bad_pointer != 0) {
        return ops->bad_pointer();
    }
    return ops != 0 ? ops->bad_pointer_value : (uint64_t)-1;
}

static uint64_t syscall_common_copy_bad_pointer(
    const struct syscall_common_user_copy_ops *ops) {
    if (ops != 0 && ops->bad_pointer != 0) {
        return ops->bad_pointer();
    }
    return ops != 0 ? ops->bad_pointer_value : (uint64_t)-1;
}

static uint64_t syscall_common_access_denied(void) {
    const struct process *proc = process_current();

    kprint("security: access denied pid=%u uid=%u caps=%x\n",
           proc != 0 ? proc->pid : 0u,
           process_uid(proc),
           process_capabilities(proc));
    return (uint64_t)(int64_t)-NEX_ERR_ACCES;
}

static int syscall_common_current_has_capability(uint32_t cap) {
    return process_has_capability(process_current(), cap);
}

static uint64_t syscall_common_request_core_capability(
    uint32_t op,
    uint32_t mask,
    const char *auth_token) {
    struct process *proc = process_current_mut();
    uint32_t current;
    uint32_t valid_mask = PROCESS_CAP_SYS_ADMIN;

    (void)auth_token;
    if (proc == 0 || (mask & ~valid_mask) != 0u) {
        return (uint64_t)(int64_t)-NEX_ERR_INVAL;
    }
    current = process_capabilities(proc);
    switch (op) {
        case SYS_CAP_OP_GET:
            return current;
        case SYS_CAP_OP_DROP:
            process_set_capabilities(proc, current & ~mask);
            return process_capabilities(proc);
        case SYS_CAP_OP_GRANT:
            if (!process_has_capability(proc, PROCESS_CAP_GRANT)) {
                return syscall_common_access_denied();
            }
            process_set_capabilities(proc, current | mask);
            return process_capabilities(proc);
        case SYS_CAP_OP_SPAWN_SET:
            if ((mask & ~current) != 0u) {
                return syscall_common_access_denied();
            }
            process_set_next_spawn_capabilities(proc, mask);
            return process_next_spawn_capabilities(proc, current);
        case SYS_CAP_OP_SPAWN_CLEAR:
            process_clear_next_spawn_capabilities(proc);
            return process_capabilities(proc);
        case SYS_CAP_OP_SPAWN_GET:
            return process_next_spawn_capabilities_enabled(proc)
                ? process_next_spawn_capabilities(proc, current)
                : current;
        case SYS_CAP_OP_AUTH_GRANT:
            if (process_uid(proc) != 0u) {
                return syscall_common_access_denied();
            }
            process_set_capabilities(proc, current | mask);
            return process_capabilities(proc);
        default:
            return (uint64_t)(int64_t)-NEX_ERR_INVAL;
    }
}

int syscall_common_request_core_capability_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    char token[64];
    uint32_t op;
    uint64_t token_addr;

    if (request == 0 || result == 0 || request->number != SYS_CAPABILITY) {
        return 0;
    }
    op = kernel_syscall_arg_u32(request, 0);
    token_addr = kernel_syscall_arg_u64(request, 2);
    token[0] = '\0';
    if (op == SYS_CAP_OP_AUTH_GRANT) {
        if (ops == 0 || ops->copy_user_cstr == 0 || token_addr == 0u ||
            !ops->copy_user_cstr(token, token_addr, sizeof(token))) {
            result->action = SYSCALL_RESULT_RETURN;
            result->value = syscall_common_copy_bad_pointer(ops);
            return 1;
        }
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_common_request_core_capability(
        op,
        kernel_syscall_arg_u32(request, 1),
        token);
    return 1;
}

static uint64_t syscall_common_request_core_identity(
    uint32_t op,
    uint32_t uid,
    const char *auth_token,
    const struct syscall_common_user_copy_ops *ops,
    uint64_t user_info_addr) {
    struct process *proc = process_current_mut();
    struct syscall_identity_info info;

    if (proc == 0) {
        return (uint64_t)(int64_t)-NEX_ERR_INVAL;
    }
    switch (op) {
        case SYS_IDENTITY_OP_GET:
            if (ops == 0 || ops->copy_to_user == 0 || user_info_addr == 0u) {
                return (uint64_t)(int64_t)-NEX_ERR_INVAL;
            }
            info.uid = process_uid(proc);
            info.gid = process_gid(proc);
            return ops->copy_to_user(user_info_addr, &info, sizeof(info))
                ? 1u
                : syscall_common_copy_bad_pointer(ops);
        case SYS_IDENTITY_OP_DROP_USER:
            if (process_uid(proc) != 0u) {
                return syscall_common_access_denied();
            }
            if (uid == 0u) {
                uid = 1000u;
            }
            process_set_identity(proc, uid, uid);
            process_set_capabilities(proc, 0u);
            process_clear_next_spawn_capabilities(proc);
            return 1u;
        case SYS_IDENTITY_OP_AUTH_ROOT:
            (void)auth_token;
            if (process_uid(proc) != 0u) {
                return syscall_common_access_denied();
            }
            process_set_identity(proc, 0u, 0u);
            process_set_capabilities(proc, PROCESS_CAP_SYS_ADMIN);
            return 1u;
        case SYS_IDENTITY_OP_PUSH:
            return process_identity_push(proc)
                ? 1u
                : (uint64_t)(int64_t)-NEX_ERR_AGAIN;
        case SYS_IDENTITY_OP_POP:
            return process_identity_pop(proc)
                ? 1u
                : (uint64_t)(int64_t)-NEX_ERR_INVAL;
        default:
            return (uint64_t)(int64_t)-NEX_ERR_INVAL;
    }
}

int syscall_common_request_core_identity_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    char token[64];
    uint32_t op;
    uint64_t token_addr;

    if (request == 0 || result == 0 || request->number != SYS_IDENTITY) {
        return 0;
    }
    op = kernel_syscall_arg_u32(request, 0);
    token_addr = kernel_syscall_arg_u64(request, 2);
    token[0] = '\0';
    if (op == SYS_IDENTITY_OP_AUTH_ROOT) {
        if (ops == 0 || ops->copy_user_cstr == 0 || token_addr == 0u ||
            !ops->copy_user_cstr(token, token_addr, sizeof(token))) {
            result->action = SYSCALL_RESULT_RETURN;
            result->value = syscall_common_copy_bad_pointer(ops);
            return 1;
        }
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_common_request_core_identity(
        op,
        kernel_syscall_arg_u32(request, 1),
        token,
        ops,
        kernel_syscall_arg_u64(request, 3));
    return 1;
}

uint64_t syscall_common_request_core_capability_event_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_input_ops *ops) {
    struct syscall_capability_event event;

    if (ops == 0 || ops->copy_from_user == 0 ||
        !ops->copy_from_user(&event, user_info_addr, sizeof(event))) {
        return syscall_common_bad_pointer(ops);
    }
    return syscall_common_request_core_capability_event(&event);
}

int syscall_common_request_core_capability_event_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_input_ops *ops) {
    if (request == 0 || result == 0 ||
        request->number != SYS_CAPABILITY_EVENT) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_common_request_core_capability_event_transfer(
        kernel_syscall_arg_u64(request, 0), ops);
    return 1;
}

static uint64_t syscall_common_misc_call(uint64_t (*fn)(void *), void *ctx,
                                         uint64_t fallback) {
    return fn != 0 ? fn(ctx) : fallback;
}

int syscall_common_request_core_misc_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_misc_ops *ops) {
    if (request == 0 || result == 0 || ops == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_CLEAR:
            result->value = syscall_common_misc_call(ops->clear, ops->ctx, 0u);
            return 1;
        case SYS_TICKS:
            result->value = syscall_common_misc_call(ops->ticks, ops->ctx, 0u);
            return 1;
        case SYS_REBOOT:
            if (!syscall_common_current_has_capability(PROCESS_CAP_POWER)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value =
                syscall_common_misc_call(ops->reboot, ops->ctx, (uint64_t)-1);
            return 1;
        case SYS_POWEROFF:
            if (!syscall_common_current_has_capability(PROCESS_CAP_POWER)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value =
                syscall_common_misc_call(ops->poweroff, ops->ctx, (uint64_t)-1);
            return 1;
        default:
            return 0;
    }
}

static uint64_t syscall_common_proc_call_pid(uint64_t (*fn)(void *, uint32_t),
                                             void *ctx,
                                             uint32_t pid,
                                             uint64_t fallback) {
    return fn != 0 ? fn(ctx, pid) : fallback;
}

int syscall_common_request_core_proc_lifecycle_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_proc_ops *ops) {
    if (request == 0 || result == 0 || ops == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_EXIT:
            if (ops->exit != 0) {
                result->value =
                    ops->exit(ops->ctx, kernel_syscall_arg_i32(request, 0));
            } else {
                result->value = kernel_syscall_arg_u32(request, 0);
                result->action = SYSCALL_RESULT_EXIT;
            }
            return 1;
        case SYS_YIELD:
            if (ops->yield != 0) {
                result->value = ops->yield(ops->ctx);
            } else {
                result->value = 0u;
                result->action = SYSCALL_RESULT_YIELD;
            }
            return 1;
        case SYS_EXEC_REPLACE:
            if (ops->exec_replace != 0) {
                result->value =
                    ops->exec_replace(ops->ctx,
                                      kernel_syscall_arg_u64(request, 0),
                                      kernel_syscall_arg_u64(request, 1));
            } else {
                result->value = kernel_syscall_arg_u64(request, 0);
                result->action = SYSCALL_RESULT_EXEC;
            }
            return 1;
        case SYS_EXEC:
            result->value = ops->exec != 0
                ? ops->exec(ops->ctx,
                            kernel_syscall_arg_u64(request, 0),
                            kernel_syscall_arg_u64(request, 1))
                : (uint64_t)-1;
            return 1;
        case SYS_FORK:
            result->value = ops->fork != 0 ? ops->fork(ops->ctx) : (uint64_t)-1;
            return 1;
        case SYS_WAIT:
            if (ops->wait != 0) {
                result->value = ops->wait(ops->ctx,
                                          kernel_syscall_arg_u32(request, 0),
                                          kernel_syscall_arg_u64(request, 1));
            } else {
                result->value = kernel_syscall_arg_u32(request, 0);
                result->extra = kernel_syscall_arg_u64(request, 1);
                result->action = SYSCALL_RESULT_WAIT;
            }
            return 1;
        case SYS_SLEEP:
            if (ops->sleep != 0) {
                result->value =
                    ops->sleep(ops->ctx, kernel_syscall_arg_u32(request, 0));
            } else {
                result->value = kernel_syscall_arg_u32(request, 0);
                result->action = SYSCALL_RESULT_SLEEP;
            }
            return 1;
        case SYS_GETPID:
            result->value = ops->getpid != 0 ? ops->getpid(ops->ctx) : 0u;
            return 1;
        case SYS_PROC_QUERY:
            result->value = ops->proc_query != 0
                ? ops->proc_query(ops->ctx,
                                  kernel_syscall_arg_u32(request, 0),
                                  kernel_syscall_arg_u32(request, 1),
                                  kernel_syscall_arg_u64(request, 2))
                : 0u;
            return 1;
        case SYS_KILL:
            if (!syscall_common_current_has_capability(PROCESS_CAP_SIGNAL)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_proc_call_pid(
                ops->kill,
                ops->ctx,
                kernel_syscall_arg_u32(request, 0),
                (uint64_t)(int64_t)-NEX_ERR_NOSYS);
            return 1;
        case SYS_FG:
            result->value = syscall_common_proc_call_pid(
                ops->fg,
                ops->ctx,
                kernel_syscall_arg_u32(request, 0),
                (uint64_t)(int64_t)-NEX_ERR_NOSYS);
            return 1;
        case SYS_BG:
            result->value = syscall_common_proc_call_pid(
                ops->bg,
                ops->ctx,
                kernel_syscall_arg_u32(request, 0),
                (uint64_t)(int64_t)-NEX_ERR_NOSYS);
            return 1;
        case SYS_TTY_CLAIM:
            result->value = ops->tty_claim != 0
                ? ops->tty_claim(ops->ctx)
                : (uint64_t)(int64_t)-NEX_ERR_NOSYS;
            return 1;
        case SYS_SPAWN:
            result->value = ops->spawn != 0
                ? ops->spawn(ops->ctx,
                             kernel_syscall_arg_u64(request, 0),
                             kernel_syscall_arg_u32(request, 1),
                             kernel_syscall_arg_u32(request, 2),
                             kernel_syscall_arg_u64(request, 3))
                : (uint64_t)(int64_t)-NEX_ERR_NOSYS;
            return 1;
        default:
            return 0;
    }
}

static void syscall_common_query_copy_name(char *dst,
                                           uint32_t dst_size,
                                           const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int syscall_common_query_set_size(uint32_t *info_size, uint32_t size) {
    if (info_size != 0) {
        *info_size = size;
    }
    return 1;
}

enum {
    SYSCALL_COMMON_IPC_NAME_MAX = 31u,
    SYSCALL_COMMON_MQ_MAX = 16u,
    SYSCALL_COMMON_MQ_DEPTH = 16u,
    SYSCALL_COMMON_SEM_MAX = 32u,
    SYSCALL_COMMON_SEM_VALUE_MAX = 0x7fffffffu
};

struct syscall_common_ipc_message {
    uint16_t size;
    uint8_t data[SYS_MQ_MESSAGE_MAX];
};

struct syscall_common_message_queue {
    uint8_t used;
    uint8_t linked;
    uint8_t head;
    uint8_t count;
    char name[SYSCALL_COMMON_IPC_NAME_MAX + 1u];
    struct syscall_common_ipc_message messages[SYSCALL_COMMON_MQ_DEPTH];
};

struct syscall_common_semaphore {
    uint8_t used;
    uint8_t linked;
    char name[SYSCALL_COMMON_IPC_NAME_MAX + 1u];
    uint32_t value;
};

static struct syscall_common_message_queue
    g_syscall_common_mq[SYSCALL_COMMON_MQ_MAX];
static struct syscall_common_semaphore
    g_syscall_common_sem[SYSCALL_COMMON_SEM_MAX];

static void syscall_common_ipc_copy_name_local(char *dst, const char *src) {
    uint32_t i = 0u;

    while (src[i] != '\0' && i < SYSCALL_COMMON_IPC_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int syscall_common_ipc_name_equal(const char *lhs, const char *rhs) {
    uint32_t i = 0u;

    while (lhs[i] != '\0' && rhs[i] != '\0' && lhs[i] == rhs[i]) {
        i++;
    }
    return lhs[i] == rhs[i];
}

static int syscall_common_ipc_copy_name(
    uint64_t user_name_addr,
    char *name,
    const struct syscall_common_user_copy_ops *ops) {
    if (ops == 0 || ops->copy_user_cstr == 0 ||
        !ops->copy_user_cstr(
            name, user_name_addr, SYSCALL_COMMON_IPC_NAME_MAX + 1u)) {
        return 0;
    }
    return name[0] != '\0';
}

static void syscall_common_query_fill_root_entry(
    struct syscall_root_entry_info *out,
    const struct fs_service_root_entry_info *entry) {
    if (out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (entry == 0) {
        return;
    }
    syscall_common_query_copy_name(out->name, sizeof(out->name), entry->name);
    out->native_id = entry->native_id;
    out->size = entry->size;
    out->attributes = entry->attributes;
}

static void syscall_common_query_fill_fat_root_entry(
    struct syscall_fat_entry_info *out,
    const struct fs_service_root_entry_info *entry) {
    if (out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (entry == 0) {
        return;
    }
    syscall_common_query_copy_name(out->name, sizeof(out->name), entry->name);
    out->first_cluster = entry->native_id;
    out->size = entry->size;
    out->attributes = entry->attributes;
}

int syscall_common_request_core_query_info(uint32_t kind,
                                           uint64_t arg0,
                                           uint64_t arg1,
                                           void *info,
                                           uint32_t *info_size) {
    if (info == 0) {
        return 0;
    }
    if (info_size != 0) {
        *info_size = 0u;
    }
    switch (kind) {
        case SYS_QUERY_BOOT_INFO: {
            struct syscall_boot_info *boot = (struct syscall_boot_info *)info;

            memset(boot, 0, sizeof(*boot));
            if (g_common_query_boot_info != 0) {
                boot->boot_drive = g_common_query_boot_info->boot_drive;
                boot->partition_lba = g_common_query_boot_info->partition_lba;
                boot->partition_sectors =
                    g_common_query_boot_info->partition_sectors;
                boot->module_count = g_common_query_boot_info->module_count;
            }
            return syscall_common_query_set_size(info_size, sizeof(*boot));
        }
        case SYS_QUERY_MEMMAP: {
            struct syscall_memmap_info *memmap =
                (struct syscall_memmap_info *)info;
            uint32_t index = (uint32_t)arg0;

            if (g_common_query_memmap == 0 ||
                index >= g_common_query_memmap_count) {
                return 0;
            }
            memmap->base = g_common_query_memmap[index].base;
            memmap->length = g_common_query_memmap[index].length;
            memmap->type = g_common_query_memmap[index].type;
            memmap->reserved = g_common_query_memmap[index].reserved;
            return syscall_common_query_set_size(info_size, sizeof(*memmap));
        }
        case SYS_QUERY_PMM: {
            struct syscall_pmm_info *pmm = (struct syscall_pmm_info *)info;

            pmm->total_pages = pmm_total_pages();
            pmm->free_pages = pmm_free_pages();
            pmm->used_pages = pmm_used_pages();
            pmm->dropped_pages = pmm_dropped_pages();
            return syscall_common_query_set_size(info_size, sizeof(*pmm));
        }
        case SYS_QUERY_VM:
            process_mm_query_vm_snapshot((struct syscall_vm_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_vm_info));
        case SYS_QUERY_FB:
            *(struct syscall_framebuffer_info *)info = g_common_query_fb_info;
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_framebuffer_info));
        case SYS_QUERY_BLOCK:
            return kernel_query_block_info((uint32_t)arg0,
                                           (struct syscall_block_info *)info) &&
                   syscall_common_query_set_size(
                       info_size, sizeof(struct syscall_block_info));
        case SYS_QUERY_PART:
            return kernel_query_part_info((uint32_t)arg0,
                                          (uint32_t)arg1,
                                          (struct syscall_partition_info *)info) &&
                   syscall_common_query_set_size(
                       info_size, sizeof(struct syscall_partition_info));
        case SYS_QUERY_PROGRAM: {
            struct syscall_program_info *program =
                (struct syscall_program_info *)info;
            const char *name = process_program_name((uint32_t)arg0);

            if (name == 0) {
                return 0;
            }
            memset(program, 0, sizeof(*program));
            syscall_common_query_copy_name(program->name,
                                           sizeof(program->name),
                                           name);
            return syscall_common_query_set_size(info_size, sizeof(*program));
        }
        case SYS_QUERY_PCI:
            kernel_query_pci_info((uint32_t)arg0,
                                  (struct syscall_pci_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_pci_info));
        case SYS_QUERY_AC97:
            kernel_query_ac97_info((struct syscall_ac97_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_ac97_info));
        case SYS_QUERY_HDA:
            kernel_query_hda_info((struct syscall_hda_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_hda_info));
        case SYS_QUERY_RTL8139:
            kernel_query_rtl8139_info((struct syscall_rtl8139_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_rtl8139_info));
        case SYS_QUERY_AUDIO:
            (void)kernel_query_audio_info((uint32_t)arg0,
                                          (struct syscall_audio_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_audio_info));
        case SYS_QUERY_RTC:
            return kernel_query_rtc_info((struct syscall_rtc_info *)info) &&
                   syscall_common_query_set_size(
                       info_size, sizeof(struct syscall_rtc_info));
        case SYS_QUERY_PROFILE:
            if ((arg1 & SYS_PROFILE_QUERY_RESET) != 0u) {
                if (!syscall_common_current_has_capability(PROCESS_CAP_DEBUG)) {
                    return 0;
                }
                kernel_profile_reset();
            }
            return kernel_profile_query((uint32_t)arg0,
                                        (struct syscall_profile_info *)info) &&
                   syscall_common_query_set_size(
                       info_size, sizeof(struct syscall_profile_info));
        case SYS_QUERY_ROOT:
        case SYS_QUERY_FAT_ROOT: {
            struct fs_service_root_entry_info entry;

            if (!fs_service_root_get_entry(
                    g_common_query_vfs, (uint32_t)arg0, &entry)) {
                return 0;
            }
            if (kind == SYS_QUERY_FAT_ROOT) {
                syscall_common_query_fill_fat_root_entry(
                    (struct syscall_fat_entry_info *)info, &entry);
                return syscall_common_query_set_size(
                    info_size, sizeof(struct syscall_fat_entry_info));
            }
            syscall_common_query_fill_root_entry(
                (struct syscall_root_entry_info *)info, &entry);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_root_entry_info));
        }
        case SYS_QUERY_KMSG: {
            struct syscall_kmsg_info *kmsg = (struct syscall_kmsg_info *)info;
            uint32_t copied;

            memset(kmsg, 0, sizeof(*kmsg));
            kmsg->total_size = kprint_log_size();
            kmsg->offset = (uint32_t)arg0;
            copied = kprint_log_read((uint32_t)arg0,
                                     kmsg->data,
                                     sizeof(kmsg->data));
            if (copied == 0u) {
                return 0;
            }
            kmsg->bytes_copied = copied;
            return syscall_common_query_set_size(info_size, sizeof(*kmsg));
        }
        default:
            return 0;
    }
}

static int syscall_common_request_core_query_find_info(
    uint32_t kind,
    uint64_t user_name_addr,
    void *info,
    uint32_t *info_size,
    const struct syscall_common_user_copy_ops *ops) {
    struct fs_service_root_entry_info entry;
    char name[NOS_NAME_BUFFER_SIZE];

    if (info == 0 || ops == 0 || ops->copy_user_cstr == 0) {
        return 0;
    }
    if (!ops->copy_user_cstr(name, user_name_addr, sizeof(name)) ||
        !fs_service_root_find_entry(g_common_query_vfs, name, &entry)) {
        return 0;
    }
    if (kind == SYS_QUERY_FAT_ROOT_FIND) {
        syscall_common_query_fill_fat_root_entry(
            (struct syscall_fat_entry_info *)info, &entry);
        return syscall_common_query_set_size(
            info_size, sizeof(struct syscall_fat_entry_info));
    }
    syscall_common_query_fill_root_entry(
        (struct syscall_root_entry_info *)info, &entry);
    return syscall_common_query_set_size(
        info_size, sizeof(struct syscall_root_entry_info));
}

static int syscall_common_query_kind_supported(uint32_t kind) {
    switch (kind) {
        case SYS_QUERY_BOOT_INFO:
        case SYS_QUERY_MEMMAP:
        case SYS_QUERY_PMM:
        case SYS_QUERY_VM:
        case SYS_QUERY_FB:
        case SYS_QUERY_BLOCK:
        case SYS_QUERY_PART:
        case SYS_QUERY_PROGRAM:
        case SYS_QUERY_PCI:
        case SYS_QUERY_AC97:
        case SYS_QUERY_HDA:
        case SYS_QUERY_RTL8139:
        case SYS_QUERY_AUDIO:
        case SYS_QUERY_RTC:
        case SYS_QUERY_PROFILE:
        case SYS_QUERY_ROOT:
        case SYS_QUERY_FAT_ROOT:
        case SYS_QUERY_ROOT_FIND:
        case SYS_QUERY_FAT_ROOT_FIND:
        case SYS_QUERY_KMSG:
            return 1;
        default:
            return 0;
    }
}

static int syscall_common_query_kind_supported_by_ops(
    uint32_t kind,
    const struct syscall_common_query_ops *ops) {
    switch (kind) {
        case SYS_QUERY_TTY:
            return ops != 0 && ops->fd_kind != 0;
        case SYS_QUERY_FD:
            return ops != 0 && ops->fd_query != 0;
        case SYS_QUERY_MOUNT:
            return ops != 0 && ops->fill_mount_info != 0;
        case SYS_QUERY_MACHINE_INFO:
            return ops != 0 && ops->fill_machine_info != 0;
        default:
            return 0;
    }
}

static int syscall_common_query_requires_debug(uint32_t kind) {
    switch (kind) {
        case SYS_QUERY_MEMMAP:
        case SYS_QUERY_PMM:
        case SYS_QUERY_VM:
        case SYS_QUERY_PCI:
        case SYS_QUERY_PROFILE:
        case SYS_QUERY_KMSG:
            return 1;
        default:
            return 0;
    }
}

static void syscall_common_query_copy_path(char *dst,
                                           uint32_t size,
                                           const char *src) {
    if (dst == 0 || size == 0u) {
        return;
    }
    memset(dst, 0, size);
    if (src == 0) {
        return;
    }
    for (uint32_t i = 0u; i + 1u < size && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
}

static int syscall_common_query_ops_info(
    uint32_t kind,
    uint64_t arg0,
    uint64_t arg1,
    void *info,
    uint32_t *info_size,
    const struct syscall_common_query_ops *ops) {
    if (info == 0 || ops == 0) {
        return 0;
    }
    switch (kind) {
        case SYS_QUERY_TTY: {
            struct syscall_tty_info *tty = (struct syscall_tty_info *)info;
            uint32_t fd_kind =
                ops->fd_kind != 0 ? ops->fd_kind(ops->ctx, (uint32_t)arg0) : 0u;

            if (ops->tty_query != 0) {
                if (!ops->tty_query(ops->ctx, (uint32_t)arg0, tty)) {
                    return 0;
                }
                return syscall_common_query_set_size(info_size, sizeof(*tty));
            }
            if (fd_kind != KERNEL_FILE_TTY_STDIN &&
                fd_kind != KERNEL_FILE_TTY_STDOUT &&
                fd_kind != KERNEL_FILE_TTY_STDERR) {
                return 0;
            }
            memset(tty, 0, sizeof(*tty));
            tty->kind = SYS_TTY_KIND_VIRTUAL;
            tty->active = 1u;
            syscall_common_query_copy_path(
                tty->path, sizeof(tty->path), "/dev/tty0");
            return syscall_common_query_set_size(info_size, sizeof(*tty));
        }
        case SYS_QUERY_FD:
            if (ops->fd_query == 0 ||
                ops->fd_query(
                    ops->ctx, (uint32_t)arg0, (struct syscall_fd_info *)info) <=
                    0) {
                return 0;
            }
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_fd_info));
        case SYS_QUERY_MOUNT:
            if (ops->fill_mount_info == 0 ||
                ops->fill_mount_info(ops->ctx,
                                     (struct syscall_mount_info *)info,
                                     (uint32_t)arg0,
                                     (uint32_t)arg1) <= 0) {
                return 0;
            }
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_mount_info));
        case SYS_QUERY_MACHINE_INFO:
            if (ops->fill_machine_info == 0) {
                return 0;
            }
            ops->fill_machine_info(ops->ctx,
                                   (struct syscall_machine_info *)info);
            return syscall_common_query_set_size(
                info_size, sizeof(struct syscall_machine_info));
        default:
            return 0;
    }
}

uint64_t syscall_common_request_core_query_transfer(
    uint32_t kind,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops) {
    union {
        struct syscall_boot_info boot;
        struct syscall_memmap_info memmap;
        struct syscall_pmm_info pmm;
        struct syscall_vm_info vm;
        struct syscall_framebuffer_info fb;
        struct syscall_block_info block;
        struct syscall_partition_info part;
        struct syscall_program_info program;
        struct syscall_pci_info pci;
        struct syscall_ac97_info ac97;
        struct syscall_hda_info hda;
        struct syscall_rtl8139_info rtl8139;
        struct syscall_audio_info audio;
        struct syscall_rtc_info rtc;
        struct syscall_profile_info profile;
        struct syscall_root_entry_info root;
        struct syscall_fat_entry_info fat_root;
        struct syscall_kmsg_info kmsg;
        struct syscall_tty_info tty;
        struct syscall_fd_info fd;
        struct syscall_mount_info mount;
        struct syscall_machine_info machine;
    } info;
    uint32_t info_size = 0u;

    if (ops == 0 || ops->copy_to_user == 0) {
        return syscall_common_copy_bad_pointer(ops);
    }
    if (syscall_common_query_requires_debug(kind) &&
        !syscall_common_current_has_capability(PROCESS_CAP_DEBUG)) {
        return syscall_common_access_denied();
    }
    if (kind == SYS_QUERY_ROOT_FIND || kind == SYS_QUERY_FAT_ROOT_FIND) {
        if (!syscall_common_request_core_query_find_info(
                kind, arg0, &info, &info_size, ops)) {
            return 0u;
        }
    } else if (!syscall_common_request_core_query_info(
                   kind, arg0, arg1, &info, &info_size)) {
            return 0u;
    }
    if (!ops->copy_to_user(user_info_addr, &info, info_size)) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return 1u;
}

static uint64_t syscall_common_request_core_query_transfer_with_ops(
    uint32_t kind,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *copy_ops,
    const struct syscall_common_query_ops *query_ops) {
    union {
        struct syscall_tty_info tty;
        struct syscall_fd_info fd;
        struct syscall_mount_info mount;
        struct syscall_machine_info machine;
    } info;
    uint32_t info_size = 0u;

    if (copy_ops == 0 || copy_ops->copy_to_user == 0) {
        return syscall_common_copy_bad_pointer(copy_ops);
    }
    if (!syscall_common_query_ops_info(
            kind, arg0, arg1, &info, &info_size, query_ops)) {
        return 0u;
    }
    if (!copy_ops->copy_to_user(user_info_addr, &info, info_size)) {
        return syscall_common_copy_bad_pointer(copy_ops);
    }
    return 1u;
}

int syscall_common_request_core_query_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    uint32_t kind;

    if (request == 0 || result == 0 || request->number != SYS_QUERY) {
        return 0;
    }
    kind = kernel_syscall_arg_u32(request, 0);
    if (!syscall_common_query_kind_supported(kind)) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_common_request_core_query_transfer(
        kind,
        kernel_syscall_arg_u64(request, 1),
        kernel_syscall_arg_u64(request, 2),
        kernel_syscall_arg_u64(request, 3),
        ops);
    return 1;
}

int syscall_common_request_core_query_request_with_ops(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *copy_ops,
    const struct syscall_common_query_ops *query_ops) {
    uint32_t kind;

    if (request == 0 || result == 0 || request->number != SYS_QUERY) {
        return 0;
    }
    kind = kernel_syscall_arg_u32(request, 0);
    if (syscall_common_query_kind_supported(kind)) {
        return syscall_common_request_core_query_request(
            request, result, copy_ops);
    }
    if (!syscall_common_query_kind_supported_by_ops(kind, query_ops)) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_common_request_core_query_transfer_with_ops(
        kind,
        kernel_syscall_arg_u64(request, 1),
        kernel_syscall_arg_u64(request, 2),
        kernel_syscall_arg_u64(request, 3),
        copy_ops,
        query_ops);
    return 1;
}

static uint32_t syscall_common_path_len(const char *text) {
    uint32_t len = 0u;

    while (text != 0 && text[len] != '\0') {
        len++;
    }
    return len;
}

static int syscall_common_copy_resolved_user_path(
    struct process *proc,
    uint64_t user_path_addr,
    char *buffer,
    uint32_t size,
    const struct syscall_common_user_copy_ops *ops) {
    char input[NOS_PATH_MAX + 1u];
    uint32_t i = 0u;

    if (proc == 0 || buffer == 0 || size == 0u ||
        ops == 0 || ops->copy_user_cstr == 0) {
        return 0;
    }
    if (!ops->copy_user_cstr(input, user_path_addr, sizeof(input))) {
        return -1;
    }
    while (input[i] != '\0' && i + 1u < size) {
        buffer[i] = input[i];
        i++;
    }
    buffer[i] = '\0';
    return fs_resolve_process_path(proc, input, buffer, size) ? 1 : 0;
}

uint64_t syscall_common_request_core_getcwd_transfer(
    struct process *proc,
    uint64_t user_path_addr,
    uint32_t size,
    const struct syscall_common_user_copy_ops *ops) {
    const char *cwd;
    uint32_t len;

    if (proc == 0 || user_path_addr == 0u || size == 0u ||
        ops == 0 || ops->copy_to_user == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    cwd = process_cwd(proc);
    len = syscall_common_path_len(cwd) + 1u;
    if (len > size) {
        return (uint64_t)(uint32_t)-1;
    }
    if (!ops->copy_to_user(user_path_addr, cwd, len)) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return len - 1u;
}

uint64_t syscall_common_request_core_opendir_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_path_addr,
    const struct syscall_common_user_copy_ops *ops) {
    char path[NOS_PATH_MAX + 1u];

    if (proc == 0 || vfs == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    switch (syscall_common_copy_resolved_user_path(
        proc, user_path_addr, path, sizeof(path), ops)) {
        case -1:
            return syscall_common_copy_bad_pointer(ops);
        case 0:
            return (uint64_t)(uint32_t)-1;
        default:
            break;
    }
    return fs_service_opendir(proc, vfs, path);
}

uint64_t syscall_common_request_core_readdir_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint32_t fd,
    uint64_t user_entry_addr,
    const struct syscall_common_user_copy_ops *ops) {
    struct syscall_dirent entry;
    int32_t rc;

    if (proc == 0 || vfs == 0 ||
        ops == 0 || ops->copy_to_user == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    rc = (int32_t)fs_service_readdir(proc, vfs, fd, &entry);
    if (rc != 1) {
        return (uint64_t)(int64_t)rc;
    }
    if (!ops->copy_to_user(user_entry_addr, &entry, sizeof(entry))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return 1u;
}

uint64_t syscall_common_request_core_pipe_transfer(
    struct process *proc,
    uint64_t user_pair_addr,
    const struct syscall_common_user_copy_ops *ops) {
    uint32_t pair[2];

    if (proc == 0 || ops == 0 || ops->copy_to_user == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    if (fs_service_pipe(proc, pair) != 0) {
        return (uint64_t)(uint32_t)-1;
    }
    if (!ops->copy_to_user(user_pair_addr, pair, sizeof(pair))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return 0u;
}

uint64_t syscall_common_request_core_dup2_dispatch(
    struct process *proc,
    uint32_t src_fd,
    uint32_t dst_fd) {
    if (proc == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    return fs_service_dup2(proc, src_fd, dst_fd);
}

int syscall_common_request_core_fs_fd_request(
    struct process *proc,
    struct vfs *vfs,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_DUP2:
            result->value = syscall_common_request_core_dup2_dispatch(
                proc,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_PIPE:
            result->value = syscall_common_request_core_pipe_transfer(
                proc, kernel_syscall_arg_u64(request, 0), ops);
            return 1;
        case SYS_GETCWD:
            result->value = syscall_common_request_core_getcwd_transfer(
                proc,
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1),
                ops);
            return 1;
        case SYS_OPENDIR:
            result->value = syscall_common_request_core_opendir_transfer(
                proc, vfs, kernel_syscall_arg_u64(request, 0), ops);
            return 1;
        case SYS_READDIR:
            result->value = syscall_common_request_core_readdir_transfer(
                proc,
                vfs,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                ops);
            return 1;
        default:
            return 0;
    }
}

static int syscall_common_request_core_fd_is_tty_input(
    const struct process *proc,
    uint32_t fd) {
    const struct file *file;

    if (proc == 0) {
        return 0;
    }
    file = file_table_active((struct file *)proc->files, PROCESS_FILE_MAX, fd);
    if (file == 0) {
        return 0;
    }
    if (file->kind == KERNEL_FILE_TTY_STDIN) {
        return 1;
    }
    return file->kind == KERNEL_FILE_VFS &&
           file->vfs_node.mount_kind == VFS_MOUNT_DEVFS &&
           (file->vfs_node.aux_index == VFS_DEV_TTY ||
            file->vfs_node.aux_index == VFS_DEV_TTY1 ||
            file->vfs_node.aux_index == VFS_DEV_TTY2 ||
            file->vfs_node.aux_index == VFS_DEV_TTY3 ||
            file->vfs_node.aux_index == VFS_DEV_STDIN);
}

static struct tty *syscall_common_request_core_fd_tty_input(
    const struct process *proc,
    uint32_t fd) {
    const struct file *file;

    if (proc == 0) {
        return 0;
    }
    file = file_table_active((struct file *)proc->files, PROCESS_FILE_MAX, fd);
    if (file == 0 || !syscall_common_request_core_fd_is_tty_input(proc, fd)) {
        return 0;
    }
    return (struct tty *)file_tty_private_handle(file);
}

uint64_t syscall_common_request_core_open_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_path_addr,
    uint32_t flags,
    const struct syscall_common_user_copy_ops *ops) {
    char path[NOS_PATH_MAX + 1u];

    if (proc == 0 || vfs == 0 || ops == 0 || ops->copy_user_cstr == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    switch (syscall_common_copy_resolved_user_path(
        proc, user_path_addr, path, sizeof(path), ops)) {
        case -1:
            return syscall_common_copy_bad_pointer(ops);
        case 0:
            return (uint64_t)(uint32_t)-1;
        default:
            break;
    }
    return fs_service_open(proc, vfs, path, flags);
}

int syscall_common_request_core_tty_read_transfer(
    uint64_t user_address,
    uint32_t size,
    uint32_t flags,
    struct tty *tty,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_file_io_ops *io_ops,
    uint64_t *result_out) {
    uint32_t count;
    uint32_t mode;

    if (result_out == 0) {
        return 1;
    }
    if (ops == 0 || ops->copy_to_user == 0 ||
        io_ops == 0 || tty == 0 ||
        io_ops->io_buffer == 0 ||
        io_ops->io_buffer_size == 0u ||
        size == 0u || size > io_ops->io_buffer_size) {
        *result_out = (uint64_t)(uint32_t)-1;
        return 1;
    }

    mode = (flags & SYS_READ_CHAR) != 0u ? TTY_READ_CHAR : TTY_READ_LINE;
    tty_set_raw_input(tty, mode == TTY_READ_CHAR);
    if (io_ops->drain_tty_input != 0) {
        io_ops->drain_tty_input(io_ops->ctx, tty);
    }
    if (mode == TTY_READ_LINE && !tty_has_line(tty)) {
        *result_out = (flags & SYS_READ_NONBLOCK) != 0u
            ? 0u
            : (uint64_t)(int64_t)-NEX_ERR_AGAIN;
        return 1;
    }
    count = tty_read(tty, io_ops->io_buffer, size, mode);
    if (count == 0u) {
        *result_out = (flags & SYS_READ_NONBLOCK) != 0u
            ? 0u
            : (uint64_t)(int64_t)-NEX_ERR_AGAIN;
        return 1;
    }
    if (mode == TTY_READ_LINE) {
        if (count == 1u && io_ops->io_buffer[0] == '\0') {
            io_ops->io_buffer[0] = '\n';
            *result_out = ops->copy_to_user(user_address,
                                            io_ops->io_buffer,
                                            1u)
                ? 1u
                : syscall_common_copy_bad_pointer(ops);
            return 1;
        }
        if (count < size) {
            io_ops->io_buffer[count] = '\n';
            count++;
        }
    }
    *result_out = ops->copy_to_user(user_address, io_ops->io_buffer, count)
        ? count
        : syscall_common_copy_bad_pointer(ops);
    return 1;
}

uint64_t syscall_common_request_core_read_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint32_t fd,
    uint64_t user_address,
    uint32_t size,
    uint32_t flags,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_file_io_ops *io_ops) {
    int32_t scheduled;
    struct tty *tty;
    uint64_t tty_result;

    if (proc == 0 || vfs == 0 || ops == 0 || ops->copy_to_user == 0 ||
        io_ops == 0 || io_ops->io_buffer == 0 ||
        io_ops->io_buffer_size == 0u ||
        size == 0u || size > io_ops->io_buffer_size) {
        return (uint64_t)(uint32_t)-1;
    }
    tty = syscall_common_request_core_fd_tty_input(proc, fd);
    if (tty != 0 &&
        syscall_common_request_core_tty_read_transfer(user_address,
                                                     size,
                                                     flags,
                                                     tty,
                                                     ops,
                                                     io_ops,
                                                     &tty_result)) {
        return tty_result;
    }
    scheduled = (int32_t)fs_service_read(proc,
                                         vfs,
                                         fd,
                                         io_ops->io_buffer,
                                         size,
                                         flags,
                                         0);
    if (scheduled <= 0) {
        return (uint64_t)(int64_t)scheduled;
    }
    return ops->copy_to_user(user_address,
                             io_ops->io_buffer,
                             (uint32_t)scheduled)
        ? (uint64_t)(uint32_t)scheduled
        : syscall_common_copy_bad_pointer(ops);
}

uint64_t syscall_common_request_core_write_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint32_t fd,
    uint64_t user_address,
    uint32_t size,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_file_io_ops *io_ops) {
    uint64_t written;

    if (proc == 0 || vfs == 0 || ops == 0 || ops->copy_from_user == 0 ||
        io_ops == 0 || io_ops->io_buffer == 0 ||
        io_ops->io_buffer_size == 0u ||
        size == 0u || size > io_ops->io_buffer_size ||
        !ops->copy_from_user(io_ops->io_buffer, user_address, size)) {
        return (uint64_t)(uint32_t)-1;
    }
    written = fs_service_write(proc, vfs, fd, io_ops->io_buffer, size);
    return written;
}

uint64_t syscall_common_request_core_close_dispatch(
    struct process *proc,
    uint32_t fd) {
    return fs_service_close(proc, fd);
}

uint64_t syscall_common_request_core_seek_dispatch(struct process *proc,
                                                   uint32_t fd,
                                                   int64_t offset,
                                                   uint32_t whence) {
    return (uint64_t)fs_service_seek(proc, fd, offset, whence);
}

int syscall_common_request_core_io_request(
    struct process *proc,
    struct vfs *vfs,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_file_io_ops *io_ops) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_OPEN:
            result->value = syscall_common_request_core_open_transfer(
                proc,
                vfs,
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1),
                ops);
            return 1;
        case SYS_READ:
            result->value = syscall_common_request_core_read_transfer(
                proc,
                vfs,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2),
                kernel_syscall_arg_u32(request, 3),
                ops,
                io_ops);
            if ((int64_t)result->value == -(int64_t)NEX_ERR_AGAIN &&
                (kernel_syscall_arg_u32(request, 3) & SYS_READ_NONBLOCK) == 0u) {
                struct tty *tty = syscall_common_request_core_fd_tty_input(
                    proc, kernel_syscall_arg_u32(request, 0));

                if (tty != 0) {
                    result->action = SYSCALL_RESULT_IO_WAIT;
                    result->extra = (uint64_t)(uintptr_t)tty;
                }
            }
            return 1;
        case SYS_WRITE:
            result->value = syscall_common_request_core_write_transfer(
                proc,
                vfs,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2),
                ops,
                io_ops);
            return 1;
        case SYS_CLOSE:
            result->value = syscall_common_request_core_close_dispatch(
                proc,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SEEK:
            result->value = syscall_common_request_core_seek_dispatch(
                proc,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_i64(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        default:
            return 0;
    }
}

uint64_t syscall_common_request_core_page_alloc_dispatch(
    const struct syscall_common_vm_page_ops *ops) {
    if (ops == 0 || ops->page_alloc == 0) {
        return 0u;
    }
    return ops->page_alloc(ops->ctx);
}

uint64_t syscall_common_request_core_page_free_dispatch(
    const struct syscall_common_vm_page_ops *ops,
    uint64_t user_page) {
    if (ops == 0 || ops->page_free == 0) {
        return (uint64_t)-1;
    }
    return ops->page_free(ops->ctx, user_page);
}

uint64_t syscall_common_request_core_page_protect_dispatch(
    const struct syscall_common_vm_page_ops *ops,
    uint64_t user_page,
    uint32_t writable) {
    if (ops == 0 || ops->page_protect == 0) {
        return (uint64_t)-1;
    }
    return ops->page_protect(ops->ctx, user_page, writable);
}

int syscall_common_request_core_vm_page_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_vm_page_ops *ops) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_PAGE_ALLOC:
            result->value =
                syscall_common_request_core_page_alloc_dispatch(ops);
            return 1;
        case SYS_PAGE_FREE:
            result->value = syscall_common_request_core_page_free_dispatch(
                ops,
                kernel_syscall_arg_u64(request, 0));
            return 1;
        default:
            return 0;
    }
}

static uint64_t syscall_common_vm_page_size(
    const struct syscall_common_vm_page_ops *ops) {
    return ops != 0 && ops->page_size != 0u ? ops->page_size : 4096u;
}

static uint64_t syscall_common_vm_round_length(
    uint64_t length,
    const struct syscall_common_vm_page_ops *ops) {
    uint64_t page_size = syscall_common_vm_page_size(ops);
    uint64_t max_pages = ops != 0 && ops->max_pages != 0u
        ? ops->max_pages
        : 0xffffffffu;

    if (length == 0u ||
        length > max_pages * page_size) {
        return 0u;
    }
    return (length + page_size - 1u) & ~(page_size - 1u);
}

static int syscall_common_vm_range_valid(
    uint64_t addr,
    uint64_t length,
    const struct syscall_common_vm_page_ops *ops) {
    uint64_t page_size = syscall_common_vm_page_size(ops);

    return ops != 0 &&
           addr >= ops->user_mmap_base &&
           addr + length >= addr &&
           addr + length <= ops->user_mmap_end &&
           (addr & (page_size - 1u)) == 0u;
}

uint64_t syscall_common_request_core_mmap_transfer(
    uint64_t user_request_addr,
    const struct syscall_common_user_copy_ops *copy_ops,
    const struct syscall_common_vm_page_ops *page_ops) {
    struct syscall_mmap_request request;
    uint64_t total_length;

    if (copy_ops == 0 || copy_ops->copy_from_user == 0 ||
        page_ops == 0 ||
        !copy_ops->copy_from_user(&request,
                                  user_request_addr,
                                  sizeof(request))) {
        return 0u;
    }
    total_length = syscall_common_vm_round_length(request.length, page_ops);
    if (total_length == 0u ||
        request.offset != 0u ||
        request.prot == 0u ||
        (request.prot & ~(SYS_PROT_READ | SYS_PROT_WRITE)) != 0u) {
        return 0u;
    }
    if (request.shm_handle != 0u &&
        (((request.flags & SYS_MAP_SHARED) == 0u) ||
         ((request.flags & SYS_MAP_ANONYMOUS) != 0u))) {
        return 0u;
    }
    if (request.shm_handle == 0u &&
        ((request.flags & SYS_MAP_ANONYMOUS) == 0u)) {
        return 0u;
    }
    if ((request.flags & SYS_MAP_FIXED) != 0u &&
        !syscall_common_vm_range_valid(request.addr, total_length, page_ops)) {
        return 0u;
    }
    return addrspace_mmap(request.addr,
                          total_length,
                          request.prot,
                          request.flags,
                          request.shm_handle,
                          request.offset);
}

uint64_t syscall_common_request_core_munmap_dispatch(
    uint64_t user_addr,
    uint64_t length,
    const struct syscall_common_vm_page_ops *page_ops) {
    uint64_t rounded_length;
    uint64_t page_size = syscall_common_vm_page_size(page_ops);

    if (page_ops == 0 ||
        user_addr == 0u ||
        length == 0u) {
        return 0u;
    }
    rounded_length = (length + page_size - 1u) & ~(page_size - 1u);
    if (rounded_length < length ||
        (user_addr & (page_size - 1u)) != 0u ||
        user_addr + rounded_length < user_addr) {
        return 0u;
    }
    return addrspace_munmap(user_addr, rounded_length) ? 1u : 0u;
}

uint64_t syscall_common_request_core_mprotect_dispatch(
    uint64_t user_addr,
    uint64_t length,
    uint32_t prot,
    const struct syscall_common_vm_page_ops *page_ops) {
    uint64_t rounded_length;
    uint64_t page_size = syscall_common_vm_page_size(page_ops);
    int writable;

    if (page_ops == 0 || page_ops->page_protect == 0 ||
        user_addr == 0u || length == 0u ||
        prot == 0u ||
        (prot & ~(SYS_PROT_READ | SYS_PROT_WRITE)) != 0u) {
        return 0u;
    }
    rounded_length = (length + page_size - 1u) & ~(page_size - 1u);
    if (rounded_length < length ||
        (user_addr & (page_size - 1u)) != 0u ||
        user_addr + rounded_length < user_addr) {
        return 0u;
    }
    writable = (prot & SYS_PROT_WRITE) != 0u;
    for (uint64_t page = user_addr;
         page < user_addr + rounded_length;
         page += page_size) {
        if (page_ops->page_protect(page_ops->ctx, page, writable) != 0u) {
            return 0u;
        }
    }
    if (!addrspace_note_protect_range(
            user_addr,
            rounded_length,
            writable ? VMM_PERM_WRITE : 0u)) {
        return 0u;
    }
    return 1u;
}

int syscall_common_request_core_mmap_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *copy_ops,
    const struct syscall_common_vm_page_ops *page_ops) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MMAP:
            result->value = syscall_common_request_core_mmap_transfer(
                kernel_syscall_arg_u64(request, 0),
                copy_ops,
                page_ops);
            return 1;
        case SYS_MUNMAP:
            result->value = syscall_common_request_core_munmap_dispatch(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u64(request, 1),
                page_ops);
            return 1;
        case SYS_MPROTECT:
            result->value = syscall_common_request_core_mprotect_dispatch(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2),
                page_ops);
            return 1;
        default:
            return 0;
    }
}

uint64_t syscall_common_request_core_shm_open_transfer(
    uint64_t user_name_addr,
    uint64_t size,
    uint32_t flags,
    const struct syscall_common_user_copy_ops *ops) {
    char name[NOS_NAME_BUFFER_SIZE];

    if (ops == 0 || ops->copy_user_cstr == 0 ||
        !ops->copy_user_cstr(name, user_name_addr, sizeof(name))) {
        return (uint64_t)-1;
    }
    return (uint64_t)(int64_t)addrspace_shm_open(name, size, flags);
}

uint64_t syscall_common_request_core_shm_unlink_transfer(
    uint64_t user_name_addr,
    const struct syscall_common_user_copy_ops *ops) {
    char name[NOS_NAME_BUFFER_SIZE];

    if (ops == 0 || ops->copy_user_cstr == 0 ||
        !ops->copy_user_cstr(name, user_name_addr, sizeof(name))) {
        return 0u;
    }
    return (uint64_t)addrspace_shm_unlink(name);
}

int syscall_common_request_core_shm_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_SHM_OPEN:
            result->value = syscall_common_request_core_shm_open_transfer(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2),
                ops);
            return 1;
        case SYS_SHM_UNLINK:
            result->value = syscall_common_request_core_shm_unlink_transfer(
                kernel_syscall_arg_u64(request, 0),
                ops);
            return 1;
        default:
            return 0;
    }
}

static void syscall_common_mq_destroy_if_unlinked_empty(
    struct syscall_common_message_queue *queue) {
    if (queue != 0 && queue->used && !queue->linked && queue->count == 0u) {
        memset(queue, 0, sizeof(*queue));
    }
}

static struct syscall_common_message_queue *syscall_common_mq_from_handle(
    uint32_t handle) {
    if (handle == 0u || handle > SYSCALL_COMMON_MQ_MAX ||
        !g_syscall_common_mq[handle - 1u].used) {
        return 0;
    }
    return &g_syscall_common_mq[handle - 1u];
}

static uint64_t syscall_common_mq_open_transfer(
    uint64_t user_name_addr,
    uint32_t flags,
    const struct syscall_common_user_copy_ops *ops) {
    char name[SYSCALL_COMMON_IPC_NAME_MAX + 1u];
    uint32_t free_slot = SYSCALL_COMMON_MQ_MAX;

    if (!syscall_common_ipc_copy_name(user_name_addr, name, ops)) {
        return (uint64_t)-1;
    }
    for (uint32_t i = 0u; i < SYSCALL_COMMON_MQ_MAX; i++) {
        if (g_syscall_common_mq[i].used &&
            g_syscall_common_mq[i].linked &&
            syscall_common_ipc_name_equal(g_syscall_common_mq[i].name, name)) {
            if ((flags & SYS_IPC_CREATE) != 0u &&
                (flags & SYS_IPC_EXCL) != 0u) {
                return (uint64_t)-1;
            }
            return i + 1u;
        }
        if (!g_syscall_common_mq[i].used &&
            free_slot == SYSCALL_COMMON_MQ_MAX) {
            free_slot = i;
        }
    }
    if ((flags & SYS_IPC_CREATE) == 0u ||
        free_slot == SYSCALL_COMMON_MQ_MAX) {
        return (uint64_t)-1;
    }
    memset(&g_syscall_common_mq[free_slot],
           0,
           sizeof(g_syscall_common_mq[free_slot]));
    g_syscall_common_mq[free_slot].used = 1u;
    g_syscall_common_mq[free_slot].linked = 1u;
    syscall_common_ipc_copy_name_local(
        g_syscall_common_mq[free_slot].name, name);
    return free_slot + 1u;
}

static uint64_t syscall_common_mq_unlink_transfer(
    uint64_t user_name_addr,
    const struct syscall_common_user_copy_ops *ops) {
    char name[SYSCALL_COMMON_IPC_NAME_MAX + 1u];

    if (!syscall_common_ipc_copy_name(user_name_addr, name, ops)) {
        return 0u;
    }
    for (uint32_t i = 0u; i < SYSCALL_COMMON_MQ_MAX; i++) {
        if (g_syscall_common_mq[i].used &&
            g_syscall_common_mq[i].linked &&
            syscall_common_ipc_name_equal(g_syscall_common_mq[i].name, name)) {
            g_syscall_common_mq[i].linked = 0u;
            g_syscall_common_mq[i].name[0] = '\0';
            syscall_common_mq_destroy_if_unlinked_empty(
                &g_syscall_common_mq[i]);
            return 1u;
        }
    }
    return 0u;
}

static uint64_t syscall_common_mq_send_transfer(
    uint32_t handle,
    uint64_t user_buffer_addr,
    const struct syscall_common_user_copy_ops *ops) {
    struct syscall_mq_buffer buffer;
    struct syscall_common_message_queue *queue =
        syscall_common_mq_from_handle(handle);
    struct syscall_common_ipc_message *message;
    uint32_t tail;

    if (queue == 0 || ops == 0 || ops->copy_from_user == 0 ||
        !ops->copy_from_user(&buffer, user_buffer_addr, sizeof(buffer)) ||
        buffer.size == 0u || buffer.size > SYS_MQ_MESSAGE_MAX) {
        return (uint64_t)-1;
    }
    if (queue->count == SYSCALL_COMMON_MQ_DEPTH) {
        return 0u;
    }
    tail = (queue->head + queue->count) % SYSCALL_COMMON_MQ_DEPTH;
    message = &queue->messages[tail];
    if (!ops->copy_from_user(message->data,
                             buffer.data_addr,
                             buffer.size)) {
        return (uint64_t)-1;
    }
    message->size = (uint16_t)buffer.size;
    queue->count++;
    return buffer.size;
}

static uint64_t syscall_common_mq_receive_transfer(
    uint32_t handle,
    uint64_t user_buffer_addr,
    const struct syscall_common_user_copy_ops *ops) {
    struct syscall_mq_buffer buffer;
    struct syscall_common_message_queue *queue =
        syscall_common_mq_from_handle(handle);
    struct syscall_common_ipc_message *message;
    uint32_t message_size;

    if (queue == 0 || ops == 0 ||
        ops->copy_from_user == 0 || ops->copy_to_user == 0 ||
        !ops->copy_from_user(&buffer, user_buffer_addr, sizeof(buffer)) ||
        buffer.size == 0u) {
        return (uint64_t)-1;
    }
    if (queue->count == 0u) {
        return 0u;
    }
    message = &queue->messages[queue->head];
    if (buffer.size < message->size ||
        !ops->copy_to_user(buffer.data_addr,
                           message->data,
                           message->size)) {
        return (uint64_t)-1;
    }
    message_size = message->size;
    buffer.size = message->size;
    if (!ops->copy_to_user(user_buffer_addr, &buffer, sizeof(buffer))) {
        return (uint64_t)-1;
    }
    queue->head = (queue->head + 1u) % SYSCALL_COMMON_MQ_DEPTH;
    queue->count--;
    syscall_common_mq_destroy_if_unlinked_empty(queue);
    return message_size;
}

static struct syscall_common_semaphore *syscall_common_sem_from_handle(
    uint32_t handle) {
    if (handle == 0u || handle > SYSCALL_COMMON_SEM_MAX ||
        !g_syscall_common_sem[handle - 1u].used) {
        return 0;
    }
    return &g_syscall_common_sem[handle - 1u];
}

static uint64_t syscall_common_sem_open_transfer(
    uint64_t user_name_addr,
    uint32_t initial_value,
    uint32_t flags,
    const struct syscall_common_user_copy_ops *ops) {
    char name[SYSCALL_COMMON_IPC_NAME_MAX + 1u];
    uint32_t free_slot = SYSCALL_COMMON_SEM_MAX;

    if (initial_value > SYSCALL_COMMON_SEM_VALUE_MAX ||
        !syscall_common_ipc_copy_name(user_name_addr, name, ops)) {
        return (uint64_t)-1;
    }
    for (uint32_t i = 0u; i < SYSCALL_COMMON_SEM_MAX; i++) {
        if (g_syscall_common_sem[i].used &&
            g_syscall_common_sem[i].linked &&
            syscall_common_ipc_name_equal(
                g_syscall_common_sem[i].name, name)) {
            if ((flags & SYS_IPC_CREATE) != 0u &&
                (flags & SYS_IPC_EXCL) != 0u) {
                return (uint64_t)-1;
            }
            return i + 1u;
        }
        if (!g_syscall_common_sem[i].used &&
            free_slot == SYSCALL_COMMON_SEM_MAX) {
            free_slot = i;
        }
    }
    if ((flags & SYS_IPC_CREATE) == 0u ||
        free_slot == SYSCALL_COMMON_SEM_MAX) {
        return (uint64_t)-1;
    }
    memset(&g_syscall_common_sem[free_slot],
           0,
           sizeof(g_syscall_common_sem[free_slot]));
    g_syscall_common_sem[free_slot].used = 1u;
    g_syscall_common_sem[free_slot].linked = 1u;
    g_syscall_common_sem[free_slot].value = initial_value;
    syscall_common_ipc_copy_name_local(
        g_syscall_common_sem[free_slot].name, name);
    return free_slot + 1u;
}

static uint64_t syscall_common_sem_unlink_transfer(
    uint64_t user_name_addr,
    const struct syscall_common_user_copy_ops *ops) {
    char name[SYSCALL_COMMON_IPC_NAME_MAX + 1u];

    if (!syscall_common_ipc_copy_name(user_name_addr, name, ops)) {
        return 0u;
    }
    for (uint32_t i = 0u; i < SYSCALL_COMMON_SEM_MAX; i++) {
        if (g_syscall_common_sem[i].used &&
            g_syscall_common_sem[i].linked &&
            syscall_common_ipc_name_equal(
                g_syscall_common_sem[i].name, name)) {
            g_syscall_common_sem[i].linked = 0u;
            g_syscall_common_sem[i].name[0] = '\0';
            return 1u;
        }
    }
    return 0u;
}

static uint64_t syscall_common_sem_trywait_dispatch(uint32_t handle) {
    struct syscall_common_semaphore *sem =
        syscall_common_sem_from_handle(handle);

    if (sem == 0) {
        return (uint64_t)-1;
    }
    if (sem->value == 0u) {
        return 0u;
    }
    sem->value--;
    return 1u;
}

static uint64_t syscall_common_sem_post_dispatch(uint32_t handle) {
    struct syscall_common_semaphore *sem =
        syscall_common_sem_from_handle(handle);

    if (sem == 0 || sem->value == SYSCALL_COMMON_SEM_VALUE_MAX) {
        return 0u;
    }
    sem->value++;
    return 1u;
}

int syscall_common_request_core_ipc_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    if (syscall_common_request_core_shm_request(request, result, ops)) {
        return 1;
    }
    switch (request->number) {
        case SYS_MQ_OPEN:
            result->value = syscall_common_mq_open_transfer(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1),
                ops);
            return 1;
        case SYS_MQ_UNLINK:
            result->value = syscall_common_mq_unlink_transfer(
                kernel_syscall_arg_u64(request, 0),
                ops);
            return 1;
        case SYS_MQ_SEND:
            result->value = syscall_common_mq_send_transfer(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                ops);
            return 1;
        case SYS_MQ_RECEIVE:
            result->value = syscall_common_mq_receive_transfer(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                ops);
            return 1;
        case SYS_SEM_OPEN:
            result->value = syscall_common_sem_open_transfer(
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2),
                ops);
            return 1;
        case SYS_SEM_UNLINK:
            result->value = syscall_common_sem_unlink_transfer(
                kernel_syscall_arg_u64(request, 0),
                ops);
            return 1;
        case SYS_SEM_TRYWAIT:
            result->value = syscall_common_sem_trywait_dispatch(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SEM_POST:
            result->value = syscall_common_sem_post_dispatch(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        default:
            return 0;
    }
}

uint64_t syscall_common_request_core_path_transfer(
    struct vfs *vfs,
    struct process *proc,
    uint32_t number,
    uint64_t user_path_addr,
    const struct syscall_common_user_copy_ops *ops) {
    char path[NOS_PATH_MAX + 1u];
    uint64_t fd;

    if (vfs == 0 || proc == 0 || ops == 0 || ops->copy_user_cstr == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    switch (syscall_common_copy_resolved_user_path(
        proc, user_path_addr, path, sizeof(path), ops)) {
        case -1:
            return syscall_common_copy_bad_pointer(ops);
        case 0:
            return (uint64_t)(uint32_t)-1;
        default:
            break;
    }
    switch (number) {
        case SYS_MKDIR:
            return fs_service_mkdir(proc, vfs, path);
        case SYS_RMDIR:
            return fs_service_rmdir(proc, vfs, path);
        case SYS_REMOVE:
            return fs_service_remove(proc, vfs, path);
        case SYS_MKFIFO:
            return fs_service_mkfifo(vfs, path);
        case SYS_CHDIR:
            fd = fs_service_opendir(proc, vfs, path);
            if ((int64_t)fd < 0) {
                return (uint64_t)(uint32_t)-1;
            }
            (void)fs_service_close(proc, (uint32_t)fd);
            process_set_cwd(proc, path);
            return 0u;
        default:
            return (uint64_t)(uint32_t)-1;
    }
}

static uint64_t syscall_common_request_core_chmod_transfer(
    struct vfs *vfs,
    struct process *proc,
    uint64_t user_path_addr,
    uint32_t mode,
    const struct syscall_common_user_copy_ops *ops) {
    char path[NOS_PATH_MAX + 1u];

    if (vfs == 0 || proc == 0 || ops == 0 || ops->copy_user_cstr == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    switch (syscall_common_copy_resolved_user_path(
        proc, user_path_addr, path, sizeof(path), ops)) {
        case -1:
            return syscall_common_copy_bad_pointer(ops);
        case 0:
            return (uint64_t)(uint32_t)-1;
        default:
            break;
    }
    return fs_service_chmod(proc, vfs, path, mode);
}

static uint64_t syscall_common_request_core_chown_transfer(
    struct vfs *vfs,
    struct process *proc,
    uint64_t user_path_addr,
    uint32_t uid,
    uint32_t gid,
    const struct syscall_common_user_copy_ops *ops) {
    char path[NOS_PATH_MAX + 1u];

    if (vfs == 0 || proc == 0 || ops == 0 || ops->copy_user_cstr == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    switch (syscall_common_copy_resolved_user_path(
        proc, user_path_addr, path, sizeof(path), ops)) {
        case -1:
            return syscall_common_copy_bad_pointer(ops);
        case 0:
            return (uint64_t)(uint32_t)-1;
        default:
            break;
    }
    return fs_service_chown(proc, vfs, path, uid, gid);
}

static uint64_t syscall_common_request_core_setcap_transfer(
    struct vfs *vfs,
    struct process *proc,
    uint64_t user_path_addr,
    uint32_t caps,
    const struct syscall_common_user_copy_ops *ops) {
    char path[NOS_PATH_MAX + 1u];

    if (vfs == 0 || proc == 0 || ops == 0 || ops->copy_user_cstr == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    switch (syscall_common_copy_resolved_user_path(
        proc, user_path_addr, path, sizeof(path), ops)) {
        case -1:
            return syscall_common_copy_bad_pointer(ops);
        case 0:
            return (uint64_t)(uint32_t)-1;
        default:
            break;
    }
    return fs_service_setcap(proc, vfs, path, caps);
}

int syscall_common_request_core_fs_path_request(
    struct process *proc,
    struct vfs *vfs,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    if (request == 0 || result == 0) {
        return 0;
    }
    switch (request->number) {
        case SYS_MKDIR:
        case SYS_RMDIR:
        case SYS_REMOVE:
        case SYS_MKFIFO:
        case SYS_CHDIR:
            result->action = SYSCALL_RESULT_RETURN;
            result->value = syscall_common_request_core_path_transfer(
                vfs,
                proc,
                request->number,
                kernel_syscall_arg_u64(request, 0),
                ops);
            return 1;
        case SYS_CHMOD:
            result->action = SYSCALL_RESULT_RETURN;
            result->value = syscall_common_request_core_chmod_transfer(
                vfs,
                proc,
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1),
                ops);
            return 1;
        case SYS_CHOWN:
            result->action = SYSCALL_RESULT_RETURN;
            result->value = syscall_common_request_core_chown_transfer(
                vfs,
                proc,
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2),
                ops);
            return 1;
        case SYS_SETCAP:
            result->action = SYSCALL_RESULT_RETURN;
            result->value = syscall_common_request_core_setcap_transfer(
                vfs,
                proc,
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u32(request, 1),
                ops);
            return 1;
        default:
            return 0;
    }
}

static int syscall_common_mount_source_is_boot(const char *source) {
    return streq(source, "boot") || streq(source, "/dev/boot");
}

uint64_t syscall_common_request_core_mount_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_source_addr,
    uint64_t user_target_addr,
    uint32_t kind,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_mount_ops *mount_ops) {
    char source[NOS_PATH_MAX + 1u];
    char target[NOS_PATH_MAX + 1u];
    int boot_source;

    if (proc == 0 || vfs == 0 || ops == 0 || ops->copy_user_cstr == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    if (!ops->copy_user_cstr(source, user_source_addr, sizeof(source))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    switch (syscall_common_copy_resolved_user_path(
        proc, user_target_addr, target, sizeof(target), ops)) {
        case -1:
            return syscall_common_copy_bad_pointer(ops);
        case 0:
            return (uint64_t)(uint32_t)-1;
        default:
            break;
    }
    boot_source = syscall_common_mount_source_is_boot(source);
    if (boot_source) {
        if (mount_ops == 0 || !mount_ops->has_boot_info) {
            return (uint64_t)(-(int64_t)SYS_MOUNT_ERR_INVALID_SOURCE);
        }
        return fs_service_mount_boot(vfs,
                                     target,
                                     kind,
                                     mount_ops->boot_partition_lba,
                                     mount_ops->boot_partition_sectors);
    }
    return fs_service_mount(vfs, source, target, kind);
}

uint64_t syscall_common_request_core_umount_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_target_addr,
    const struct syscall_common_user_copy_ops *ops) {
    char target[NOS_PATH_MAX + 1u];

    if (proc == 0 || vfs == 0 || ops == 0 || ops->copy_user_cstr == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    switch (syscall_common_copy_resolved_user_path(
        proc, user_target_addr, target, sizeof(target), ops)) {
        case -1:
            return syscall_common_copy_bad_pointer(ops);
        case 0:
            return (uint64_t)(uint32_t)-1;
        default:
            break;
    }
    return fs_service_umount(vfs, target);
}

uint64_t syscall_common_request_core_switch_root_transfer(
    struct process *proc,
    struct vfs *vfs,
    uint64_t user_target_addr,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_mount_ops *mount_ops) {
    char target[NOS_PATH_MAX + 1u];

    if (proc == 0 || vfs == 0 || ops == 0 || ops->copy_user_cstr == 0 ||
        mount_ops == 0 || mount_ops->switch_root == 0) {
        return (uint64_t)(uint32_t)-1;
    }
    switch (syscall_common_copy_resolved_user_path(
        proc, user_target_addr, target, sizeof(target), ops)) {
        case -1:
            return syscall_common_copy_bad_pointer(ops);
        case 0:
            return (uint64_t)(uint32_t)-1;
        default:
            break;
    }
    return mount_ops->switch_root(mount_ops->ctx, vfs, target);
}

int syscall_common_request_core_mount_request(
    struct process *proc,
    struct vfs *vfs,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    const struct syscall_common_mount_ops *mount_ops) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MOUNT:
            if (!syscall_common_current_has_capability(PROCESS_CAP_MOUNT)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_mount_transfer(
                proc,
                vfs,
                kernel_syscall_arg_u64(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2),
                ops,
                mount_ops);
            return 1;
        case SYS_UMOUNT:
            if (!syscall_common_current_has_capability(PROCESS_CAP_MOUNT)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_umount_transfer(
                proc,
                vfs,
                kernel_syscall_arg_u64(request, 0),
                ops);
            return 1;
        case SYS_SWITCH_ROOT:
            if (!syscall_common_current_has_capability(PROCESS_CAP_MOUNT)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_switch_root_transfer(
                proc,
                vfs,
                kernel_syscall_arg_u64(request, 0),
                ops,
                mount_ops);
            return 1;
        default:
            return 0;
    }
}

int syscall_common_request_core_gfx_info(uint32_t op,
                                         struct syscall_gfx_info *info) {
    if (op != SYS_GFX_INFO || info == 0) {
        return 0;
    }
    kernel_gfx_info(info);
    return 1;
}

int syscall_common_request_core_gfx_command(
    uint32_t op,
    const struct syscall_gfx_command *cmd) {
    enum kernel_gfx_buffer_kind buffer_kind = kernel_gfx_buffer_kind(op);

    if (buffer_kind != KERNEL_GFX_BUFFER_COMMAND_IN || cmd == 0) {
        return 0;
    }
    return kernel_gfx_dispatch(op, cmd, 0);
}

int syscall_common_request_core_gfx_batch_valid(
    const struct syscall_gfx_batch *batch) {
    if (batch == 0 ||
        batch->count > SYS_GFX_BATCH_MAX_COMMANDS ||
        (batch->flags & ~SYS_GFX_BATCH_PRESENT) != 0u ||
        (batch->count != 0u && batch->entries_addr == 0u) ||
        batch->count > 0xffffffffu / sizeof(struct syscall_gfx_batch_entry)) {
        return 0;
    }
    return 1;
}

int syscall_common_request_core_gfx_batch_dispatch(
    const struct syscall_gfx_batch_entry *entries,
    uint32_t count,
    int allow_blit) {
    if (entries == 0 && count != 0u) {
        return 0;
    }
    for (uint32_t i = 0u; i < count; i++) {
        const struct syscall_gfx_batch_entry *entry = &entries[i];

        if (entry->reserved != 0u ||
            entry->op == SYS_GFX_INFO ||
            entry->op == SYS_GFX_BATCH ||
            entry->op == SYS_GFX_PRESENT ||
            (!allow_blit && entry->op == SYS_GFX_BLIT) ||
            !syscall_common_request_core_gfx_command(entry->op,
                                                     &entry->command)) {
            return 0;
        }
    }
    return 1;
}

int syscall_common_request_core_gfx_blit_plan(
    const struct syscall_gfx_blit *blit,
    uint32_t max_dimension,
    uint64_t max_user_addr,
    struct syscall_common_gfx_blit_plan *plan,
    int *noop) {
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t src_x = 0u;
    uint32_t src_y = 0u;
    uint32_t visible_width;
    uint32_t visible_height;
    uint64_t first_addr;
    uint64_t span;
    int32_t dst_x;
    int32_t dst_y;

    if (noop != 0) {
        *noop = 0;
    }
    if (plan != 0) {
        memset(plan, 0, sizeof(*plan));
    }
    if (blit == 0 || plan == 0 ||
        blit->pixels_addr == 0u ||
        blit->width == 0u || blit->height == 0u ||
        blit->width > max_dimension ||
        blit->height > max_dimension ||
        blit->width > 0xffffffffu / sizeof(uint32_t) ||
        blit->pitch < blit->width * sizeof(uint32_t) ||
        blit->format != SYS_GFX_FORMAT_XRGB8888 ||
        blit->flags != 0u ||
        blit->pixels_addr > max_user_addr) {
        return 0;
    }

    kernel_gfx_dimensions(&screen_width, &screen_height);
    if (screen_width == 0u || screen_height == 0u) {
        return 0;
    }
    dst_x = blit->dst_x;
    dst_y = blit->dst_y;
    visible_width = blit->width;
    visible_height = blit->height;
    if (dst_x < 0) {
        uint32_t crop = (uint32_t)(-(int64_t)dst_x);

        if (crop >= visible_width) {
            if (noop != 0) {
                *noop = 1;
            }
            return 1;
        }
        src_x = crop;
        visible_width -= crop;
        dst_x = 0;
    }
    if (dst_y < 0) {
        uint32_t crop = (uint32_t)(-(int64_t)dst_y);

        if (crop >= visible_height) {
            if (noop != 0) {
                *noop = 1;
            }
            return 1;
        }
        src_y = crop;
        visible_height -= crop;
        dst_y = 0;
    }
    if ((uint32_t)dst_x >= screen_width || (uint32_t)dst_y >= screen_height) {
        if (noop != 0) {
            *noop = 1;
        }
        return 1;
    }
    if (visible_width > screen_width - (uint32_t)dst_x) {
        visible_width = screen_width - (uint32_t)dst_x;
    }
    if (visible_height > screen_height - (uint32_t)dst_y) {
        visible_height = screen_height - (uint32_t)dst_y;
    }

    first_addr = blit->pixels_addr +
                 (uint64_t)src_y * blit->pitch +
                 (uint64_t)src_x * sizeof(uint32_t);
    if (first_addr < blit->pixels_addr || first_addr > max_user_addr) {
        return 0;
    }
    span = (uint64_t)(visible_height - 1u) * blit->pitch +
           (uint64_t)visible_width * sizeof(uint32_t);
    if (span == 0u || span > 0xffffffffu ||
        first_addr + span < first_addr ||
        first_addr + span - 1u > max_user_addr) {
        return 0;
    }

    plan->first_addr = first_addr;
    plan->span = span;
    plan->src_x = src_x;
    plan->src_y = src_y;
    plan->visible_width = visible_width;
    plan->visible_height = visible_height;
    plan->dst_x = dst_x;
    plan->dst_y = dst_y;
    plan->pitch = blit->pitch;
    return 1;
}

int syscall_common_request_core_gfx_blit_dispatch(
    const uint32_t *pixels,
    uint32_t pitch,
    uint32_t width,
    uint32_t height,
    int32_t dst_x,
    int32_t dst_y) {
    return kernel_gfx_blit_xrgb8888(pixels, pitch, width, height, dst_x, dst_y);
}

uint64_t syscall_common_request_core_gfx_batch_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    struct syscall_gfx_batch_entry *scratch,
    uint32_t scratch_count,
    int allow_blit) {
    struct syscall_gfx_batch batch;
    uint32_t processed = 0u;
    int valid = 1;

    if (ops == 0 || ops->copy_from_user == 0 ||
        scratch == 0 || scratch_count == 0u ||
        !ops->copy_from_user(&batch, user_info_addr, sizeof(batch))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    if (!syscall_common_request_core_gfx_batch_valid(&batch)) {
        return (uint64_t)-1;
    }

    kernel_gfx_begin_batch();
    while (processed < batch.count) {
        uint32_t count = batch.count - processed;
        uint32_t bytes;
        uint64_t entries_addr;

        if (count > scratch_count) {
            count = scratch_count;
        }
        bytes = count * sizeof(struct syscall_gfx_batch_entry);
        entries_addr = batch.entries_addr +
                       (uint64_t)processed * sizeof(struct syscall_gfx_batch_entry);
        if (entries_addr < batch.entries_addr ||
            !ops->copy_from_user(scratch, entries_addr, bytes)) {
            kernel_gfx_end_batch(0u);
            return syscall_common_copy_bad_pointer(ops);
        }
        valid = syscall_common_request_core_gfx_batch_dispatch(
            scratch, count, allow_blit);
        if (!valid) {
            break;
        }
        processed += count;
    }
    kernel_gfx_end_batch(valid ? batch.flags : 0u);
    return valid ? 0u : (uint64_t)-1;
}

uint64_t syscall_common_request_core_gfx_blit_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint32_t max_dimension,
    uint64_t max_user_addr) {
    struct syscall_gfx_blit blit;
    struct syscall_common_gfx_blit_plan plan;
    int noop = 0;

    if (ops == 0 || ops->copy_from_user == 0 ||
        scratch == 0 || scratch_size < sizeof(uint32_t) ||
        !ops->copy_from_user(&blit, user_info_addr, sizeof(blit))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    if (!syscall_common_request_core_gfx_blit_plan(
            &blit,
            max_dimension,
            max_user_addr,
            &plan,
            &noop)) {
        return (uint64_t)-1;
    }
    if (noop) {
        return 0u;
    }

    for (uint32_t x = 0u; x < plan.visible_width;) {
        uint32_t chunk_width = plan.visible_width - x;
        uint32_t row_bytes;
        uint32_t rows_per_chunk;

        if (chunk_width > scratch_size / sizeof(uint32_t)) {
            chunk_width = scratch_size / sizeof(uint32_t);
        }
        row_bytes = chunk_width * sizeof(uint32_t);
        rows_per_chunk = scratch_size / row_bytes;
        for (uint32_t y = 0u; y < plan.visible_height;) {
            uint32_t row_count = plan.visible_height - y;

            if (row_count > rows_per_chunk) {
                row_count = rows_per_chunk;
            }
            for (uint32_t row = 0u; row < row_count; row++) {
                uint64_t row_addr = plan.first_addr +
                                    (uint64_t)(y + row) * plan.pitch +
                                    (uint64_t)x * sizeof(uint32_t);

                if (row_addr < plan.first_addr ||
                    !ops->copy_from_user(&scratch[row * row_bytes],
                                         row_addr,
                                         row_bytes)) {
                    return syscall_common_copy_bad_pointer(ops);
                }
            }
            if (!syscall_common_request_core_gfx_blit_dispatch(
                    (const uint32_t *)scratch,
                    row_bytes,
                    chunk_width,
                    row_count,
                    plan.dst_x + (int32_t)x,
                    plan.dst_y + (int32_t)y)) {
                return (uint64_t)-1;
            }
            y += row_count;
        }
        x += chunk_width;
    }
    return 0u;
}

uint64_t syscall_common_request_core_gfx_transfer(
    uint32_t op,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    struct syscall_gfx_batch_entry *batch_scratch,
    uint32_t batch_scratch_count,
    uint8_t *blit_scratch,
    uint32_t blit_scratch_size,
    uint32_t max_dimension,
    uint64_t max_user_addr,
    int allow_blit) {
    struct syscall_gfx_command cmd;
    struct syscall_gfx_info info;
    enum kernel_gfx_buffer_kind buffer_kind;

    if (op == SYS_GFX_BATCH) {
        return syscall_common_request_core_gfx_batch_transfer(
            user_info_addr,
            ops,
            batch_scratch,
            batch_scratch_count,
            allow_blit);
    }
    if (op == SYS_GFX_BLIT) {
        return syscall_common_request_core_gfx_blit_transfer(
            user_info_addr,
            ops,
            blit_scratch,
            blit_scratch_size,
            max_dimension,
            max_user_addr);
    }

    if (ops == 0 || ops->copy_from_user == 0 || ops->copy_to_user == 0) {
        return syscall_common_copy_bad_pointer(ops);
    }
    buffer_kind = kernel_gfx_buffer_kind(op);
    switch (buffer_kind) {
        case KERNEL_GFX_BUFFER_INFO_OUT:
            memset(&info, 0, sizeof(info));
            if (!syscall_common_request_core_gfx_info(op, &info)) {
                return (uint64_t)-1;
            }
            return ops->copy_to_user(user_info_addr, &info, sizeof(info))
                ? 0u
                : syscall_common_copy_bad_pointer(ops);
        case KERNEL_GFX_BUFFER_COMMAND_IN:
            if (!ops->copy_from_user(&cmd, user_info_addr, sizeof(cmd))) {
                return syscall_common_copy_bad_pointer(ops);
            }
            return syscall_common_request_core_gfx_command(op, &cmd)
                ? 0u
                : (uint64_t)-1;
        case KERNEL_GFX_BUFFER_INVALID:
        default:
            return (uint64_t)-1;
    }
}

int syscall_common_request_core_gfx_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    struct syscall_gfx_batch_entry *batch_scratch,
    uint32_t batch_scratch_count,
    uint8_t *blit_scratch,
    uint32_t blit_scratch_size,
    uint32_t max_dimension,
    uint64_t max_user_addr,
    int allow_blit) {
    if (request == 0 || result == 0 || request->number != SYS_GFX) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    if (kernel_syscall_arg_u32(request, 0) != SYS_GFX_INFO &&
        !syscall_common_current_has_capability(PROCESS_CAP_DISPLAY)) {
        result->value = syscall_common_access_denied();
        return 1;
    }
    result->value = syscall_common_request_core_gfx_transfer(
        kernel_syscall_arg_u32(request, 0),
        kernel_syscall_arg_u64(request, 1),
        ops,
        batch_scratch,
        batch_scratch_count,
        blit_scratch,
        blit_scratch_size,
        max_dimension,
        max_user_addr,
        allow_blit);
    return 1;
}

int syscall_common_request_core_clipboard(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (request == 0 || result == 0 || request->number != SYS_CLIPBOARD) {
        return 0;
    }
    if (kernel_syscall_arg_u32(request, 0) != SYS_CLIPBOARD_CLEAR) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    if (!syscall_common_current_has_capability(PROCESS_CAP_CLIPBOARD)) {
        result->value = syscall_common_access_denied();
        return 1;
    }
    result->value = syscall_common_request_core_clipboard_set_text("", 0u);
    return 1;
}

uint32_t syscall_common_request_core_clipboard_size(void) {
    return kernel_clipboard_size();
}

const char *syscall_common_request_core_clipboard_text(void) {
    return kernel_clipboard_text();
}

uint32_t syscall_common_request_core_clipboard_copy_size(uint32_t requested) {
    uint32_t size = kernel_clipboard_size();

    return size < requested ? size : requested;
}

uint32_t syscall_common_request_core_clipboard_set_text(const char *text,
                                                       uint32_t bytes) {
    if (text == 0) {
        bytes = 0u;
    }
    if (bytes > KERNEL_CLIPBOARD_TEXT_MAX) {
        bytes = KERNEL_CLIPBOARD_TEXT_MAX;
    }
    return kernel_clipboard_set_text(text == 0 ? "" : text, bytes);
}

uint32_t syscall_common_request_core_clipboard_prepare_get(
    struct syscall_clipboard_transfer *transfer) {
    uint32_t copied;

    if (transfer == 0) {
        return 0u;
    }
    copied = syscall_common_request_core_clipboard_copy_size(transfer->bytes);
    transfer->size = syscall_common_request_core_clipboard_size();
    return copied;
}

uint32_t syscall_common_request_core_clipboard_prepare_set(
    const struct syscall_clipboard_transfer *transfer) {
    if (transfer == 0) {
        return 0u;
    }
    return transfer->bytes < KERNEL_CLIPBOARD_TEXT_MAX
        ? transfer->bytes
        : KERNEL_CLIPBOARD_TEXT_MAX;
}

uint32_t syscall_common_request_core_clipboard_commit_set(
    struct syscall_clipboard_transfer *transfer,
    const char *text,
    uint32_t bytes) {
    uint32_t size;

    size = syscall_common_request_core_clipboard_set_text(text, bytes);
    if (transfer != 0) {
        transfer->size = size;
    }
    return size;
}

uint32_t syscall_common_request_core_clipboard_prepare_size(
    struct syscall_clipboard_transfer *transfer) {
    uint32_t size = syscall_common_request_core_clipboard_size();

    if (transfer != 0) {
        transfer->size = size;
    }
    return size;
}

static uint64_t syscall_common_clipboard_bad_pointer(
    const struct syscall_common_clipboard_transfer_ops *ops) {
    if (ops == 0) {
        return (uint64_t)-1;
    }
    if (ops->bad_pointer != 0) {
        return ops->bad_pointer();
    }
    return ops->bad_pointer_value;
}

uint64_t syscall_common_request_core_clipboard_transfer(
    uint32_t op,
    uint64_t user_info_addr,
    const struct syscall_common_clipboard_transfer_ops *ops,
    char *scratch,
    uint32_t scratch_size) {
    struct syscall_clipboard_transfer transfer;
    uint32_t bytes;

    if (op == SYS_CLIPBOARD_CLEAR) {
        return syscall_common_request_core_clipboard_set_text("", 0u);
    }
    if (ops == 0 || ops->copy_from_user == 0 || ops->copy_to_user == 0 ||
        !ops->copy_from_user(&transfer, user_info_addr, sizeof(transfer))) {
        return syscall_common_clipboard_bad_pointer(ops);
    }

    switch (op) {
        case SYS_CLIPBOARD_GET:
            bytes = syscall_common_request_core_clipboard_prepare_get(
                &transfer);
            if (bytes != 0u &&
                !ops->copy_to_user(transfer.data_addr,
                                   syscall_common_request_core_clipboard_text(),
                                   bytes)) {
                return syscall_common_clipboard_bad_pointer(ops);
            }
            return ops->copy_to_user(user_info_addr, &transfer, sizeof(transfer))
                ? bytes
                : syscall_common_clipboard_bad_pointer(ops);
        case SYS_CLIPBOARD_SET:
            bytes = syscall_common_request_core_clipboard_prepare_set(
                &transfer);
            if (bytes >= scratch_size || scratch == 0) {
                bytes = scratch_size == 0u ? 0u : scratch_size - 1u;
            }
            if (bytes != 0u &&
                !ops->copy_from_user(scratch, transfer.data_addr, bytes)) {
                return syscall_common_clipboard_bad_pointer(ops);
            }
            if (scratch != 0) {
                scratch[bytes] = '\0';
            }
            (void)syscall_common_request_core_clipboard_commit_set(
                &transfer, scratch, bytes);
            return ops->copy_to_user(user_info_addr, &transfer, sizeof(transfer))
                ? transfer.size
                : syscall_common_clipboard_bad_pointer(ops);
        case SYS_CLIPBOARD_SIZE:
            (void)syscall_common_request_core_clipboard_prepare_size(&transfer);
            return ops->copy_to_user(user_info_addr, &transfer, sizeof(transfer))
                ? transfer.size
                : syscall_common_clipboard_bad_pointer(ops);
        default:
            return (uint64_t)-1;
    }
}

int syscall_common_request_core_clipboard_transfer_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_clipboard_transfer_ops *ops,
    char *scratch,
    uint32_t scratch_size) {
    if (request == 0 || result == 0 || request->number != SYS_CLIPBOARD) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    if (!syscall_common_current_has_capability(PROCESS_CAP_CLIPBOARD)) {
        result->value = syscall_common_access_denied();
        return 1;
    }
    result->value = syscall_common_request_core_clipboard_transfer(
        kernel_syscall_arg_u32(request, 0),
        kernel_syscall_arg_u64(request, 1),
        ops,
        scratch,
        scratch_size);
    return 1;
}

struct syscall_common_audio_buffer_call {
    uint32_t index;
    const struct syscall_audio_play_info *info;
    const uint8_t *buffer;
};

struct syscall_common_audio_stream_call {
    uint32_t index;
    struct audio_pcm_stream *stream;
};

static int syscall_common_audio_play_buffer_local(void *ctx) {
    const struct syscall_common_audio_buffer_call *call =
        (const struct syscall_common_audio_buffer_call *)ctx;

    return kernel_audio_play_buffer(call->index, call->info, call->buffer);
}

static int syscall_common_audio_play_stream_local(void *ctx) {
    const struct syscall_common_audio_stream_call *call =
        (const struct syscall_common_audio_stream_call *)ctx;

    return kernel_audio_play_stream(call->index, call->stream);
}

int syscall_common_request_core_audio_play_valid(
    const struct syscall_audio_play_info *info,
    uint32_t max_bytes) {
    if (info == 0 ||
        info->bytes == 0u ||
        info->bytes > max_bytes ||
        (info->flags & ~SYS_AUDIO_PLAY_F_ASYNC) != 0u ||
        info->channels == 0u ||
        info->bits_per_sample == 0u ||
        info->sample_rate == 0u) {
        return 0;
    }
    return 1;
}

uint64_t syscall_common_request_core_audio_play_dispatch(
    uint32_t index,
    const struct syscall_audio_play_info *info,
    const uint8_t *buffer) {
    struct syscall_common_audio_buffer_call call;

    if (info == 0 || buffer == 0) {
        return 0u;
    }
    call.index = index;
    call.info = info;
    call.buffer = buffer;
    return kernel_runtime_run_with_irqs_enabled(
        syscall_common_audio_play_buffer_local, &call) ? 1u : 0u;
}

uint64_t syscall_common_request_core_audio_play_transfer(
    uint32_t index,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr) {
    struct syscall_audio_play_info info;

    if (ops == 0 || ops->copy_from_user == 0 || scratch == 0 ||
        !ops->copy_from_user(&info, user_info_addr, sizeof(info))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    if (!syscall_common_request_core_audio_play_valid(&info, scratch_size)) {
        return 0u;
    }
    if (info.data_addr > max_user_addr ||
        !ops->copy_from_user(scratch, info.data_addr, info.bytes)) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return syscall_common_request_core_audio_play_dispatch(
        index, &info, scratch);
}

int syscall_common_request_core_audio_play_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr) {
    if (request == 0 || result == 0 || request->number != SYS_AUDIO_PLAY) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    if (!syscall_common_current_has_capability(PROCESS_CAP_AUDIO)) {
        result->value = syscall_common_access_denied();
        return 1;
    }
    result->value = syscall_common_request_core_audio_play_transfer(
        kernel_syscall_arg_u32(request, 0),
        kernel_syscall_arg_u64(request, 1),
        ops,
        scratch,
        scratch_size,
        max_user_addr);
    return 1;
}

int syscall_common_request_core_audio_stream_valid(
    const struct syscall_audio_stream_info *info) {
    if (info == 0 ||
        info->data_bytes == 0u ||
        info->channels == 0u ||
        info->bits_per_sample == 0u ||
        info->sample_rate == 0u ||
        (info->flags & ~SYS_AUDIO_PLAY_F_ASYNC) != 0u) {
        return 0;
    }
    return 1;
}

void syscall_common_request_core_audio_stream_init(
    struct audio_pcm_stream *stream,
    const struct syscall_audio_stream_info *info,
    void *ctx,
    uint32_t (*read)(void *ctx, void *buffer, uint32_t bytes),
    uint32_t (*cancelled)(void *ctx)) {
    if (stream == 0 || info == 0) {
        return;
    }
    stream->sample_rate = info->sample_rate;
    stream->channels = info->channels;
    stream->bits_per_sample = info->bits_per_sample;
    stream->data_bytes = info->data_bytes;
    stream->flags = info->flags;
    stream->ctx = ctx;
    stream->read = read;
    stream->cancelled = cancelled;
}

uint64_t syscall_common_request_core_audio_stream_dispatch(
    uint32_t index,
    struct audio_pcm_stream *stream) {
    struct syscall_common_audio_stream_call call;

    if (stream == 0 || stream->read == 0) {
        return 0u;
    }
    call.index = index;
    call.stream = stream;
    return kernel_runtime_run_with_irqs_enabled(
        syscall_common_audio_play_stream_local, &call) ? 1u : 0u;
}

uint64_t syscall_common_request_core_audio_stream_transfer(
    uint32_t index,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    void *ctx,
    void (*prepare)(void *ctx, const struct syscall_audio_stream_info *info),
    uint32_t (*read)(void *ctx, void *buffer, uint32_t bytes),
    uint32_t (*cancelled)(void *ctx)) {
    struct syscall_audio_stream_info info;
    struct audio_pcm_stream stream;

    if (ops == 0 || ops->copy_from_user == 0 ||
        !ops->copy_from_user(&info, user_info_addr, sizeof(info))) {
        return (uint64_t)-1;
    }
    if (!syscall_common_request_core_audio_stream_valid(&info)) {
        return 0u;
    }
    if (prepare != 0) {
        prepare(ctx, &info);
    }
    syscall_common_request_core_audio_stream_init(
        &stream, &info, ctx, read, cancelled);
    return syscall_common_request_core_audio_stream_dispatch(index, &stream);
}

int syscall_common_request_core_audio_stream_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    void *ctx,
    void (*prepare)(void *ctx, const struct syscall_audio_stream_info *info),
    uint32_t (*read)(void *ctx, void *buffer, uint32_t bytes),
    uint32_t (*cancelled)(void *ctx)) {
    if (request == 0 || result == 0 ||
        request->number != SYS_AUDIO_PLAY_FD) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    if (!syscall_common_current_has_capability(PROCESS_CAP_AUDIO)) {
        result->value = syscall_common_access_denied();
        return 1;
    }
    result->value = syscall_common_request_core_audio_stream_transfer(
        kernel_syscall_arg_u32(request, 0),
        kernel_syscall_arg_u64(request, 1),
        ops,
        ctx,
        prepare,
        read,
        cancelled);
    return 1;
}

int syscall_common_request_core_rtl8139_tx_valid(
    const struct syscall_rtl8139_tx_info *info,
    uint32_t max_bytes) {
    if (info == 0 || info->bytes < 14u || info->bytes > max_bytes) {
        return 0;
    }
    return 1;
}

uint64_t syscall_common_request_core_rtl8139_tx_dispatch(
    const uint8_t *frame,
    uint32_t bytes) {
    if (frame == 0 || bytes < 14u) {
        return 0u;
    }
    return kernel_rtl8139_send_frame(frame, bytes) ? 1u : 0u;
}

uint64_t syscall_common_request_core_rtl8139_rx_dispatch(
    struct syscall_rtl8139_rx_info *info) {
    if (info == 0) {
        return 0u;
    }
    return kernel_rtl8139_receive_packet(info) ? 1u : 0u;
}

uint64_t syscall_common_request_core_rtl8139_tx_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr) {
    struct syscall_rtl8139_tx_info info;

    if (ops == 0 || ops->copy_from_user == 0 || scratch == 0 ||
        !ops->copy_from_user(&info, user_info_addr, sizeof(info))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    if (!syscall_common_request_core_rtl8139_tx_valid(&info, scratch_size)) {
        return 0u;
    }
    if (info.data_addr > max_user_addr ||
        !ops->copy_from_user(scratch, info.data_addr, info.bytes)) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return syscall_common_request_core_rtl8139_tx_dispatch(
        scratch, info.bytes);
}

uint64_t syscall_common_request_core_rtl8139_rx_transfer(
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops) {
    struct syscall_rtl8139_rx_info info;
    uint64_t rc;

    if (ops == 0 || ops->copy_to_user == 0) {
        return syscall_common_copy_bad_pointer(ops);
    }
    memset(&info, 0, sizeof(info));
    rc = syscall_common_request_core_rtl8139_rx_dispatch(&info);
    if (!ops->copy_to_user(user_info_addr, &info, sizeof(info))) {
        return syscall_common_copy_bad_pointer(ops);
    }
    return rc;
}

int syscall_common_request_core_rtl8139_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    uint8_t *scratch,
    uint32_t scratch_size,
    uint64_t max_user_addr) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_RTL8139_TX_SEND:
            if (!syscall_common_current_has_capability(PROCESS_CAP_NET_RAW)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_rtl8139_tx_transfer(
                kernel_syscall_arg_u64(request, 0),
                ops,
                scratch,
                scratch_size,
                max_user_addr);
            return 1;
        case SYS_RTL8139_RX_DUMP:
            if (!syscall_common_current_has_capability(PROCESS_CAP_NET_RAW)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_rtl8139_rx_transfer(
                kernel_syscall_arg_u64(request, 0), ops);
            return 1;
        default:
            return 0;
    }
}

int syscall_common_request_core_block_read_dispatch(
    uint32_t disk_index,
    uint64_t lba,
    struct syscall_block_read_info *info) {
    if (info == 0) {
        return 0;
    }
    return kernel_block_read(disk_index, lba, info);
}

int syscall_common_request_core_block_write_dispatch(
    uint32_t disk_index,
    uint64_t lba,
    struct syscall_block_write_info *info) {
    if (info == 0) {
        return 0;
    }
    return kernel_block_write(disk_index, lba, info);
}

uint64_t syscall_common_request_core_block_flush_dispatch(uint32_t disk_index) {
    return kernel_block_flush(disk_index) ? 1u : 0u;
}

uint64_t syscall_common_request_core_block_read_transfer(
    uint32_t disk_index,
    uint64_t lba,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops) {
    struct syscall_block_read_info info;

    if (ops == 0 || ops->copy_to_user == 0) {
        return 0u;
    }
    if (!syscall_common_request_core_block_read_dispatch(
            disk_index, lba, &info)) {
        return 0u;
    }
    return ops->copy_to_user(user_info_addr, &info, sizeof(info)) ? 1u : 0u;
}

uint64_t syscall_common_request_core_block_write_transfer(
    uint32_t disk_index,
    uint64_t lba,
    uint64_t user_info_addr,
    const struct syscall_common_user_copy_ops *ops) {
    struct syscall_block_write_info info;

    if (ops == 0 || ops->copy_from_user == 0 || ops->copy_to_user == 0 ||
        !ops->copy_from_user(&info, user_info_addr, sizeof(info))) {
        return 0u;
    }
    if (!syscall_common_request_core_block_write_dispatch(
            disk_index, lba, &info)) {
        (void)ops->copy_to_user(user_info_addr, &info, sizeof(info));
        return 0u;
    }
    return ops->copy_to_user(user_info_addr, &info, sizeof(info)) ? 1u : 0u;
}

int syscall_common_request_core_block_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_BLOCK_READ:
            if (!syscall_common_current_has_capability(PROCESS_CAP_RAW_BLOCK)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_block_read_transfer(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u64(request, 2),
                ops);
            return 1;
        case SYS_BLOCK_WRITE:
            if (!syscall_common_current_has_capability(PROCESS_CAP_RAW_BLOCK)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_block_write_transfer(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u64(request, 2),
                ops);
            return 1;
        case SYS_BLOCK_FLUSH:
            if (!syscall_common_current_has_capability(PROCESS_CAP_RAW_BLOCK)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_block_flush_dispatch(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        default:
            return 0;
    }
}

static void syscall_common_gui_event_from_keyboard(
    struct syscall_gui_event *event,
    const struct keyboard_event_record *record) {
    event->type = SYS_GUI_EVENT_KEY;
    event->seq = record->seq;
    event->tick = record->tick;
    event->dx = 0;
    event->dy = 0;
    event->buttons = 0u;
    event->keycode = (uint32_t)record->event.keycode;
    event->ascii = record->event.ascii;
    event->pressed = record->event.pressed;
    event->released = record->event.released;
    event->shift = record->event.shift;
    event->ctrl = record->event.ctrl;
    event->alt = record->event.alt;
}

static void syscall_common_gui_event_from_mouse(
    struct syscall_gui_event *event,
    const struct mouse_event_record *record) {
    event->type = SYS_GUI_EVENT_MOUSE;
    event->seq = record->seq;
    event->tick = record->tick;
    event->dx = record->dx;
    event->dy = record->dy;
    event->buttons = record->buttons;
    event->keycode = 0u;
    event->ascii = 0;
    event->pressed = 0u;
    event->released = 0u;
    event->shift = 0u;
    event->ctrl = 0u;
    event->alt = 0u;
}

int syscall_common_request_core_gui_event_cursor_init(
    struct syscall_gui_event_cursor *cursor) {
    if (cursor == 0) {
        return 0;
    }
    cursor->keyboard_seq = keyboard_event_queue_latest_seq();
    cursor->mouse_seq = mouse_event_latest_seq();
    return 1;
}

uint64_t syscall_common_request_core_gui_event_poll(
    struct syscall_gui_event_poll *poll,
    uint32_t current_pid) {
    struct keyboard_event_record key_record;
    struct mouse_event_record mouse_record;
    struct syscall_gui_event_cursor key_cursor;
    struct syscall_gui_event_cursor mouse_cursor;
    int have_key;
    int have_mouse;
    int use_key;

    if (poll == 0) {
        return (uint64_t)-1;
    }
    {
        uint32_t focus_pid = input_focus_owner_pid();

        if (focus_pid != 0u &&
            (current_pid == 0u || focus_pid != current_pid)) {
            poll->event.type = SYS_GUI_EVENT_NONE;
            return SYS_GUI_EVENT_EMPTY;
        }
    }

    key_cursor = poll->cursor;
    mouse_cursor = poll->cursor;
    have_key = keyboard_event_queue_get_after(&key_cursor.keyboard_seq,
                                              &key_record);
    have_mouse = mouse_event_get_after(&mouse_cursor.mouse_seq, &mouse_record);

    poll->event.type = SYS_GUI_EVENT_NONE;
    poll->keyboard_dropped = keyboard_event_queue_dropped();
    poll->mouse_dropped = mouse_event_dropped();
    if (!have_key && !have_mouse) {
        return SYS_GUI_EVENT_EMPTY;
    }

    use_key = have_key && (!have_mouse || key_record.tick <= mouse_record.tick);
    if (use_key) {
        poll->cursor.keyboard_seq = key_cursor.keyboard_seq;
        syscall_common_gui_event_from_keyboard(&poll->event, &key_record);
    } else {
        poll->cursor.mouse_seq = mouse_cursor.mouse_seq;
        syscall_common_gui_event_from_mouse(&poll->event, &mouse_record);
    }
    return SYS_GUI_EVENT_READY;
}

uint64_t syscall_common_request_core_gui_event_grab(uint32_t current_pid,
                                                    int foreground_allowed) {
    if (current_pid == 0u || !foreground_allowed) {
        return (uint64_t)-1;
    }
    return input_focus_grab(current_pid) ? 0u : (uint64_t)-1;
}

uint64_t syscall_common_request_core_gui_event_release(uint32_t current_pid) {
    if (current_pid == 0u) {
        return (uint64_t)-1;
    }
    return input_focus_release(current_pid) ? 0u : (uint64_t)-1;
}

int syscall_common_request_core_gui_event_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    uint32_t current_pid,
    int foreground_allowed) {
    uint32_t op;

    if (request == 0 || result == 0 || request->number != SYS_GUI_EVENT) {
        return 0;
    }
    op = kernel_syscall_arg_u32(request, 0);
    result->action = SYSCALL_RESULT_RETURN;
    switch (op) {
        case SYS_GUI_EVENT_GRAB:
            if (!syscall_common_current_has_capability(PROCESS_CAP_INPUT)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_gui_event_grab(
                current_pid, foreground_allowed);
            return 1;
        case SYS_GUI_EVENT_RELEASE:
            if (!syscall_common_current_has_capability(PROCESS_CAP_INPUT)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value =
                syscall_common_request_core_gui_event_release(current_pid);
            return 1;
        default:
            return 0;
    }
}

int syscall_common_request_core_gui_event_transfer_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops,
    uint32_t current_pid,
    int foreground_allowed) {
    uint32_t op;
    uint64_t user_info_addr;

    if (request == 0 || result == 0 || request->number != SYS_GUI_EVENT) {
        return 0;
    }
    op = kernel_syscall_arg_u32(request, 0);
    user_info_addr = kernel_syscall_arg_u64(request, 1);
    result->action = SYSCALL_RESULT_RETURN;
    switch (op) {
        case SYS_GUI_EVENT_CURSOR_INIT: {
            struct syscall_gui_event_cursor cursor;

            if (!syscall_common_request_core_gui_event_cursor_init(&cursor)) {
                result->value = (uint64_t)-1;
                return 1;
            }
            result->value =
                ops != 0 && ops->copy_to_user != 0 &&
                ops->copy_to_user(user_info_addr, &cursor, sizeof(cursor))
                    ? 0u
                    : syscall_common_copy_bad_pointer(ops);
            return 1;
        }
        case SYS_GUI_EVENT_POLL: {
            struct syscall_gui_event_poll poll;

            if (ops == 0 || ops->copy_from_user == 0 ||
                ops->copy_to_user == 0 ||
                !ops->copy_from_user(&poll, user_info_addr, sizeof(poll))) {
                result->value = syscall_common_copy_bad_pointer(ops);
                return 1;
            }
            result->value =
                syscall_common_request_core_gui_event_poll(&poll, current_pid);
            if (!ops->copy_to_user(user_info_addr, &poll, sizeof(poll))) {
                result->value = syscall_common_copy_bad_pointer(ops);
            }
            return 1;
        }
        case SYS_GUI_EVENT_GRAB:
            if (!syscall_common_current_has_capability(PROCESS_CAP_INPUT)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = syscall_common_request_core_gui_event_grab(
                current_pid, foreground_allowed);
            return 1;
        case SYS_GUI_EVENT_RELEASE:
            if (!syscall_common_current_has_capability(PROCESS_CAP_INPUT)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value =
                syscall_common_request_core_gui_event_release(current_pid);
            return 1;
        default:
            result->value = (uint64_t)-1;
            return 1;
    }
}

int syscall_common_request_core_backend(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    if (syscall_common_request_core_clipboard(request, result)) {
        return 1;
    }
    switch (request->number) {
        case SYS_AUDIO_TONE:
            if (!syscall_common_current_has_capability(PROCESS_CAP_AUDIO)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = kernel_audio_play_tone(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2)) ? 1u : 0u;
            return 1;
        case SYS_RTL8139_TX_TEST:
            if (!syscall_common_current_has_capability(PROCESS_CAP_NET_RAW)) {
                result->value = syscall_common_access_denied();
                return 1;
            }
            result->value = kernel_rtl8139_send_test_frame() ? 1u : 0u;
            return 1;
        default:
            return 0;
    }
}

#include "kernel/internal/mem/address_space_internal.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/sys/syscall_i386.h"
#include "lib/string.h"

/*
 * i386 memory and IPC ABI adapter.
 *
 * This file keeps the compat32-only pointer copying and temporary legacy IPC
 * tables out of the native int 0x40 request adapter. The long-term target is
 * to shrink this further into common address-space and IPC helpers.
 */

enum {
    COMPAT32_PAGE_SIZE = 4096u,
    COMPAT32_IPC_NAME_MAX = 31u,
    COMPAT32_MMAP_MAX_PAGES = 16u,
    COMPAT32_MQ_MAX = 16u,
    COMPAT32_MQ_DEPTH = 16u,
    COMPAT32_SEM_MAX = 32u,
    COMPAT32_SEM_VALUE_MAX = 0x7fffffffu
};

struct compat32_ipc_message {
    uint16_t size;
    uint8_t data[SYS_MQ_MESSAGE_MAX];
};

struct compat32_message_queue {
    uint8_t used;
    uint8_t linked;
    uint8_t head;
    uint8_t count;
    char name[COMPAT32_IPC_NAME_MAX + 1u];
    struct compat32_ipc_message messages[COMPAT32_MQ_DEPTH];
};

struct compat32_semaphore {
    uint8_t used;
    uint8_t linked;
    char name[COMPAT32_IPC_NAME_MAX + 1u];
    uint32_t value;
};

static struct compat32_message_queue g_compat32_mq[COMPAT32_MQ_MAX];
static struct compat32_semaphore g_compat32_sem[COMPAT32_SEM_MAX];

static void compat32_copy_name_local(char *dst, const char *src) {
    uint32_t i = 0u;

    while (src[i] != '\0' && i < COMPAT32_IPC_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int compat32_name_equal(const char *lhs, const char *rhs) {
    uint32_t i = 0u;

    while (lhs[i] != '\0' && rhs[i] != '\0' && lhs[i] == rhs[i]) {
        i++;
    }
    return lhs[i] == rhs[i];
}

static int compat32_copy_name(uint32_t user_name, char *name) {
    if (!arch_copy_user_cstr(name, user_name, COMPAT32_IPC_NAME_MAX + 1u)) {
        return 0;
    }
    return name[0] != '\0';
}

static uint32_t compat32_round_pages(uint64_t length) {
    if (length == 0u ||
        length > ((uint64_t)COMPAT32_MMAP_MAX_PAGES * COMPAT32_PAGE_SIZE)) {
        return 0u;
    }
    return (uint32_t)((length + COMPAT32_PAGE_SIZE - 1u) / COMPAT32_PAGE_SIZE);
}

static void compat32_mq_destroy_if_unlinked_empty(struct compat32_message_queue *queue) {
    if (queue != 0 && queue->used && !queue->linked && queue->count == 0u) {
        memset(queue, 0, sizeof(*queue));
    }
}

static uint32_t compat32_mmap_perms(uint32_t prot) {
    return (prot & SYS_PROT_WRITE) != 0u ? VMM_PERM_WRITE : 0u;
}

static int compat32_mmap_range_valid(uint32_t addr, uint32_t length) {
    return addr >= USER_MMAP_BASE &&
           addr + length >= addr &&
           addr + length <= USER_MMAP_END &&
           (addr & (COMPAT32_PAGE_SIZE - 1u)) == 0u;
}

static void compat32_mmap_release_backend_range(
    struct syscall_compat32_context *ctx,
    uint32_t start,
    uint32_t page_count) {
    if (ctx == 0) {
        return;
    }
    for (uint32_t i = 0u; i < page_count; i++) {
        (void)addrspace_release_page_with_backend(
            start + i * COMPAT32_PAGE_SIZE,
            ctx->page_free,
            ctx->shared_page_unmap);
    }
}

static uint32_t syscall_compat32_mmap(struct syscall_compat32_context *ctx,
                                      uint32_t user_request) {
    struct syscall_mmap_request request;
    uint32_t page_count;
    uint32_t requested;
    uint32_t total_length;
    uint32_t mapped;
    int writable;
    int shared;

    if (ctx == 0 ||
        ctx->page_alloc_prot == 0 ||
        ctx->page_alloc_at == 0 ||
        ctx->page_free == 0 ||
        ctx->shared_page_map == 0 ||
        ctx->shared_page_unmap == 0 ||
        !arch_copy_from_user(&request, user_request, sizeof(request))) {
        return 0u;
    }
    page_count = compat32_round_pages(request.length);
    if (page_count == 0u ||
        request.offset != 0u ||
        request.prot == 0u ||
        (request.prot & ~(SYS_PROT_READ | SYS_PROT_WRITE)) != 0u) {
        return 0u;
    }
    total_length = page_count * COMPAT32_PAGE_SIZE;
    requested = (uint32_t)request.addr;
    if ((request.flags & SYS_MAP_FIXED) != 0u) {
        if (request.addr > 0xffffffffu ||
            !compat32_mmap_range_valid(requested, total_length)) {
            return 0u;
        }
    }
    shared = request.shm_handle != 0u;
    if (request.shm_handle != 0u) {
        if ((request.flags & SYS_MAP_SHARED) == 0u ||
            (request.flags & SYS_MAP_ANONYMOUS) != 0u ||
            addrspace_shm_size(request.shm_handle) < request.length ||
            page_count != 1u) {
            return 0u;
        }
    } else if ((request.flags & SYS_MAP_ANONYMOUS) == 0u) {
        return 0u;
    }

    writable = (request.prot & SYS_PROT_WRITE) != 0u;
    if (shared) {
        uint64_t frame;

        if ((request.flags & SYS_MAP_FIXED) != 0u) {
            return 0u;
        }
        frame = addrspace_shm_frame(request.shm_handle, 0u);
        if (frame == 0u || frame > 0xffffffffu) {
            return 0u;
        }
        mapped = ctx->shared_page_map((uint32_t)frame);
        if (mapped == 0u ||
            !compat32_mmap_range_valid(mapped, COMPAT32_PAGE_SIZE) ||
            !addrspace_track_existing_page(mapped,
                                           compat32_mmap_perms(request.prot),
                                           1,
                                           (uint16_t)(request.shm_handle - 1u)) ||
            !addrspace_shm_note_mapping(request.shm_handle)) {
            if (mapped != 0u) {
                (void)ctx->shared_page_unmap(mapped);
                addrspace_untrack_range(mapped, COMPAT32_PAGE_SIZE);
            }
            return 0u;
        }
        return mapped;
    }

    if ((request.flags & SYS_MAP_FIXED) != 0u) {
        mapped = requested;
        for (uint32_t i = 0u; i < page_count; i++) {
            uint32_t page = mapped + i * COMPAT32_PAGE_SIZE;

            if (ctx->page_alloc_at(page, writable) != page ||
                !addrspace_track_existing_page(page,
                                               compat32_mmap_perms(request.prot),
                                               0,
                                               (uint16_t)0xffffu)) {
                compat32_mmap_release_backend_range(ctx, mapped, i + 1u);
                return 0u;
            }
        }
        return mapped;
    }

    mapped = ctx->page_alloc_prot(writable);
    if (mapped == 0u || !compat32_mmap_range_valid(mapped, COMPAT32_PAGE_SIZE)) {
        return 0u;
    }
    if (!addrspace_track_existing_page(mapped,
                                       compat32_mmap_perms(request.prot),
                                       0,
                                       (uint16_t)0xffffu)) {
        (void)ctx->page_free(mapped);
        return 0u;
    }
    for (uint32_t i = 1u; i < page_count; i++) {
        uint32_t page = mapped + i * COMPAT32_PAGE_SIZE;

        if (!compat32_mmap_range_valid(page, COMPAT32_PAGE_SIZE) ||
            ctx->page_alloc_at(page, writable) != page ||
            !addrspace_track_existing_page(page,
                                           compat32_mmap_perms(request.prot),
                                           0,
                                           (uint16_t)0xffffu)) {
            compat32_mmap_release_backend_range(ctx, mapped, i + 1u);
            return 0u;
        }
    }
    return (uint32_t)mapped;
}

static uint32_t syscall_compat32_mprotect(struct syscall_compat32_context *ctx,
                                          uint32_t user_addr,
                                          uint32_t length,
                                          uint32_t prot) {
    uint32_t rounded_length;
    uint32_t protect_pages;
    int writable;

    if (ctx == 0 || ctx->page_protect == 0 ||
        user_addr == 0u || length == 0u ||
        prot == 0u ||
        (prot & ~(SYS_PROT_READ | SYS_PROT_WRITE)) != 0u) {
        return 0u;
    }
    rounded_length = (length + COMPAT32_PAGE_SIZE - 1u) &
                     ~(COMPAT32_PAGE_SIZE - 1u);
    if (rounded_length < length ||
        (user_addr & (COMPAT32_PAGE_SIZE - 1u)) != 0u ||
        user_addr + rounded_length < user_addr) {
        return 0u;
    }
    protect_pages = rounded_length / COMPAT32_PAGE_SIZE;

    writable = (prot & SYS_PROT_WRITE) != 0u;
    for (uint32_t i = 0u; i < protect_pages; i++) {
        if (ctx->page_protect(user_addr + i * COMPAT32_PAGE_SIZE,
                              writable) != 0) {
            return 0u;
        }
    }
    if (!addrspace_note_protect_range(user_addr,
                                      rounded_length,
                                      writable ? VMM_PERM_WRITE : 0u)) {
        return 0u;
    }
    return 1u;
}

static uint32_t syscall_compat32_munmap(struct syscall_compat32_context *ctx,
                                        uint32_t user_addr,
                                        uint32_t length) {
    uint32_t rounded_length;

    if (ctx == 0 || user_addr == 0u || length == 0u) {
        return 0u;
    }
    rounded_length = (length + COMPAT32_PAGE_SIZE - 1u) &
                     ~(COMPAT32_PAGE_SIZE - 1u);
    if (rounded_length < length ||
        (user_addr & (COMPAT32_PAGE_SIZE - 1u)) != 0u ||
        user_addr + rounded_length < user_addr) {
        return 0u;
    }
    for (uint32_t i = 0u; i < rounded_length / COMPAT32_PAGE_SIZE; i++) {
        if (!addrspace_release_page_with_backend(
                user_addr + i * COMPAT32_PAGE_SIZE,
                ctx->page_free,
                ctx->shared_page_unmap)) {
            return 0u;
        }
    }
    return 1u;
}

static uint32_t syscall_compat32_shm_open(struct syscall_compat32_context *ctx,
                                          uint32_t user_name,
                                          uint32_t size,
                                          uint32_t flags) {
    char name[COMPAT32_IPC_NAME_MAX + 1u];
    int handle;

    if (ctx == 0 ||
        size == 0u || size > COMPAT32_PAGE_SIZE ||
        !compat32_copy_name(user_name, name)) {
        return (uint32_t)-1;
    }
    handle = addrspace_shm_open(name, size, flags);
    if (handle <= 0 || addrspace_shm_frame((uint32_t)handle, 0u) == 0u) {
        return (uint32_t)-1;
    }
    return (uint32_t)handle;
}

static uint32_t syscall_compat32_shm_unlink(struct syscall_compat32_context *ctx,
                                            uint32_t user_name) {
    char name[COMPAT32_IPC_NAME_MAX + 1u];

    (void)ctx;
    if (!compat32_copy_name(user_name, name)) {
        return 0u;
    }
    return addrspace_shm_unlink(name) ? 1u : 0u;
}

static struct compat32_message_queue *compat32_mq_from_handle(uint32_t handle) {
    if (handle == 0u || handle > COMPAT32_MQ_MAX ||
        !g_compat32_mq[handle - 1u].used) {
        return 0;
    }
    return &g_compat32_mq[handle - 1u];
}

static uint32_t syscall_compat32_mq_open(uint32_t user_name, uint32_t flags) {
    char name[COMPAT32_IPC_NAME_MAX + 1u];
    uint32_t free_slot = COMPAT32_MQ_MAX;

    if (!compat32_copy_name(user_name, name)) {
        return (uint32_t)-1;
    }
    for (uint32_t i = 0u; i < COMPAT32_MQ_MAX; i++) {
        if (g_compat32_mq[i].used && g_compat32_mq[i].linked &&
            compat32_name_equal(g_compat32_mq[i].name, name)) {
            if ((flags & SYS_IPC_CREATE) != 0u &&
                (flags & SYS_IPC_EXCL) != 0u) {
                return (uint32_t)-1;
            }
            return i + 1u;
        }
        if (!g_compat32_mq[i].used && free_slot == COMPAT32_MQ_MAX) {
            free_slot = i;
        }
    }
    if ((flags & SYS_IPC_CREATE) == 0u || free_slot == COMPAT32_MQ_MAX) {
        return (uint32_t)-1;
    }
    memset(&g_compat32_mq[free_slot], 0, sizeof(g_compat32_mq[free_slot]));
    g_compat32_mq[free_slot].used = 1u;
    g_compat32_mq[free_slot].linked = 1u;
    compat32_copy_name_local(g_compat32_mq[free_slot].name, name);
    return free_slot + 1u;
}

static uint32_t syscall_compat32_mq_unlink(uint32_t user_name) {
    char name[COMPAT32_IPC_NAME_MAX + 1u];

    if (!compat32_copy_name(user_name, name)) {
        return 0u;
    }
    for (uint32_t i = 0u; i < COMPAT32_MQ_MAX; i++) {
        if (g_compat32_mq[i].used && g_compat32_mq[i].linked &&
            compat32_name_equal(g_compat32_mq[i].name, name)) {
            g_compat32_mq[i].linked = 0u;
            g_compat32_mq[i].name[0] = '\0';
            compat32_mq_destroy_if_unlinked_empty(&g_compat32_mq[i]);
            return 1u;
        }
    }
    return 0u;
}

static uint32_t syscall_compat32_mq_send(uint32_t handle, uint32_t user_buffer) {
    struct syscall_mq_buffer buffer;
    struct compat32_message_queue *queue = compat32_mq_from_handle(handle);
    struct compat32_ipc_message *message;
    uint32_t tail;

    if (queue == 0 ||
        !arch_copy_from_user(&buffer, user_buffer, sizeof(buffer)) ||
        buffer.size == 0u || buffer.size > SYS_MQ_MESSAGE_MAX) {
        return (uint32_t)-1;
    }
    if (queue->count == COMPAT32_MQ_DEPTH) {
        return 0u;
    }
    tail = (queue->head + queue->count) % COMPAT32_MQ_DEPTH;
    message = &queue->messages[tail];
    if (!arch_copy_from_user(message->data,
                             (uint32_t)buffer.data_addr,
                             buffer.size)) {
        return (uint32_t)-1;
    }
    message->size = (uint16_t)buffer.size;
    queue->count++;
    return buffer.size;
}

static uint32_t syscall_compat32_mq_receive(uint32_t handle,
                                            uint32_t user_buffer) {
    struct syscall_mq_buffer buffer;
    struct compat32_message_queue *queue = compat32_mq_from_handle(handle);
    struct compat32_ipc_message *message;
    uint32_t message_size;

    if (queue == 0 ||
        !arch_copy_from_user(&buffer, user_buffer, sizeof(buffer)) ||
        buffer.size == 0u) {
        return (uint32_t)-1;
    }
    if (queue->count == 0u) {
        return 0u;
    }
    message = &queue->messages[queue->head];
    if (buffer.size < message->size ||
        !arch_copy_to_user((uint32_t)buffer.data_addr,
                           message->data,
                           message->size)) {
        return (uint32_t)-1;
    }
    message_size = message->size;
    buffer.size = message->size;
    if (!arch_copy_to_user(user_buffer, &buffer, sizeof(buffer))) {
        return (uint32_t)-1;
    }
    queue->head = (queue->head + 1u) % COMPAT32_MQ_DEPTH;
    queue->count--;
    compat32_mq_destroy_if_unlinked_empty(queue);
    return message_size;
}

static struct compat32_semaphore *compat32_sem_from_handle(uint32_t handle) {
    if (handle == 0u || handle > COMPAT32_SEM_MAX ||
        !g_compat32_sem[handle - 1u].used) {
        return 0;
    }
    return &g_compat32_sem[handle - 1u];
}

static uint32_t syscall_compat32_sem_open(uint32_t user_name,
                                          uint32_t initial_value,
                                          uint32_t flags) {
    char name[COMPAT32_IPC_NAME_MAX + 1u];
    uint32_t free_slot = COMPAT32_SEM_MAX;

    if (initial_value > COMPAT32_SEM_VALUE_MAX ||
        !compat32_copy_name(user_name, name)) {
        return (uint32_t)-1;
    }
    for (uint32_t i = 0u; i < COMPAT32_SEM_MAX; i++) {
        if (g_compat32_sem[i].used && g_compat32_sem[i].linked &&
            compat32_name_equal(g_compat32_sem[i].name, name)) {
            if ((flags & SYS_IPC_CREATE) != 0u &&
                (flags & SYS_IPC_EXCL) != 0u) {
                return (uint32_t)-1;
            }
            return i + 1u;
        }
        if (!g_compat32_sem[i].used && free_slot == COMPAT32_SEM_MAX) {
            free_slot = i;
        }
    }
    if ((flags & SYS_IPC_CREATE) == 0u || free_slot == COMPAT32_SEM_MAX) {
        return (uint32_t)-1;
    }
    memset(&g_compat32_sem[free_slot], 0, sizeof(g_compat32_sem[free_slot]));
    g_compat32_sem[free_slot].used = 1u;
    g_compat32_sem[free_slot].linked = 1u;
    g_compat32_sem[free_slot].value = initial_value;
    compat32_copy_name_local(g_compat32_sem[free_slot].name, name);
    return free_slot + 1u;
}

static uint32_t syscall_compat32_sem_unlink(uint32_t user_name) {
    char name[COMPAT32_IPC_NAME_MAX + 1u];

    if (!compat32_copy_name(user_name, name)) {
        return 0u;
    }
    for (uint32_t i = 0u; i < COMPAT32_SEM_MAX; i++) {
        if (g_compat32_sem[i].used && g_compat32_sem[i].linked &&
            compat32_name_equal(g_compat32_sem[i].name, name)) {
            g_compat32_sem[i].linked = 0u;
            g_compat32_sem[i].name[0] = '\0';
            return 1u;
        }
    }
    return 0u;
}

static uint32_t syscall_compat32_sem_trywait(uint32_t handle) {
    struct compat32_semaphore *sem = compat32_sem_from_handle(handle);

    if (sem == 0) {
        return (uint32_t)-1;
    }
    if (sem->value == 0u) {
        return 0u;
    }
    sem->value--;
    return 1u;
}

static uint32_t syscall_compat32_sem_post(uint32_t handle) {
    struct compat32_semaphore *sem = compat32_sem_from_handle(handle);

    if (sem == 0 || sem->value == COMPAT32_SEM_VALUE_MAX) {
        return 0u;
    }
    sem->value++;
    return 1u;
}

int syscall_i386_request_adapter_mem(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MMAP:
            result->value = syscall_compat32_mmap(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_MUNMAP:
            result->value = syscall_compat32_munmap(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_MPROTECT:
            result->value = syscall_compat32_mprotect(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_PAGE_ALLOC:
            result->value = syscall_compat32_page_alloc(ctx);
            return 1;
        case SYS_PAGE_FREE:
            result->value = syscall_compat32_page_free(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        default:
            return 0;
    }
}

int syscall_i386_request_adapter_ipc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_SHM_OPEN:
            result->value = syscall_compat32_shm_open(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_SHM_UNLINK:
            result->value = syscall_compat32_shm_unlink(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_MQ_OPEN:
            result->value = syscall_compat32_mq_open(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_MQ_UNLINK:
            result->value = syscall_compat32_mq_unlink(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_MQ_SEND:
            result->value = syscall_compat32_mq_send(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_MQ_RECEIVE:
            result->value = syscall_compat32_mq_receive(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_SEM_OPEN:
            result->value = syscall_compat32_sem_open(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_SEM_UNLINK:
            result->value = syscall_compat32_sem_unlink(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SEM_TRYWAIT:
            result->value = syscall_compat32_sem_trywait(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SEM_POST:
            result->value = syscall_compat32_sem_post(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        default:
            return 0;
    }
}


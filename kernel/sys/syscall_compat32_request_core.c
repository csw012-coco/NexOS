#include "block/blockdev.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/core/system_query_internal.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/process.h"
#include "lib/string.h"

extern uint64_t syscall_handle_fg(uint32_t pid) __attribute__((weak));
extern uint64_t syscall_handle_bg(uint32_t pid) __attribute__((weak));

enum {
    COMPAT32_GFX_BATCH_CHUNK = 16u,
    COMPAT32_GFX_BLIT_MAX_DIMENSION = 8192u,
    COMPAT32_PAGE_SIZE = 4096u,
    COMPAT32_IPC_NAME_MAX = 31u,
    COMPAT32_SHM_MAX = 16u,
    COMPAT32_MMAP_MAX = 32u,
    COMPAT32_MMAP_MAX_PAGES = 16u,
    COMPAT32_MQ_MAX = 16u,
    COMPAT32_MQ_DEPTH = 16u,
    COMPAT32_SEM_MAX = 32u,
    COMPAT32_SEM_VALUE_MAX = 0x7fffffffu,
    COMPAT32_AUDIO_BUFFER_MAX = 65536u,
    COMPAT32_RTL8139_TX_BUFFER_MAX = 1600u
};

struct compat32_shm_object {
    uint8_t used;
    uint8_t linked;
    uint16_t mapping_refs;
    uint32_t size;
    uint32_t frame;
    char name[COMPAT32_IPC_NAME_MAX + 1u];
};

struct compat32_mmap_region {
    uint8_t used;
    uint8_t shared;
    uint16_t shm_index;
    uint32_t owner_pid;
    uint32_t prot;
    uint32_t addr;
    uint32_t length;
    uint32_t page_count;
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

static char g_compat32_clipboard_buffer[KERNEL_CLIPBOARD_TEXT_MAX + 1u];
static struct syscall_gfx_batch_entry g_compat32_gfx_batch_entries[COMPAT32_GFX_BATCH_CHUNK];
static uint8_t g_compat32_audio_buffer[COMPAT32_AUDIO_BUFFER_MAX];
static uint8_t g_compat32_rtl8139_tx_buffer[COMPAT32_RTL8139_TX_BUFFER_MAX];
static struct compat32_shm_object g_compat32_shm[COMPAT32_SHM_MAX];
static struct compat32_mmap_region g_compat32_mmaps[COMPAT32_MMAP_MAX];
static struct compat32_message_queue g_compat32_mq[COMPAT32_MQ_MAX];
static struct compat32_semaphore g_compat32_sem[COMPAT32_SEM_MAX];

static uint32_t compat32_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

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

void syscall_compat32_vm_snapshot(struct syscall_compat32_context *ctx,
                                  struct syscall_vm_info *info) {
    uint32_t pid = ctx != 0 ? ctx->pid : 0u;

    if (info == 0) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->mmap_region_capacity = COMPAT32_MMAP_MAX;
    info->mmap_page_capacity = COMPAT32_MMAP_MAX_PAGES;
    info->shm_object_capacity = COMPAT32_SHM_MAX;
    info->user_stack_pages = NOS_USER_STACK_SIZE / COMPAT32_PAGE_SIZE;
    for (uint32_t i = 0u; i < COMPAT32_MMAP_MAX; i++) {
        if (!g_compat32_mmaps[i].used ||
            (pid != 0u && g_compat32_mmaps[i].owner_pid != pid)) {
            continue;
        }
        info->mmap_regions++;
        info->mmap_pages += g_compat32_mmaps[i].page_count;
        if (g_compat32_mmaps[i].shared) {
            info->shared_regions++;
            info->shm_mapped_pages += g_compat32_mmaps[i].page_count;
        }
    }
    for (uint32_t i = 0u; i < COMPAT32_SHM_MAX; i++) {
        if (g_compat32_shm[i].used) {
            info->shm_objects++;
        }
    }
}

static void compat32_destroy_shm_if_unlinked(struct syscall_compat32_context *ctx,
                                             uint32_t index) {
    if (index >= COMPAT32_SHM_MAX) {
        return;
    }
    if (g_compat32_shm[index].used &&
        !g_compat32_shm[index].linked &&
        g_compat32_shm[index].mapping_refs == 0u) {
        if (g_compat32_shm[index].frame != 0u &&
            ctx != 0 &&
            ctx->shared_page_free != 0) {
            (void)ctx->shared_page_free(g_compat32_shm[index].frame);
        }
        memset(&g_compat32_shm[index], 0, sizeof(g_compat32_shm[index]));
    }
}

static struct compat32_shm_object *compat32_shm_from_handle(uint32_t handle,
                                                            uint32_t *index) {
    uint32_t slot;

    if (handle == 0u || handle > COMPAT32_SHM_MAX) {
        return 0;
    }
    slot = handle - 1u;
    if (!g_compat32_shm[slot].used) {
        return 0;
    }
    if (index != 0) {
        *index = slot;
    }
    return &g_compat32_shm[slot];
}

static struct compat32_mmap_region *compat32_mmap_find_containing(
    uint32_t pid, uint32_t addr, uint32_t end) {
    for (uint32_t i = 0u; i < COMPAT32_MMAP_MAX; i++) {
        struct compat32_mmap_region *region = &g_compat32_mmaps[i];
        uint32_t region_end;

        if (!region->used ||
            (pid != 0u && region->owner_pid != pid) ||
            region->addr > addr ||
            region->length > 0xffffffffu - region->addr) {
            continue;
        }
        region_end = region->addr + region->length;
        if (end <= region_end) {
            return region;
        }
    }
    return 0;
}

static int compat32_mmap_overlaps(uint32_t pid, uint32_t addr, uint32_t end) {
    for (uint32_t i = 0u; i < COMPAT32_MMAP_MAX; i++) {
        uint32_t region_end;

        if (!g_compat32_mmaps[i].used ||
            (pid != 0u && g_compat32_mmaps[i].owner_pid != pid) ||
            g_compat32_mmaps[i].length >
                0xffffffffu - g_compat32_mmaps[i].addr) {
            continue;
        }
        region_end = g_compat32_mmaps[i].addr + g_compat32_mmaps[i].length;
        if (addr < region_end && g_compat32_mmaps[i].addr < end) {
            return 1;
        }
    }
    return 0;
}

void syscall_compat32_cleanup_pid(struct syscall_compat32_context *ctx,
                                  uint32_t pid) {
    if (pid == 0u) {
        return;
    }
    for (uint32_t i = 0u; i < COMPAT32_MMAP_MAX; i++) {
        struct compat32_mmap_region *region = &g_compat32_mmaps[i];

        if (!region->used || region->owner_pid != pid) {
            continue;
        }
        if (region->shared) {
            if (ctx != 0 && ctx->pid == pid &&
                ctx->shared_page_unmap != 0) {
                (void)ctx->shared_page_unmap(region->addr);
            }
            if (region->shm_index < COMPAT32_SHM_MAX &&
                g_compat32_shm[region->shm_index].used &&
                g_compat32_shm[region->shm_index].mapping_refs != 0u) {
                g_compat32_shm[region->shm_index].mapping_refs--;
                compat32_destroy_shm_if_unlinked(ctx, region->shm_index);
            }
        } else if (ctx != 0 && ctx->pid == pid &&
                   ctx->page_free != 0) {
            for (uint32_t page = 0u; page < region->page_count; page++) {
                (void)ctx->page_free(region->addr +
                                     page * COMPAT32_PAGE_SIZE);
            }
        }
        memset(region, 0, sizeof(*region));
    }
}

static struct compat32_mmap_region *compat32_mmap_alloc_slot(void) {
    for (uint32_t i = 0u; i < COMPAT32_MMAP_MAX; i++) {
        if (!g_compat32_mmaps[i].used) {
            return &g_compat32_mmaps[i];
        }
    }
    return 0;
}

static void compat32_mq_destroy_if_unlinked_empty(struct compat32_message_queue *queue) {
    if (queue != 0 && queue->used && !queue->linked && queue->count == 0u) {
        memset(queue, 0, sizeof(*queue));
    }
}

static uint32_t syscall_compat32_mmap(struct syscall_compat32_context *ctx,
                                      uint32_t user_request) {
    struct syscall_mmap_request request;
    struct compat32_mmap_region *region;
    struct compat32_shm_object *shm = 0;
    uint32_t shm_index = 0u;
    uint32_t page_count;
    uint32_t base = 0u;
    uint32_t requested;
    uint32_t total_length;
    int writable;

    if (ctx == 0 || ctx->page_alloc == 0 ||
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
    writable = (request.prot & SYS_PROT_WRITE) != 0u;
    total_length = page_count * COMPAT32_PAGE_SIZE;
    requested = (uint32_t)request.addr;
    if ((request.flags & SYS_MAP_FIXED) != 0u) {
        if (ctx->page_alloc_at == 0 ||
            request.addr > 0xffffffffu ||
            requested < 0x50000000u ||
            requested + total_length < requested ||
            requested + total_length > 0x70000000u ||
            (requested & (COMPAT32_PAGE_SIZE - 1u)) != 0u ||
            compat32_mmap_overlaps(ctx->pid,
                                   requested,
                                   requested + total_length)) {
            return 0u;
        }
    }
    if (request.shm_handle != 0u) {
        if ((request.flags & SYS_MAP_FIXED) != 0u ||
            (request.flags & SYS_MAP_SHARED) == 0u ||
            (request.flags & SYS_MAP_ANONYMOUS) != 0u ||
            ctx->shared_page_map == 0 ||
            page_count != 1u) {
            return 0u;
        }
        shm = compat32_shm_from_handle(request.shm_handle, &shm_index);
        if (shm == 0 || request.length > shm->size ||
            request.length > COMPAT32_PAGE_SIZE ||
            shm->frame == 0u) {
            return 0u;
        }
    } else if ((request.flags & SYS_MAP_ANONYMOUS) == 0u) {
        return 0u;
    }

    region = compat32_mmap_alloc_slot();
    if (region == 0) {
        return 0u;
    }
    if (shm != 0) {
        base = ctx->shared_page_map(shm->frame);
        if (base == 0u) {
            return 0u;
        }
    } else {
        for (uint32_t i = 0u; i < page_count; i++) {
            uint32_t page = (request.flags & SYS_MAP_FIXED) != 0u
                                ? ctx->page_alloc_at(
                                      requested + i * COMPAT32_PAGE_SIZE,
                                      writable)
                                : (ctx->page_alloc_prot != 0
                                       ? ctx->page_alloc_prot(writable)
                                       : ctx->page_alloc());

            if (page == 0u || (i != 0u && page != base + i * COMPAT32_PAGE_SIZE)) {
                if (page != 0u) {
                    (void)ctx->page_free(page);
                }
                for (uint32_t j = 0u; j < i; j++) {
                    (void)ctx->page_free(base + j * COMPAT32_PAGE_SIZE);
                }
                return 0u;
            }
            if (i == 0u) {
                base = page;
            }
        }
    }

    if (shm != 0) {
        shm->mapping_refs++;
    }
    memset(region, 0, sizeof(*region));
    region->used = 1u;
    region->shared = shm != 0 ? 1u : 0u;
    region->shm_index = (uint16_t)shm_index;
    region->owner_pid = ctx->pid;
    region->prot = request.prot;
    region->addr = base;
    region->length = total_length;
    region->page_count = page_count;
    return base;
}

static uint32_t syscall_compat32_mprotect(struct syscall_compat32_context *ctx,
                                          uint32_t user_addr,
                                          uint32_t length,
                                          uint32_t prot) {
    struct compat32_mmap_region *region;
    struct compat32_mmap_region *middle = 0;
    struct compat32_mmap_region *tail = 0;
    uint32_t rounded_length;
    uint32_t region_end;
    uint32_t protect_end;
    uint32_t offset_pages;
    uint32_t protect_pages;
    uint32_t old_prot;
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
    protect_end = user_addr + rounded_length;
    region = compat32_mmap_find_containing(ctx->pid, user_addr, protect_end);
    if (region == 0 || region->shared) {
        return 0u;
    }
    region_end = region->addr + region->length;
    offset_pages = (user_addr - region->addr) / COMPAT32_PAGE_SIZE;
    protect_pages = rounded_length / COMPAT32_PAGE_SIZE;
    if (user_addr != region->addr) {
        middle = compat32_mmap_alloc_slot();
        if (middle == 0) {
            return 0u;
        }
        middle->used = 1u;
    }
    if (protect_end != region_end) {
        tail = compat32_mmap_alloc_slot();
        if (tail == 0) {
            if (middle != 0) {
                memset(middle, 0, sizeof(*middle));
            }
            return 0u;
        }
        tail->used = 1u;
    }

    writable = (prot & SYS_PROT_WRITE) != 0u;
    for (uint32_t i = 0u; i < protect_pages; i++) {
        if (ctx->page_protect(user_addr + i * COMPAT32_PAGE_SIZE,
                              writable) != 0) {
            if (middle != 0) {
                memset(middle, 0, sizeof(*middle));
            }
            if (tail != 0) {
                memset(tail, 0, sizeof(*tail));
            }
            return 0u;
        }
    }

    old_prot = region->prot;
    if (user_addr == region->addr && protect_end == region_end) {
        region->prot = prot;
        return 1u;
    }
    if (tail != 0) {
        memset(tail, 0, sizeof(*tail));
        tail->used = 1u;
        tail->shared = 0u;
        tail->owner_pid = region->owner_pid;
        tail->prot = old_prot;
        tail->addr = protect_end;
        tail->length = region_end - protect_end;
        tail->page_count = tail->length / COMPAT32_PAGE_SIZE;
    }
    if (middle != 0) {
        memset(middle, 0, sizeof(*middle));
        middle->used = 1u;
        middle->shared = 0u;
        middle->owner_pid = region->owner_pid;
        middle->prot = prot;
        middle->addr = user_addr;
        middle->length = rounded_length;
        middle->page_count = protect_pages;
        region->length = user_addr - region->addr;
        region->page_count = offset_pages;
    } else {
        region->prot = prot;
        region->length = rounded_length;
        region->page_count = protect_pages;
    }
    return 1u;
}

static uint32_t syscall_compat32_munmap(struct syscall_compat32_context *ctx,
                                        uint32_t user_addr,
                                        uint32_t length) {
    struct compat32_mmap_region *region;
    uint32_t rounded_length;
    uint32_t region_end;
    uint32_t unmap_end;
    uint32_t offset_pages;
    uint32_t unmap_pages;
    struct compat32_mmap_region *tail = 0;
    uint32_t ok = 1u;

    if (ctx == 0 || ctx->page_free == 0 ||
        user_addr == 0u || length == 0u) {
        return 0u;
    }
    rounded_length = (length + COMPAT32_PAGE_SIZE - 1u) &
                     ~(COMPAT32_PAGE_SIZE - 1u);
    if (rounded_length < length ||
        (user_addr & (COMPAT32_PAGE_SIZE - 1u)) != 0u ||
        user_addr + rounded_length < user_addr) {
        return 0u;
    }
    unmap_end = user_addr + rounded_length;
    region = compat32_mmap_find_containing(ctx->pid, user_addr, unmap_end);
    if (region == 0 ||
        (region->shared &&
         (user_addr != region->addr || rounded_length != region->length))) {
        return 0u;
    }
    region_end = region->addr + region->length;
    offset_pages = (user_addr - region->addr) / COMPAT32_PAGE_SIZE;
    unmap_pages = rounded_length / COMPAT32_PAGE_SIZE;
    if (!region->shared && user_addr != region->addr &&
        unmap_end != region_end) {
        tail = compat32_mmap_alloc_slot();
        if (tail == 0) {
            return 0u;
        }
    }
    if (region->shared && region->shm_index < COMPAT32_SHM_MAX &&
        g_compat32_shm[region->shm_index].used) {
        struct compat32_shm_object *shm = &g_compat32_shm[region->shm_index];

        if (ctx->shared_page_unmap == 0 ||
            ctx->shared_page_unmap(user_addr) != 0) {
            return 0u;
        }
        if (shm->mapping_refs != 0u) {
            shm->mapping_refs--;
        }
    } else {
        for (uint32_t i = 0u; i < unmap_pages; i++) {
            if (ctx->page_free(user_addr + i * COMPAT32_PAGE_SIZE) != 0) {
                ok = 0u;
            }
        }
    }
    if (!ok) {
        return 0u;
    }
    if (region->shared) {
        compat32_destroy_shm_if_unlinked(ctx, region->shm_index);
        memset(region, 0, sizeof(*region));
        return 1u;
    }
    if (user_addr == region->addr && unmap_end == region_end) {
        memset(region, 0, sizeof(*region));
    } else if (user_addr == region->addr) {
        region->addr = unmap_end;
        region->length = region_end - unmap_end;
        region->page_count -= unmap_pages;
    } else if (unmap_end == region_end) {
        region->length = user_addr - region->addr;
        region->page_count = offset_pages;
    } else {
        memset(tail, 0, sizeof(*tail));
        tail->used = 1u;
        tail->shared = 0u;
        tail->owner_pid = region->owner_pid;
        tail->prot = region->prot;
        tail->addr = unmap_end;
        tail->length = region_end - unmap_end;
        tail->page_count = region->page_count - offset_pages - unmap_pages;
        region->length = user_addr - region->addr;
        region->page_count = offset_pages;
    }
    return 1u;
}

static uint32_t syscall_compat32_shm_open(struct syscall_compat32_context *ctx,
                                          uint32_t user_name,
                                          uint32_t size,
                                          uint32_t flags) {
    char name[COMPAT32_IPC_NAME_MAX + 1u];
    uint32_t free_slot = COMPAT32_SHM_MAX;

    if (ctx == 0 ||
        ctx->shared_page_alloc == 0 ||
        size == 0u || size > COMPAT32_PAGE_SIZE ||
        !compat32_copy_name(user_name, name)) {
        return (uint32_t)-1;
    }
    for (uint32_t i = 0u; i < COMPAT32_SHM_MAX; i++) {
        if (g_compat32_shm[i].used && g_compat32_shm[i].linked &&
            compat32_name_equal(g_compat32_shm[i].name, name)) {
            if ((flags & SYS_SHM_CREATE) != 0u &&
                (flags & SYS_SHM_EXCL) != 0u) {
                return (uint32_t)-1;
            }
            return i + 1u;
        }
        if (!g_compat32_shm[i].used && free_slot == COMPAT32_SHM_MAX) {
            free_slot = i;
        }
    }
    if ((flags & SYS_SHM_CREATE) == 0u || free_slot == COMPAT32_SHM_MAX) {
        return (uint32_t)-1;
    }
    memset(&g_compat32_shm[free_slot], 0, sizeof(g_compat32_shm[free_slot]));
    g_compat32_shm[free_slot].frame = ctx->shared_page_alloc();
    if (g_compat32_shm[free_slot].frame == 0u) {
        return (uint32_t)-1;
    }
    g_compat32_shm[free_slot].used = 1u;
    g_compat32_shm[free_slot].linked = 1u;
    g_compat32_shm[free_slot].size = size;
    compat32_copy_name_local(g_compat32_shm[free_slot].name, name);
    return free_slot + 1u;
}

static uint32_t syscall_compat32_shm_unlink(struct syscall_compat32_context *ctx,
                                            uint32_t user_name) {
    char name[COMPAT32_IPC_NAME_MAX + 1u];

    if (!compat32_copy_name(user_name, name)) {
        return 0u;
    }
    for (uint32_t i = 0u; i < COMPAT32_SHM_MAX; i++) {
        if (g_compat32_shm[i].used && g_compat32_shm[i].linked &&
            compat32_name_equal(g_compat32_shm[i].name, name)) {
            g_compat32_shm[i].linked = 0u;
            g_compat32_shm[i].name[0] = '\0';
            compat32_destroy_shm_if_unlinked(ctx, i);
            return 1u;
        }
    }
    return 0u;
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

static int syscall_compat32_pid_exists(struct syscall_compat32_context *ctx,
                                       uint32_t pid) {
    struct process_snapshot snapshot;

    if (ctx == 0 || ctx->process_snapshot == 0 || pid == 0u) {
        return 0;
    }
    for (uint32_t slot = 0u; slot < NOS_PROCESS_SLOT_MAX; slot++) {
        if (ctx->process_snapshot(slot, &snapshot) &&
            snapshot.pid == pid &&
            snapshot.state != PROCESS_STATE_FREE) {
            return 1;
        }
    }
    return 0;
}

static uint32_t syscall_compat32_fg(struct syscall_compat32_context *ctx,
                                    uint32_t pid) {
    if (syscall_handle_fg != 0) {
        return (uint32_t)syscall_handle_fg(pid);
    }
    if (ctx == 0 || ctx->tty == 0 || !syscall_compat32_pid_exists(ctx, pid)) {
        return 0u;
    }
    tty_set_foreground_pid(ctx->tty, pid);
    return 1u;
}

static uint32_t syscall_compat32_bg(struct syscall_compat32_context *ctx,
                                    uint32_t pid) {
    if (syscall_handle_bg != 0) {
        return (uint32_t)syscall_handle_bg(pid);
    }
    if (ctx == 0 || ctx->tty == 0 || pid == 0u) {
        return 0u;
    }
    tty_clear_foreground_pid(ctx->tty, pid);
    return 1u;
}

static uint32_t syscall_compat32_block_read(uint32_t disk_index,
                                             uint64_t lba,
                                             uint32_t user_info) {
    struct syscall_block_read_info info;

    if (!kernel_block_read(disk_index, lba, &info)) {
        return 0u;
    }
    return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
}

static uint32_t syscall_compat32_block_write(uint32_t disk_index,
                                              uint64_t lba,
                                              uint32_t user_info) {
    struct syscall_block_write_info info;

    if (!arch_copy_from_user(&info, user_info, sizeof(info))) {
        return 0u;
    }
    if (!kernel_block_write(disk_index, lba, &info)) {
        (void)arch_copy_to_user(user_info, &info, sizeof(info));
        return 0u;
    }
    return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
}

static uint32_t syscall_compat32_clipboard(uint32_t op, uint32_t user_info) {
    struct syscall_clipboard_transfer transfer;
    uint32_t bytes;

    if (op == SYS_CLIPBOARD_CLEAR) {
        return kernel_clipboard_set_text("", 0u);
    }
    if (!arch_copy_from_user(&transfer, user_info, sizeof(transfer))) {
        return (uint32_t)-1;
    }
    switch (op) {
        case SYS_CLIPBOARD_GET:
            bytes = compat32_min_u32(kernel_clipboard_size(), transfer.bytes);
            if (bytes != 0u &&
                !arch_copy_to_user((uint32_t)transfer.data_addr,
                                   kernel_clipboard_text(),
                                   bytes)) {
                return (uint32_t)-1;
            }
            transfer.size = kernel_clipboard_size();
            return arch_copy_to_user(user_info, &transfer, sizeof(transfer))
                ? bytes
                : (uint32_t)-1;
        case SYS_CLIPBOARD_SET:
            bytes = compat32_min_u32(transfer.bytes, KERNEL_CLIPBOARD_TEXT_MAX);
            if (bytes != 0u &&
                !arch_copy_from_user(g_compat32_clipboard_buffer,
                                     (uint32_t)transfer.data_addr,
                                     bytes)) {
                return (uint32_t)-1;
            }
            g_compat32_clipboard_buffer[bytes] = '\0';
            transfer.size = kernel_clipboard_set_text(g_compat32_clipboard_buffer, bytes);
            return arch_copy_to_user(user_info, &transfer, sizeof(transfer))
                ? transfer.size
                : (uint32_t)-1;
        case SYS_CLIPBOARD_SIZE:
            transfer.size = kernel_clipboard_size();
            return arch_copy_to_user(user_info, &transfer, sizeof(transfer))
                ? transfer.size
                : (uint32_t)-1;
        default:
            return (uint32_t)-1;
    }
}

static uint32_t syscall_compat32_gfx_batch(uint32_t user_info) {
    struct syscall_gfx_batch batch;
    uint32_t processed = 0u;
    int valid = 1;

    if (!arch_copy_from_user(&batch, user_info, sizeof(batch)) ||
        batch.count > SYS_GFX_BATCH_MAX_COMMANDS ||
        (batch.flags & ~SYS_GFX_BATCH_PRESENT) != 0u ||
        (batch.count != 0u && batch.entries_addr == 0u)) {
        return (uint32_t)-1;
    }

    kernel_gfx_begin_batch();
    while (processed < batch.count) {
        uint32_t count = batch.count - processed;
        uint32_t bytes;
        uint32_t entries_addr;

        if (count > COMPAT32_GFX_BATCH_CHUNK) {
            count = COMPAT32_GFX_BATCH_CHUNK;
        }
        bytes = count * sizeof(struct syscall_gfx_batch_entry);
        entries_addr = (uint32_t)batch.entries_addr +
            processed * sizeof(struct syscall_gfx_batch_entry);
        if (!arch_copy_from_user(g_compat32_gfx_batch_entries, entries_addr, bytes)) {
            valid = 0;
            break;
        }
        for (uint32_t i = 0u; i < count; i++) {
            const struct syscall_gfx_batch_entry *entry =
                &g_compat32_gfx_batch_entries[i];

            if (entry->reserved != 0u ||
                entry->op == SYS_GFX_INFO ||
                entry->op == SYS_GFX_BATCH ||
                entry->op == SYS_GFX_BLIT ||
                entry->op == SYS_GFX_PRESENT ||
                !kernel_gfx_dispatch(entry->op, &entry->command, 0)) {
                valid = 0;
                break;
            }
        }
        if (!valid) {
            break;
        }
        processed += count;
    }
    kernel_gfx_end_batch(valid ? batch.flags : 0u);
    return valid ? 0u : (uint32_t)-1;
}

static uint32_t syscall_compat32_gfx(uint32_t op, uint32_t user_info) {
    struct syscall_gfx_command cmd;
    struct syscall_gfx_info info;
    enum kernel_gfx_buffer_kind buffer_kind;

    if (op == SYS_GFX_BATCH) {
        return syscall_compat32_gfx_batch(user_info);
    }
    if (op == SYS_GFX_BLIT) {
        return (uint32_t)-1;
    }

    buffer_kind = kernel_gfx_buffer_kind(op);
    switch (buffer_kind) {
        case KERNEL_GFX_BUFFER_INFO_OUT:
            memset(&info, 0, sizeof(info));
            if (!kernel_gfx_dispatch(op, 0, &info)) {
                return (uint32_t)-1;
            }
            return arch_copy_to_user(user_info, &info, sizeof(info))
                ? 0u
                : (uint32_t)-1;
        case KERNEL_GFX_BUFFER_COMMAND_IN:
            if (!arch_copy_from_user(&cmd, user_info, sizeof(cmd))) {
                return (uint32_t)-1;
            }
            return kernel_gfx_dispatch(op, &cmd, 0) ? 0u : (uint32_t)-1;
        case KERNEL_GFX_BUFFER_INVALID:
        default:
            return (uint32_t)-1;
    }
}

static uint32_t syscall_compat32_gui_event(uint32_t op, uint32_t user_info) {
    if (op == SYS_GUI_EVENT_CURSOR_INIT) {
        struct syscall_gui_event_cursor cursor = {0};

        return arch_copy_to_user(user_info, &cursor, sizeof(cursor))
            ? 0u
            : (uint32_t)-1;
    }
    if (op == SYS_GUI_EVENT_POLL) {
        struct syscall_gui_event_poll poll;

        if (!arch_copy_from_user(&poll, user_info, sizeof(poll))) {
            return (uint32_t)-1;
        }
        memset(&poll.event, 0, sizeof(poll.event));
        poll.event.type = SYS_GUI_EVENT_NONE;
        poll.keyboard_dropped = 0u;
        poll.mouse_dropped = 0u;
        return arch_copy_to_user(user_info, &poll, sizeof(poll))
            ? SYS_GUI_EVENT_EMPTY
            : (uint32_t)-1;
    }
    if (op == SYS_GUI_EVENT_GRAB || op == SYS_GUI_EVENT_RELEASE) {
        return 0u;
    }
    return (uint32_t)-1;
}

static uint32_t syscall_compat32_audio_play(uint32_t index, uint32_t user_info) {
    struct syscall_audio_play_info info;

    if (!arch_copy_from_user(&info, user_info, sizeof(info))) {
        return (uint32_t)-1;
    }
    if (info.bytes == 0u || info.bytes > COMPAT32_AUDIO_BUFFER_MAX ||
        (info.flags & ~SYS_AUDIO_PLAY_F_ASYNC) != 0u ||
        info.channels == 0u ||
        info.bits_per_sample == 0u ||
        info.sample_rate == 0u) {
        return 0u;
    }
    if (!arch_copy_from_user(g_compat32_audio_buffer,
                             (uint32_t)info.data_addr,
                             info.bytes)) {
        return (uint32_t)-1;
    }
    return kernel_audio_play_buffer(index, &info, g_compat32_audio_buffer) ? 1u : 0u;
}

static uint32_t syscall_compat32_rtl8139_tx_send(uint32_t user_info) {
    struct syscall_rtl8139_tx_info info;

    if (!arch_copy_from_user(&info, user_info, sizeof(info))) {
        return (uint32_t)-1;
    }
    if (info.bytes < 14u || info.bytes > COMPAT32_RTL8139_TX_BUFFER_MAX) {
        return 0u;
    }
    if (!arch_copy_from_user(g_compat32_rtl8139_tx_buffer,
                             (uint32_t)info.data_addr,
                             info.bytes)) {
        return (uint32_t)-1;
    }
    return kernel_rtl8139_send_frame(g_compat32_rtl8139_tx_buffer, info.bytes) ? 1u : 0u;
}

static uint32_t syscall_compat32_rtl8139_rx_dump(uint32_t user_info) {
    struct syscall_rtl8139_rx_info info;

    memset(&info, 0, sizeof(info));
    if (!kernel_rtl8139_receive_packet(&info)) {
        return arch_copy_to_user(user_info, &info, sizeof(info)) ? 0u : (uint32_t)-1;
    }
    return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : (uint32_t)-1;
}

static uint32_t syscall_compat32_capability_event(uint32_t user_info) {
    struct syscall_capability_event event;

    if (!arch_copy_from_user(&event, user_info, sizeof(event))) {
        return (uint32_t)-1;
    }
    event.source[sizeof(event.source) - 1u] = '\0';
    event.action[sizeof(event.action) - 1u] = '\0';
    event.caps[sizeof(event.caps) - 1u] = '\0';
    (void)event;
    return 0u;
}

int syscall_compat32_request_core_io(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_OPEN:
            result->value = syscall_compat32_open(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_READ:
            result->value = syscall_compat32_read(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2),
                kernel_syscall_arg_u32(request, 3));
            return 1;
        case SYS_WRITE:
            result->value = syscall_compat32_write(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_CLOSE:
            result->value = syscall_compat32_close(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_DUP2:
            result->value = syscall_compat32_dup2(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_PIPE:
            result->value = syscall_compat32_pipe(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SEEK:
            result->value = syscall_compat32_seek(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_i32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        default:
            return 0;
    }
}

int syscall_compat32_request_core_fs(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MKDIR:
            result->value = syscall_compat32_mkdir(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_RMDIR:
            result->value = syscall_compat32_rmdir(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_REMOVE:
            result->value = syscall_compat32_remove(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_CHDIR:
            result->value = syscall_compat32_chdir(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_GETCWD:
            result->value = syscall_compat32_getcwd(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_OPENDIR:
            result->value = syscall_compat32_opendir(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_READDIR:
            result->value = syscall_compat32_readdir(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_SWITCH_ROOT:
            result->value = (uint32_t)-1;
            return 1;
        default:
            return 0;
    }
}

int syscall_compat32_request_core_proc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_EXIT:
            result->value = kernel_syscall_arg_u32(request, 0);
            result->action = SYSCALL_RESULT_EXIT;
            return 1;
        case SYS_YIELD:
            result->value = 0u;
            result->action = SYSCALL_RESULT_YIELD;
            return 1;
        case SYS_EXEC_REPLACE:
            result->value = kernel_syscall_arg_u32(request, 0);
            result->action = SYSCALL_RESULT_EXEC;
            return 1;
        case SYS_EXEC:
            result->value = syscall_compat32_exec(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_FORK:
            result->value = (uint32_t)-1;
            return 1;
        case SYS_WAIT:
            result->value = kernel_syscall_arg_u32(request, 0);
            result->action = SYSCALL_RESULT_WAIT;
            return 1;
        case SYS_SLEEP:
            result->value = kernel_syscall_arg_u32(request, 0);
            result->action = SYSCALL_RESULT_SLEEP;
            return 1;
        case SYS_GETPID:
            result->value = ctx->pid;
            return 1;
        case SYS_PROC_QUERY:
            result->value = syscall_compat32_proc_query(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_KILL:
            result->value = syscall_compat32_kill(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_FG:
            result->value = syscall_compat32_fg(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_BG:
            result->value = syscall_compat32_bg(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SPAWN:
            result->value = syscall_compat32_spawn(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        default:
            return 0;
    }
}

int syscall_compat32_request_core_mount(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MOUNT:
            result->value = syscall_compat32_mount(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_UMOUNT:
            result->value = syscall_compat32_umount(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        default:
            return 0;
    }
}

int syscall_compat32_request_core_query(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0 ||
        request->number != SYS_QUERY) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    result->value = syscall_compat32_query(
        ctx,
        kernel_syscall_arg_u32(request, 0),
        kernel_syscall_arg_u32(request, 1),
        kernel_syscall_arg_u32(request, 2),
        kernel_syscall_arg_u32(request, 3));
    return 1;
}

int syscall_compat32_request_core_mem(
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

int syscall_compat32_request_core_ipc(
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

int syscall_compat32_request_core_misc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_CLEAR:
            if (ctx->tty != 0) {
                tty_clear(ctx->tty);
            }
            result->value = 0u;
            return 1;
        case SYS_TICKS:
            result->value = ctx->ticks;
            return 1;
        case SYS_BLOCK_READ:
            result->value = syscall_compat32_block_read(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_BLOCK_WRITE:
            result->value = syscall_compat32_block_write(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_BLOCK_FLUSH:
            result->value = kernel_block_flush(kernel_syscall_arg_u32(request, 0)) ? 1u : 0u;
            return 1;
        case SYS_AUDIO_TONE:
            result->value = kernel_audio_play_tone(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2))
                ? 1u
                : 0u;
            return 1;
        case SYS_AUDIO_PLAY:
            result->value = syscall_compat32_audio_play(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_AUDIO_PLAY_FD:
            result->value = (uint32_t)-1;
            return 1;
        case SYS_RTL8139_TX_TEST:
            result->value = kernel_rtl8139_send_test_frame() ? 1u : 0u;
            return 1;
        case SYS_RTL8139_TX_SEND:
            result->value = syscall_compat32_rtl8139_tx_send(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_RTL8139_RX_DUMP:
            result->value = syscall_compat32_rtl8139_rx_dump(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_GFX:
            result->value = syscall_compat32_gfx(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_GUI_EVENT:
            result->value = syscall_compat32_gui_event(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_CLIPBOARD:
            result->value = syscall_compat32_clipboard(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_REBOOT:
            result->value = (uint32_t)-1;
            return 1;
        case SYS_CAPABILITY_EVENT:
            result->value = syscall_compat32_capability_event(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        default:
            result->value = 0u;
            return 1;
    }
}

int syscall_compat32_dispatch_request(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    return syscall_compat32_request_core_io(ctx, request, result) ||
           syscall_compat32_request_core_fs(ctx, request, result) ||
           syscall_compat32_request_core_proc(ctx, request, result) ||
           syscall_compat32_request_core_mount(ctx, request, result) ||
           syscall_compat32_request_core_query(ctx, request, result) ||
           syscall_compat32_request_core_mem(ctx, request, result) ||
           syscall_compat32_request_core_ipc(ctx, request, result) ||
           syscall_compat32_request_core_misc(ctx, request, result);
}

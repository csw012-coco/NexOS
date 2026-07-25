#include "kernel/internal/mem/address_space_internal.h"
#include "abi/syscall_abi.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/mem/pmm.h"
#include "hal/hal.h"
#include "lib/string.h"

enum {
    ADDRSPACE_SHM_MAX = 16,
    ADDRSPACE_SHM_NAME_MAX = 31,
    ADDRSPACE_SHM_PAGE_MAX = 256
};

struct addrspace_shm_object {
    uint8_t used;
    uint8_t linked;
    uint16_t mapping_refs;
    uint32_t page_count;
    char name[ADDRSPACE_SHM_NAME_MAX + 1];
    uint64_t pages[ADDRSPACE_SHM_PAGE_MAX];
};

static struct addrspace_shm_object g_shm_objects[ADDRSPACE_SHM_MAX];

static void addrspace_shm_destroy(struct addrspace_shm_object *object);

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static struct user_page_mapping *addrspace_find_mapping(uint64_t virt_addr) {
    uint64_t page = align_down(virt_addr, USER_PAGE_SIZE);

    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        if (g_bound_mappings[i].used && g_bound_mappings[i].virt_addr == page) {
            return &g_bound_mappings[i];
        }
    }
    return 0;
}

static struct user_page_mapping *addrspace_alloc_mapping_slot(void) {
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        if (!g_bound_mappings[i].used) {
            return &g_bound_mappings[i];
        }
    }
    return 0;
}

static void addrspace_clear_mapping(struct user_page_mapping *mapping) {
    if (mapping == 0) {
        return;
    }
    mapping->used = 0;
    mapping->virt_addr = 0;
    mapping->phys_addr = 0;
    mapping->reserved_pool = 0;
    mapping->shared = 0;
    mapping->writable = 0;
    mapping->shm_slot = 0;
}

static int addrspace_try_alloc_phys_page(uint64_t *phys_addr_out, int *reserved_pool_out) {
    if (phys_addr_out == 0 || reserved_pool_out == 0) {
        return 0;
    }

    *phys_addr_out = pmm_alloc_page();
    *reserved_pool_out = 0;
    return *phys_addr_out != 0;
}

static void addrspace_release_mapping(struct user_page_mapping *mapping) {
    uint64_t phys_addr;
    struct addrspace_shm_object *object = 0;

    if (mapping == 0 || !mapping->used) {
        return;
    }
    if (mapping->shared && mapping->shm_slot < ADDRSPACE_SHM_MAX) {
        object = &g_shm_objects[mapping->shm_slot];
    }
    if (vmm_unmap(mapping->virt_addr, &phys_addr) && !mapping->reserved_pool) {
        pmm_free_page(phys_addr);
    }
    if (object != 0 && object->used && object->mapping_refs != 0) {
        object->mapping_refs--;
        if (!object->linked && object->mapping_refs == 0) {
            for (uint32_t i = 0; i < object->page_count; i++) {
                pmm_release_page(object->pages[i]);
            }
            memset(object, 0, sizeof(*object));
        }
    }
    addrspace_clear_mapping(mapping);
}

static void addrspace_release_shared_ref(struct user_page_mapping *mapping) {
    struct addrspace_shm_object *object;

    if (mapping == 0 ||
        !mapping->shared ||
        mapping->shm_slot >= ADDRSPACE_SHM_MAX) {
        return;
    }
    object = &g_shm_objects[mapping->shm_slot];
    if (!object->used || object->mapping_refs == 0u) {
        return;
    }
    object->mapping_refs--;
    if (!object->linked && object->mapping_refs == 0u) {
        addrspace_shm_destroy(object);
    }
}

int addrspace_release_page_with_backend(
    uint64_t virt_addr,
    int32_t (*page_free)(uint32_t user_page),
    int32_t (*shared_page_unmap)(uint32_t user_page)) {
    struct user_page_mapping *mapping = addrspace_find_mapping(virt_addr);
    uint32_t page = (uint32_t)align_down(virt_addr, USER_PAGE_SIZE);

    if (mapping == 0) {
        return 0;
    }
    if (mapping->shared) {
        if (shared_page_unmap == 0 || shared_page_unmap(page) != 0) {
            return 0;
        }
        addrspace_release_shared_ref(mapping);
    } else {
        if (page_free == 0 || page_free(page) != 0) {
            return 0;
        }
    }
    addrspace_clear_mapping(mapping);
    return 1;
}

int addrspace_release_page_for_pid_with_backend(
    uint32_t pid,
    uint64_t virt_addr,
    int32_t (*page_free_pid)(uint32_t pid, uint32_t user_page),
    int32_t (*shared_page_unmap_pid)(uint32_t pid, uint32_t user_page)) {
    struct user_page_mapping *mapping = addrspace_find_mapping(virt_addr);
    uint32_t page = (uint32_t)align_down(virt_addr, USER_PAGE_SIZE);

    if (mapping == 0 || pid == 0u) {
        return 0;
    }
    if (mapping->shared) {
        if (shared_page_unmap_pid == 0 ||
            shared_page_unmap_pid(pid, page) != 0) {
            return 0;
        }
        addrspace_release_shared_ref(mapping);
    } else {
        if (page_free_pid == 0 || page_free_pid(pid, page) != 0) {
            return 0;
        }
    }
    addrspace_clear_mapping(mapping);
    return 1;
}

static void addrspace_rollback_retained_shared_refs(
    struct user_page_mapping *mappings,
    uint32_t count) {
    if (mappings == 0) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        struct user_page_mapping *mapping = &mappings[i];
        struct addrspace_shm_object *object;

        if (!mapping->used ||
            !mapping->shared ||
            mapping->shm_slot >= ADDRSPACE_SHM_MAX ||
            !g_shm_objects[mapping->shm_slot].used) {
            continue;
        }
        object = &g_shm_objects[mapping->shm_slot];
        if (object->mapping_refs != 0) {
            object->mapping_refs--;
        }
        if (!object->linked && object->mapping_refs == 0) {
            addrspace_shm_destroy(object);
        }
    }
}

int addrspace_map_page_at(uint64_t virt_addr, uint32_t perms) {
    struct user_page_mapping *mapping;
    uint64_t phys_addr;
    uint64_t page = align_down(virt_addr, USER_PAGE_SIZE);
    int reserved_pool = 0;

    if (addrspace_find_mapping(page) != 0) {
        return 1;
    }

    mapping = addrspace_alloc_mapping_slot();
    if (mapping == 0) {
        return 0;
    }

    if (!addrspace_try_alloc_phys_page(&phys_addr, &reserved_pool)) {
        return 0;
    }
    if (!vmm_map(page,
                 phys_addr,
                 VMM_PERM_USER | perms)) {
        if (!reserved_pool) {
            pmm_free_page(phys_addr);
        }
        return 0;
    }
    vmm_allow_user_page(page);

    mapping->virt_addr = page;
    mapping->phys_addr = phys_addr;
    mapping->used = 1;
    mapping->reserved_pool = reserved_pool ? 1u : 0u;
    mapping->shared = 0;
    mapping->writable = (perms & VMM_PERM_WRITE) != 0;
    mapping->shm_slot = 0;
    (void)vmm_zero_range(page, USER_PAGE_SIZE);
    return 1;
}

int addrspace_map_range(uint64_t start, uint64_t end) {
    uint64_t page = align_down(start, USER_PAGE_SIZE);
    uint64_t page_end = align_up(end, USER_PAGE_SIZE);

    while (page < page_end) {
        if (!addrspace_map_page_at(page, VMM_PERM_WRITE)) {
            return 0;
        }
        page += USER_PAGE_SIZE;
    }
    return 1;
}

int addrspace_map_range_with_perms(uint64_t start, uint64_t end, uint32_t perms) {
    uint64_t page = align_down(start, USER_PAGE_SIZE);
    uint64_t page_end = align_up(end, USER_PAGE_SIZE);

    while (page < page_end) {
        if (!addrspace_map_page_at(page, perms)) {
            return 0;
        }
        page += USER_PAGE_SIZE;
    }
    return 1;
}

void addrspace_unmap_range_if_present(uint64_t start, uint64_t end) {
    vmm_unmap_range_if_present(start, end);
}

int addrspace_zero_range(uint64_t start, uint64_t size) {
    return vmm_zero_range(start, size);
}

int addrspace_copy_to_range(uint64_t dest, const uint8_t *src, uint64_t size) {
    return vmm_copy_to_range(dest, src, size);
}

void addrspace_release_dynamic_pages(void) {
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        addrspace_release_mapping(&g_bound_mappings[i]);
    }
    g_next_user_alloc = USER_ALLOC_BASE;
    g_bound_session->address_space.reserved_phys_next = g_bound_session->address_space.reserved_phys_base;
}

void addrspace_release_dynamic_pages_with_backend(
    int32_t (*page_free)(uint32_t user_page),
    int32_t (*shared_page_unmap)(uint32_t user_page)) {
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        struct user_page_mapping *mapping = &g_bound_mappings[i];

        if (!mapping->used) {
            continue;
        }
        (void)addrspace_release_page_with_backend(mapping->virt_addr,
                                                  page_free,
                                                  shared_page_unmap);
    }
    g_next_user_alloc = USER_ALLOC_BASE;
    g_bound_session->address_space.reserved_phys_next = g_bound_session->address_space.reserved_phys_base;
}

void addrspace_release_dynamic_pages_for_pid_with_backend(
    uint32_t pid,
    int32_t (*page_free_pid)(uint32_t pid, uint32_t user_page),
    int32_t (*shared_page_unmap_pid)(uint32_t pid, uint32_t user_page)) {
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        struct user_page_mapping *mapping = &g_bound_mappings[i];

        if (!mapping->used) {
            continue;
        }
        (void)addrspace_release_page_for_pid_with_backend(
            pid,
            mapping->virt_addr,
            page_free_pid,
            shared_page_unmap_pid);
    }
    g_next_user_alloc = USER_ALLOC_BASE;
    g_bound_session->address_space.reserved_phys_next = g_bound_session->address_space.reserved_phys_base;
}

uint64_t addrspace_alloc_page(void) {
    uint64_t addr = g_next_user_alloc;

    while (addr < USER_ALLOC_END) {
        if (addrspace_find_mapping(addr) == 0) {
            if (!addrspace_map_page_at(addr, VMM_PERM_WRITE)) {
                return 0;
            }
            g_next_user_alloc = addr + USER_PAGE_SIZE;
            return addr;
        }
        addr += USER_PAGE_SIZE;
    }
    return 0;
}

int addrspace_free_page(uint64_t virt_addr) {
    struct user_page_mapping *mapping = addrspace_find_mapping(virt_addr);

    if (mapping == 0) {
        return 0;
    }
    addrspace_release_mapping(mapping);
    return 1;
}

int addrspace_track_existing_page(uint64_t virt_addr,
                                  uint32_t perms,
                                  int shared,
                                  uint16_t shm_slot) {
    struct user_page_mapping *mapping;
    uint64_t page = align_down(virt_addr, USER_PAGE_SIZE);
    uint64_t phys = 0;

    mapping = addrspace_find_mapping(page);
    if (mapping != 0) {
        mapping->writable = (perms & VMM_PERM_WRITE) != 0;
        mapping->shared = shared ? 1u : 0u;
        mapping->shm_slot = shm_slot;
        return 1;
    }
    mapping = addrspace_alloc_mapping_slot();
    if (mapping == 0 || !vmm_query(page, &phys)) {
        return 0;
    }
    mapping->virt_addr = page;
    mapping->phys_addr = phys;
    mapping->used = 1;
    mapping->reserved_pool = 1;
    mapping->shared = shared ? 1u : 0u;
    mapping->writable = (perms & VMM_PERM_WRITE) != 0;
    mapping->shm_slot = shm_slot;
    return 1;
}

void addrspace_untrack_range(uint64_t start, uint64_t length) {
    uint64_t page;
    uint64_t end;

    if ((start & (USER_PAGE_SIZE - 1u)) != 0 || length == 0) {
        return;
    }
    end = align_up(start + length, USER_PAGE_SIZE);
    if (end <= start) {
        return;
    }
    for (page = start; page < end; page += USER_PAGE_SIZE) {
        struct user_page_mapping *mapping = addrspace_find_mapping(page);

        if (mapping != 0) {
            addrspace_clear_mapping(mapping);
        }
    }
}

int addrspace_note_protect_range(uint64_t start, uint64_t length, uint32_t perms) {
    uint64_t page;
    uint64_t end;

    if ((start & (USER_PAGE_SIZE - 1u)) != 0 || length == 0) {
        return 0;
    }
    end = align_up(start + length, USER_PAGE_SIZE);
    if (end <= start) {
        return 0;
    }
    for (page = start; page < end; page += USER_PAGE_SIZE) {
        struct user_page_mapping *mapping = addrspace_find_mapping(page);

        if (mapping == 0) {
            return 0;
        }
        mapping->writable = (perms & VMM_PERM_WRITE) != 0;
    }
    return 1;
}

int addrspace_page_is_shared(uint64_t virt_addr) {
    struct user_page_mapping *mapping = addrspace_find_mapping(virt_addr);

    return mapping != 0 && mapping->shared;
}

void addrspace_vm_snapshot(struct syscall_vm_info *info) {
    if (info == 0) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->mmap_region_capacity = USER_DYNAMIC_PAGE_LIMIT;
    info->mmap_page_capacity = USER_DYNAMIC_PAGE_LIMIT;
    info->shm_object_capacity = ADDRSPACE_SHM_MAX;
    info->user_stack_pages = NOS_USER_STACK_SIZE / USER_PAGE_SIZE;
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        const struct user_page_mapping *mapping = &g_bound_mappings[i];
        const struct user_page_mapping *previous = 0;

        if (!mapping->used) {
            continue;
        }
        info->mmap_pages++;
        if (mapping->virt_addr >= USER_PAGE_SIZE) {
            previous = addrspace_find_mapping(mapping->virt_addr -
                                             USER_PAGE_SIZE);
        }
        if (previous == 0) {
            info->mmap_regions++;
        }
        if (mapping->shared) {
            info->shm_mapped_pages++;
            if (previous == 0 || !previous->shared) {
                info->shared_regions++;
            }
        }
    }
    for (uint32_t i = 0; i < ADDRSPACE_SHM_MAX; i++) {
        if (g_shm_objects[i].used) {
            info->shm_objects++;
        }
    }
}

static struct addrspace_shm_object *addrspace_shm_by_name(const char *name) {
    for (uint32_t i = 0; i < ADDRSPACE_SHM_MAX; i++) {
        if (g_shm_objects[i].used && g_shm_objects[i].linked &&
            streq(g_shm_objects[i].name, name)) {
            return &g_shm_objects[i];
        }
    }
    return 0;
}

static void addrspace_shm_destroy(struct addrspace_shm_object *object) {
    if (object == 0 || !object->used) {
        return;
    }
    for (uint32_t i = 0; i < object->page_count; i++) {
        pmm_release_page(object->pages[i]);
    }
    memset(object, 0, sizeof(*object));
}

int addrspace_shm_open(const char *name, uint64_t size, uint32_t flags) {
    struct addrspace_shm_object *object;
    uint32_t page_count;
    uint32_t slot;

    if (name == 0 || name[0] == '\0' || str_len(name) > ADDRSPACE_SHM_NAME_MAX) {
        return -1;
    }
    object = addrspace_shm_by_name(name);
    if (object != 0) {
        if ((flags & SYS_SHM_CREATE) && (flags & SYS_SHM_EXCL)) {
            return -1;
        }
        return (int)(object - g_shm_objects) + 1;
    }
    if ((flags & SYS_SHM_CREATE) == 0 || size == 0) {
        return -1;
    }
    page_count = (uint32_t)(align_up(size, USER_PAGE_SIZE) / USER_PAGE_SIZE);
    if (page_count == 0 || page_count > ADDRSPACE_SHM_PAGE_MAX) {
        return -1;
    }
    for (slot = 0; slot < ADDRSPACE_SHM_MAX && g_shm_objects[slot].used; slot++) {
    }
    if (slot == ADDRSPACE_SHM_MAX) {
        return -1;
    }
    object = &g_shm_objects[slot];
    memset(object, 0, sizeof(*object));
    object->used = 1;
    object->linked = 1;
    object->page_count = page_count;
    memcpy(object->name, name, str_len(name) + 1u);
    for (uint32_t i = 0; i < page_count; i++) {
        void *page;

        object->pages[i] = pmm_alloc_page();
        if (object->pages[i] == 0) {
            object->page_count = i;
            addrspace_shm_destroy(object);
            return -1;
        }
        page = hal_phys_direct_map(object->pages[i]);
        if (page == 0) {
            object->page_count = i + 1u;
            addrspace_shm_destroy(object);
            return -1;
        }
        memset(page, 0, USER_PAGE_SIZE);
    }
    return (int)slot + 1;
}

int addrspace_shm_unlink(const char *name) {
    struct addrspace_shm_object *object = addrspace_shm_by_name(name);

    if (object == 0) {
        return 0;
    }
    object->linked = 0;
    object->name[0] = '\0';
    if (object->mapping_refs == 0) {
        addrspace_shm_destroy(object);
    }
    return 1;
}

uint64_t addrspace_shm_frame(uint32_t handle, uint32_t page_index) {
    struct addrspace_shm_object *object;

    if (handle == 0u || handle > ADDRSPACE_SHM_MAX) {
        return 0;
    }
    object = &g_shm_objects[handle - 1u];
    if (!object->used || page_index >= object->page_count) {
        return 0;
    }
    return object->pages[page_index];
}

uint64_t addrspace_shm_size(uint32_t handle) {
    struct addrspace_shm_object *object;

    if (handle == 0u || handle > ADDRSPACE_SHM_MAX) {
        return 0;
    }
    object = &g_shm_objects[handle - 1u];
    return object->used ? object->page_count * USER_PAGE_SIZE : 0;
}

int addrspace_shm_note_mapping(uint32_t handle) {
    struct addrspace_shm_object *object;

    if (handle == 0u || handle > ADDRSPACE_SHM_MAX) {
        return 0;
    }
    object = &g_shm_objects[handle - 1u];
    if (!object->used || object->mapping_refs == 0xffffu) {
        return 0;
    }
    object->mapping_refs++;
    return 1;
}

void addrspace_shm_note_unmapping(uint32_t handle) {
    struct addrspace_shm_object *object;

    if (handle == 0u || handle > ADDRSPACE_SHM_MAX) {
        return;
    }
    object = &g_shm_objects[handle - 1u];
    if (!object->used || object->mapping_refs == 0u) {
        return;
    }
    object->mapping_refs--;
    if (!object->linked && object->mapping_refs == 0u) {
        addrspace_shm_destroy(object);
    }
}

static int addrspace_range_free(uint64_t start, uint64_t end) {
    for (uint64_t page = start; page < end; page += USER_PAGE_SIZE) {
        if (addrspace_find_mapping(page) != 0) {
            return 0;
        }
    }
    return 1;
}

static uint64_t addrspace_find_mmap_range(uint64_t length) {
    uint64_t run = USER_MMAP_BASE;

    while (run + length <= USER_MMAP_END) {
        if (addrspace_range_free(run, run + length)) {
            return run;
        }
        run += USER_PAGE_SIZE;
    }
    return 0;
}

uint64_t addrspace_mmap(uint64_t requested_addr,
                        uint64_t length,
                        uint32_t prot,
                        uint32_t flags,
                        uint32_t shm_handle,
                        uint64_t offset) {
    struct addrspace_shm_object *object = 0;
    uint64_t start;
    uint64_t mapped_end;
    uint64_t rounded;
    uint32_t perms = 0;
    uint32_t first_page;

    if (length == 0 || (flags & (SYS_MAP_PRIVATE | SYS_MAP_SHARED)) == 0 ||
        ((flags & SYS_MAP_PRIVATE) && (flags & SYS_MAP_SHARED))) {
        return 0;
    }
    rounded = align_up(length, USER_PAGE_SIZE);
    if (rounded == 0 || rounded > USER_MMAP_END - USER_MMAP_BASE) {
        return 0;
    }
    if (prot & SYS_PROT_WRITE) {
        perms |= VMM_PERM_WRITE;
    }
    if (prot & SYS_PROT_EXEC) {
        perms |= VMM_PERM_EXEC;
    }
    if ((flags & SYS_MAP_ANONYMOUS) == 0) {
        if ((flags & SYS_MAP_SHARED) == 0 || shm_handle == 0 ||
            shm_handle > ADDRSPACE_SHM_MAX || (offset & (USER_PAGE_SIZE - 1u)) != 0) {
            return 0;
        }
        object = &g_shm_objects[shm_handle - 1u];
        first_page = (uint32_t)(offset / USER_PAGE_SIZE);
        if (!object->used || first_page + rounded / USER_PAGE_SIZE > object->page_count) {
            return 0;
        }
    } else {
        first_page = 0;
    }
    if (flags & SYS_MAP_FIXED) {
        if ((requested_addr & (USER_PAGE_SIZE - 1u)) != 0 ||
            requested_addr < USER_MMAP_BASE || requested_addr + rounded > USER_MMAP_END ||
            !addrspace_range_free(requested_addr, requested_addr + rounded)) {
            return 0;
        }
        start = requested_addr;
    } else {
        start = addrspace_find_mmap_range(rounded);
        if (start == 0) {
            return 0;
        }
    }

    mapped_end = start;
    while (mapped_end < start + rounded) {
        struct user_page_mapping *mapping = addrspace_alloc_mapping_slot();
        uint64_t phys;

        if (mapping == 0) {
            break;
        }
        if (object != 0) {
            uint32_t index = first_page + (uint32_t)((mapped_end - start) / USER_PAGE_SIZE);
            phys = object->pages[index];
            if (!pmm_retain_page(phys)) {
                break;
            }
        } else {
            phys = pmm_alloc_page();
            if (phys == 0) {
                break;
            }
        }
        if (!vmm_map(mapped_end, phys, VMM_PERM_USER | perms)) {
            pmm_release_page(phys);
            break;
        }
        vmm_allow_user_page(mapped_end);
        mapping->virt_addr = mapped_end;
        mapping->phys_addr = phys;
        mapping->used = 1;
        mapping->reserved_pool = 0;
        mapping->shared = object != 0 || (flags & SYS_MAP_SHARED) != 0;
        mapping->writable = (perms & VMM_PERM_WRITE) != 0;
        mapping->shm_slot = object != 0
                                ? (uint16_t)(object - g_shm_objects)
                                : (uint16_t)0xffffu;
        if (object != 0) {
            object->mapping_refs++;
        } else {
            (void)vmm_zero_range(mapped_end, USER_PAGE_SIZE);
        }
        mapped_end += USER_PAGE_SIZE;
    }
    if (mapped_end != start + rounded) {
        (void)addrspace_munmap(start, mapped_end - start);
        return 0;
    }
    return start;
}

int addrspace_munmap(uint64_t addr, uint64_t length) {
    uint64_t end;
    int unmapped = 0;

    if ((addr & (USER_PAGE_SIZE - 1u)) != 0 || length == 0) {
        return 0;
    }
    end = align_up(addr + length, USER_PAGE_SIZE);
    if (end <= addr) {
        return 0;
    }
    for (uint64_t page = addr; page < end; page += USER_PAGE_SIZE) {
        struct user_page_mapping *mapping = addrspace_find_mapping(page);

        if (mapping != 0) {
            addrspace_release_mapping(mapping);
            unmapped = 1;
        }
    }
    return unmapped;
}

int addrspace_fork_retain_shared(uint64_t child_root,
                                struct user_page_mapping *child_mappings) {
    uint64_t parent_root = vmm_current_root();

    if (child_root == 0 || child_mappings == 0 || !vmm_switch_root_or_fail(child_root)) {
        return 0;
    }
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        struct user_page_mapping *mapping = &child_mappings[i];
        uint64_t phys;
        uint32_t perms;

        if (!mapping->used || !mapping->shared) {
            continue;
        }
        if (!vmm_unmap(mapping->virt_addr, &phys)) {
            addrspace_rollback_retained_shared_refs(child_mappings, i);
            (void)vmm_switch_root_or_fail(parent_root);
            return 0;
        }
        perms = VMM_PERM_USER | (mapping->writable ? VMM_PERM_WRITE : 0);
        if (!vmm_map(mapping->virt_addr, mapping->phys_addr, perms)) {
            addrspace_rollback_retained_shared_refs(child_mappings, i);
            (void)vmm_switch_root_or_fail(parent_root);
            return 0;
        }
        if (mapping->shm_slot < ADDRSPACE_SHM_MAX &&
            g_shm_objects[mapping->shm_slot].used) {
            g_shm_objects[mapping->shm_slot].mapping_refs++;
        }
    }
    if (!vmm_switch_root_or_fail(parent_root)) {
        addrspace_rollback_retained_shared_refs(child_mappings,
                                                USER_DYNAMIC_PAGE_LIMIT);
        return 0;
    }
    return 1;
}

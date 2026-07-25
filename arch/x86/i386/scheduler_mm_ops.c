#include "paging.h"
#include "pmm.h"
#include "kernel/public/proc/process_mm_ops.h"
#include "scheduler_internal.h"
#include "lib/string.h"

void kernel_i386_syscall_cleanup_pid(uint32_t pid) __attribute__((weak));

static void scheduler_mm_cleanup_pid(uint32_t pid) {
    if (kernel_i386_syscall_cleanup_pid != 0) {
        kernel_i386_syscall_cleanup_pid(pid);
    }
}

static int scheduler_mm_user_page_valid(uint32_t user_page) {
    return user_page >= I386_USER_HEAP_BASE &&
           user_page < I386_USER_HEAP_LIMIT &&
           (user_page & (I386_PAGE_SIZE - 1u)) == 0u;
}

uint32_t i386_scheduler_page_alloc_with_prot(int writable) {
    struct scheduler_task *task;
    uint32_t kernel_root;

    task = i386_scheduler_current_task_mut();
    if (task == 0) {
        return 0u;
    }
    kernel_root = i386_paging_kernel_root();
    i386_paging_switch(kernel_root);
    while (task->heap_next < I386_USER_HEAP_LIMIT) {
        uint32_t address = task->heap_next;
        uint32_t existing;
        uint32_t frame;

        task->heap_next += I386_PAGE_SIZE;
        if (i386_paging_translate_in(task->root, address, &existing)) {
            continue;
        }
        frame = i386_pmm_alloc_page();
        if (frame == I386_PMM_INVALID_PAGE ||
            !i386_paging_map_page_in(task->root,
                                     address,
                                     frame,
                                     writable ? 1 : 0,
                                     1)) {
            if (frame != I386_PMM_INVALID_PAGE) {
                (void)i386_pmm_free_page(frame);
            }
            i386_paging_switch(task->root);
            return 0u;
        }
        {
            void *temporary;

            if (!i386_paging_temporary_map(frame, 2u, &temporary)) {
                uint32_t ignored;

                (void)i386_paging_unmap_page_in(task->root, address, &ignored);
                (void)i386_pmm_free_page(frame);
                i386_paging_switch(task->root);
                return 0u;
            }
            memset(temporary, 0, I386_PAGE_SIZE);
            i386_paging_temporary_unmap(2u);
        }
        i386_paging_switch(task->root);
        return address;
    }
    i386_paging_switch(task->root);
    return 0u;
}

uint32_t i386_scheduler_page_alloc(void) {
    return i386_scheduler_page_alloc_with_prot(1);
}

uint32_t i386_scheduler_page_alloc_at(uint32_t user_page, int writable) {
    struct scheduler_task *task;
    uint32_t frame;
    uint32_t existing;
    void *temporary;

    if (!scheduler_mm_user_page_valid(user_page)) {
        return 0u;
    }
    task = i386_scheduler_current_task_mut();
    if (task == 0 || i386_paging_translate_in(task->root, user_page, &existing)) {
        return 0u;
    }
    frame = i386_pmm_alloc_page();
    if (frame == I386_PMM_INVALID_PAGE) {
        return 0u;
    }
    i386_paging_switch(i386_paging_kernel_root());
    if (!i386_paging_map_page_in(task->root,
                                 user_page,
                                 frame,
                                 writable ? 1 : 0,
                                 1) ||
        !i386_paging_temporary_map(frame, 2u, &temporary)) {
        uint32_t ignored;

        (void)i386_paging_unmap_page_in(task->root, user_page, &ignored);
        (void)i386_pmm_free_page(frame);
        i386_paging_switch(task->root);
        return 0u;
    }
    memset(temporary, 0, I386_PAGE_SIZE);
    i386_paging_temporary_unmap(2u);
    i386_paging_switch(task->root);
    return user_page;
}

int32_t i386_scheduler_page_protect(uint32_t user_page, int writable) {
    struct scheduler_task *task;
    int ok;

    if (!scheduler_mm_user_page_valid(user_page)) {
        return -1;
    }
    task = i386_scheduler_current_task_mut();
    if (task == 0) {
        return -1;
    }
    i386_paging_switch(i386_paging_kernel_root());
    ok = i386_paging_protect_page_in(task->root,
                                     user_page,
                                     writable ? 1 : 0,
                                     1);
    i386_paging_switch(task->root);
    return ok ? 0 : -1;
}

uint32_t i386_scheduler_shared_page_alloc(void) {
    uint32_t frame;
    uint32_t current_root;
    void *temporary;

    current_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    frame = i386_pmm_alloc_page();
    if (frame == I386_PMM_INVALID_PAGE) {
        i386_paging_switch(current_root);
        return 0u;
    }
    if (!i386_paging_temporary_map(frame, 2u, &temporary)) {
        (void)i386_pmm_free_page(frame);
        i386_paging_switch(current_root);
        return 0u;
    }
    memset(temporary, 0, I386_PAGE_SIZE);
    i386_paging_temporary_unmap(2u);
    i386_paging_switch(current_root);
    return frame;
}

int32_t i386_scheduler_shared_page_free(uint32_t frame) {
    uint32_t current_root;
    int ok;

    if (frame == 0u || (frame & (I386_PAGE_SIZE - 1u)) != 0u) {
        return -1;
    }
    current_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    ok = i386_pmm_free_page(frame);
    i386_paging_switch(current_root);
    return ok ? 0 : -1;
}

uint32_t i386_scheduler_shared_phys_alloc(void) {
    return i386_scheduler_shared_page_alloc();
}

int32_t i386_scheduler_shared_phys_free(uint32_t frame) {
    return i386_scheduler_shared_page_free(frame);
}

uint32_t i386_scheduler_shared_page_map(uint32_t frame) {
    struct scheduler_task *task;
    uint32_t kernel_root;

    if (frame == 0u || (frame & (I386_PAGE_SIZE - 1u)) != 0u) {
        return 0u;
    }
    task = i386_scheduler_current_task_mut();
    if (task == 0) {
        return 0u;
    }
    kernel_root = i386_paging_kernel_root();
    i386_paging_switch(kernel_root);
    while (task->heap_next < I386_USER_HEAP_LIMIT) {
        uint32_t address = task->heap_next;
        uint32_t existing;

        task->heap_next += I386_PAGE_SIZE;
        if (i386_paging_translate_in(task->root, address, &existing)) {
            continue;
        }
        if (!i386_pmm_retain_page(frame)) {
            i386_paging_switch(task->root);
            return 0u;
        }
        if (!i386_paging_map_page_in(task->root, address, frame, 1, 1)) {
            (void)i386_pmm_free_page(frame);
            i386_paging_switch(task->root);
            return 0u;
        }
        i386_paging_switch(task->root);
        return address;
    }
    i386_paging_switch(task->root);
    return 0u;
}

uint32_t i386_scheduler_shared_phys_map(uint32_t frame) {
    return i386_scheduler_shared_page_map(frame);
}

int32_t i386_scheduler_shared_page_unmap(uint32_t user_page) {
    struct scheduler_task *task;
    uint32_t frame;

    if (!scheduler_mm_user_page_valid(user_page)) {
        return -1;
    }
    task = i386_scheduler_current_task_mut();
    if (task == 0) {
        return -1;
    }
    i386_paging_switch(i386_paging_kernel_root());
    if (!i386_paging_unmap_page_in(task->root, user_page, &frame)) {
        i386_paging_switch(task->root);
        return -1;
    }
    (void)i386_pmm_free_page(frame);
    if (user_page < task->heap_next) {
        task->heap_next = user_page;
    }
    i386_paging_switch(task->root);
    return 0;
}

int32_t i386_scheduler_shared_phys_unmap(uint32_t user_page) {
    return i386_scheduler_shared_page_unmap(user_page);
}

int32_t i386_scheduler_shared_page_unmap_pid(uint32_t pid, uint32_t user_page) {
    struct scheduler_task *task;
    uint32_t frame;
    uint32_t current_root;

    if (!i386_scheduler_is_active() || !scheduler_mm_user_page_valid(user_page)) {
        return -1;
    }
    task = i386_scheduler_task_by_pid(pid);
    if (task == 0 || task->root == 0u) {
        return -1;
    }
    current_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    if (!i386_paging_unmap_page_in(task->root, user_page, &frame)) {
        i386_paging_switch(current_root);
        return -1;
    }
    (void)i386_pmm_free_page(frame);
    if (user_page < task->heap_next) {
        task->heap_next = user_page;
    }
    i386_paging_switch(current_root);
    return 0;
}

int32_t i386_scheduler_page_free(uint32_t user_page) {
    struct scheduler_task *task;
    uint32_t frame;

    if (!scheduler_mm_user_page_valid(user_page)) {
        return -1;
    }
    task = i386_scheduler_current_task_mut();
    if (task == 0) {
        return -1;
    }
    i386_paging_switch(i386_paging_kernel_root());
    if (!i386_paging_unmap_page_in(task->root, user_page, &frame) ||
        !i386_pmm_free_page(frame)) {
        i386_paging_switch(task->root);
        return -1;
    }
    if (user_page < task->heap_next) {
        task->heap_next = user_page;
    }
    i386_paging_switch(task->root);
    return 0;
}

int32_t i386_scheduler_page_free_pid(uint32_t pid, uint32_t user_page) {
    struct scheduler_task *task;
    uint32_t frame;
    uint32_t current_root;

    if (!i386_scheduler_is_active() || !scheduler_mm_user_page_valid(user_page)) {
        return -1;
    }
    task = i386_scheduler_task_by_pid(pid);
    if (task == 0 || task->root == 0u) {
        return -1;
    }
    current_root = i386_paging_root();
    i386_paging_switch(i386_paging_kernel_root());
    if (!i386_paging_unmap_page_in(task->root, user_page, &frame) ||
        !i386_pmm_free_page(frame)) {
        i386_paging_switch(current_root);
        return -1;
    }
    if (user_page < task->heap_next) {
        task->heap_next = user_page;
    }
    i386_paging_switch(current_root);
    return 0;
}

static const struct process_mm_ops i386_process_mm_ops = {
    i386_scheduler_page_alloc,
    i386_scheduler_page_alloc_with_prot,
    i386_scheduler_page_alloc_at,
    i386_scheduler_page_protect,
    i386_scheduler_page_free,
    i386_scheduler_page_free_pid,
    i386_scheduler_shared_phys_alloc,
    i386_scheduler_shared_phys_free,
    i386_scheduler_shared_phys_map,
    i386_scheduler_shared_phys_unmap,
    i386_scheduler_shared_page_unmap_pid,
    scheduler_mm_cleanup_pid
};

void i386_scheduler_register_mm_ops(void) {
    process_mm_ops_register(&i386_process_mm_ops);
}

#include "paging.h"
#include "pmm.h"
#include "scheduler_internal.h"
#include "user.h"

int i386_scheduler_handle_page_fault(uint32_t fault_address,
                                     uint32_t error_code) {
    enum {
        PAGE_FAULT_PRESENT = 1u,
        PAGE_FAULT_USER = 4u
    };
    struct scheduler_task *task;
    uint32_t page;
    uint32_t map_page;
    uint32_t old_root;

    if (!scheduler_active ||
        (error_code & PAGE_FAULT_USER) == 0u) {
        return 0;
    }
    task = &tasks[i386_scheduler_current_slot()];
    old_root = i386_paging_root();
    i386_paging_switch(scheduler_kernel_root);
    if (i386_paging_resolve_cow_fault(task->root,
                                      fault_address,
                                      error_code)) {
        i386_paging_switch(old_root);
        return 1;
    }
    if ((error_code & PAGE_FAULT_PRESENT) != 0u) {
        i386_paging_switch(old_root);
        return 0;
    }
    page = fault_address & ~(I386_PAGE_SIZE - 1u);
    if (task->stack_top == 0u ||
        page < task->stack_limit ||
        page > task->stack_low) {
        i386_paging_switch(old_root);
        return 0;
    }

    for (map_page = page == task->stack_low
                        ? task->stack_low
                        : task->stack_low - I386_PAGE_SIZE;
         map_page >= page;
         map_page -= I386_PAGE_SIZE) {
        uint32_t frame;
        uint32_t existing;
        void *temporary;

        if (i386_paging_translate_in(task->root, map_page, &existing)) {
            if (map_page == page) {
                break;
            }
            continue;
        }
        frame = i386_pmm_alloc_page();
        if (frame == I386_PMM_INVALID_PAGE ||
            !i386_paging_map_page_in(task->root, map_page, frame, 1, 1)) {
            if (frame != I386_PMM_INVALID_PAGE) {
                (void)i386_pmm_free_page(frame);
            }
            i386_paging_switch(old_root);
            return 0;
        }

        i386_paging_switch(scheduler_kernel_root);
        if (!i386_paging_temporary_map(frame, 2u, &temporary)) {
            uint32_t ignored;

            (void)i386_paging_unmap_page_in(task->root, map_page, &ignored);
            (void)i386_pmm_free_page(frame);
            i386_paging_switch(old_root);
            return 0;
        }
        for (uint32_t i = 0u; i < I386_PAGE_SIZE; i++) {
            ((uint8_t *)temporary)[i] = 0u;
        }
        i386_paging_temporary_unmap(2u);
        if (map_page == page) {
            break;
        }
    }
    i386_paging_switch(old_root);
    task->stack_low = page;
    task->stack_grow_events++;
    return 1;
}

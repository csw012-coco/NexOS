#include "kernel/internal/proc/process_reap_internal.h"

static void userprog_release_process_slot(uint32_t slot) {
    g_process_slot_used[slot] = 0;
    g_process_slots[slot].pid = 0;
    g_process_slots[slot].state = PROCESS_STATE_FREE;
    g_process_slots[slot].exit_code = 0;
    g_process_slots[slot].name = 0;
    g_process_slots[slot].name_storage[0] = '\0';
    g_process_slots[slot].image_kind = PROCESS_IMAGE_NONE;
    g_process_slots[slot].entry = 0;
    g_process_slots[slot].stack_top = 0;
    g_process_slots[slot].address_space = 0;
}

static void userprog_clear_last_exited_process(void) {
    g_last_exited_process.pid = 0;
    g_last_exited_process.slot = 0;
    g_last_exited_process.state = PROCESS_STATE_FREE;
    g_last_exited_process.exit_code = 0;
    g_last_exited_process.name = 0;
    g_last_exited_process.name_storage[0] = '\0';
    g_last_exited_process.image_kind = PROCESS_IMAGE_NONE;
    g_last_exited_process.entry = 0;
    g_last_exited_process.stack_top = 0;
    g_last_exited_process.address_space = 0;
}

uint32_t process_capacity(void) {
    return USER_PROCESS_LIMIT;
}

int process_get(uint32_t slot, struct process_snapshot *out) {
    if (slot >= USER_PROCESS_LIMIT || !g_process_slot_used[slot]) {
        return 0;
    }
    process_snapshot_fill(out, &g_process_slots[slot]);
    return 1;
}

int process_get_last_exit(struct process_snapshot *out) {
    if (g_last_exited_process.state != PROCESS_STATE_EXITED) {
        return 0;
    }
    process_snapshot_fill(out, &g_last_exited_process);
    return 1;
}

int process_wait_last(struct process_snapshot *out) {
    uint32_t slot;

    if (g_last_exited_process.state != PROCESS_STATE_EXITED ||
        g_last_exited_process.slot >= USER_PROCESS_LIMIT ||
        !g_process_slot_used[g_last_exited_process.slot]) {
        return 0;
    }

    slot = g_last_exited_process.slot;
    process_snapshot_fill(out, &g_process_slots[slot]);

    process_discard_files(&g_process_slots[slot]);
    userprog_release_process_slot(slot);
    userprog_clear_last_exited_process();
    return 1;
}

int process_wait_pid(uint32_t pid, struct process_snapshot *out) {
    for (uint32_t i = 0; i < USER_PROCESS_LIMIT; i++) {
        if (!g_process_slot_used[i]) {
            continue;
        }
        if (g_process_slots[i].state != PROCESS_STATE_EXITED || g_process_slots[i].pid != pid) {
            continue;
        }
        process_snapshot_fill(out, &g_process_slots[i]);
        process_discard_files(&g_process_slots[i]);
        userprog_release_process_slot(i);
        if (g_last_exited_process.state == PROCESS_STATE_EXITED && g_last_exited_process.pid == pid) {
            process_refresh_name_ptr(&g_last_exited_process);
        }
        return 1;
    }
    return 0;
}

void job_reset_runtime(struct job_runtime *runtime) {
    if (runtime == 0) {
        return;
    }
    runtime->used = 0;
    runtime->entry = 0;
    runtime->stack_top = 0;
    addrspace_reset(&runtime->session.address_space);
    process_clear_slot_state(&runtime->session.process);
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        runtime->mappings[i].used = 0;
        runtime->mappings[i].virt_addr = 0;
        runtime->mappings[i].phys_addr = 0;
        runtime->mappings[i].reserved_pool = 0;
    }
}

struct job_runtime *job_get_runtime(uint32_t slot) {
    if (slot >= USER_PROCESS_LIMIT || !g_bg_runtimes[slot].used) {
        return 0;
    }
    return &g_bg_runtimes[slot];
}

struct job_runtime *job_find_runtime_by_pid(uint32_t pid) {
    for (uint32_t i = 0; i < USER_PROCESS_LIMIT; i++) {
        if (!g_bg_runtimes[i].used) {
            continue;
        }
        if (g_bg_runtimes[i].session.process.pid == pid) {
            return &g_bg_runtimes[i];
        }
    }
    return 0;
}

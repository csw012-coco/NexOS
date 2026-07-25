#include "kernel/public/proc/process_mm_ops.h"

static const struct process_mm_ops *g_process_mm_ops;

void process_mm_ops_register(const struct process_mm_ops *ops) {
    g_process_mm_ops = ops;
}

uint32_t process_mm_page_alloc(void) {
    return g_process_mm_ops != 0 && g_process_mm_ops->page_alloc != 0
               ? g_process_mm_ops->page_alloc()
               : 0u;
}

uint32_t process_mm_page_alloc_prot(int writable) {
    return g_process_mm_ops != 0 && g_process_mm_ops->page_alloc_prot != 0
               ? g_process_mm_ops->page_alloc_prot(writable)
               : 0u;
}

uint32_t process_mm_page_alloc_at(uint32_t user_page, int writable) {
    return g_process_mm_ops != 0 && g_process_mm_ops->page_alloc_at != 0
               ? g_process_mm_ops->page_alloc_at(user_page, writable)
               : 0u;
}

int32_t process_mm_page_protect(uint32_t user_page, int writable) {
    return g_process_mm_ops != 0 && g_process_mm_ops->page_protect != 0
               ? g_process_mm_ops->page_protect(user_page, writable)
               : -1;
}

int32_t process_mm_page_free(uint32_t user_page) {
    return g_process_mm_ops != 0 && g_process_mm_ops->page_free != 0
               ? g_process_mm_ops->page_free(user_page)
               : -1;
}

int32_t process_mm_page_free_pid(uint32_t pid, uint32_t user_page) {
    return g_process_mm_ops != 0 && g_process_mm_ops->page_free_pid != 0
               ? g_process_mm_ops->page_free_pid(pid, user_page)
               : -1;
}

uint32_t process_mm_shared_page_alloc(void) {
    return g_process_mm_ops != 0 && g_process_mm_ops->shared_page_alloc != 0
               ? g_process_mm_ops->shared_page_alloc()
               : 0u;
}

int32_t process_mm_shared_page_free(uint32_t frame) {
    return g_process_mm_ops != 0 && g_process_mm_ops->shared_page_free != 0
               ? g_process_mm_ops->shared_page_free(frame)
               : -1;
}

uint32_t process_mm_shared_page_map(uint32_t frame) {
    return g_process_mm_ops != 0 && g_process_mm_ops->shared_page_map != 0
               ? g_process_mm_ops->shared_page_map(frame)
               : 0u;
}

int32_t process_mm_shared_page_unmap(uint32_t user_page) {
    return g_process_mm_ops != 0 && g_process_mm_ops->shared_page_unmap != 0
               ? g_process_mm_ops->shared_page_unmap(user_page)
               : -1;
}

int32_t process_mm_shared_page_unmap_pid(uint32_t pid, uint32_t user_page) {
    return g_process_mm_ops != 0 && g_process_mm_ops->shared_page_unmap_pid != 0
               ? g_process_mm_ops->shared_page_unmap_pid(pid, user_page)
               : -1;
}

void process_mm_cleanup_pid(uint32_t pid) {
    if (g_process_mm_ops != 0 && g_process_mm_ops->cleanup_pid != 0) {
        g_process_mm_ops->cleanup_pid(pid);
    }
}

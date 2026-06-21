#include "kernel/internal/sys/syscall_internal.h"

uint64_t syscall_handle_mmap(uint64_t user_request_addr) {
    struct syscall_mmap_request request;

    if (!syscall_copy_from_user(&request, user_request_addr, sizeof(request))) {
        return 0;
    }
    return addrspace_mmap(request.addr,
                          request.length,
                          request.prot,
                          request.flags,
                          request.shm_handle,
                          request.offset);
}

uint64_t syscall_handle_munmap(uint64_t addr, uint64_t length) {
    return (uint64_t)addrspace_munmap(addr, length);
}

uint64_t syscall_handle_shm_open(uint64_t user_name_addr, uint64_t size, uint32_t flags) {
    if (!syscall_copy_user_cstr(g_syscall_name_buffer,
                                user_name_addr,
                                sizeof(g_syscall_name_buffer))) {
        return (uint64_t)-1;
    }
    return (uint64_t)(int64_t)addrspace_shm_open(g_syscall_name_buffer, size, flags);
}

uint64_t syscall_handle_shm_unlink(uint64_t user_name_addr) {
    if (!syscall_copy_user_cstr(g_syscall_name_buffer,
                                user_name_addr,
                                sizeof(g_syscall_name_buffer))) {
        return 0;
    }
    return (uint64_t)addrspace_shm_unlink(g_syscall_name_buffer);
}

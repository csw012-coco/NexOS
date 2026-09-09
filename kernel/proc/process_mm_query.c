#include "kernel/internal/proc/process_lifecycle_internal.h"

void process_mm_query_vm_snapshot(struct syscall_vm_info *info) {
    addrspace_vm_snapshot(info);
}

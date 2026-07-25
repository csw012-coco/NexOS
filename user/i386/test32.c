#include <nlibc.h>
#include <nexos/audio.h>
#include <nexos/fs.h>
#include <nexos/gfx.h>
#include <nexos/net.h>

#include "test32_fs.h"
#include "test32_ipc.h"
#include "test32_libc.h"
#include "test32_mm.h"
#include "test32_proc.h"
#include "test32_pseudo.h"
#include "test32_query.h"
#include "test32_sys.h"

int main(int argc, char **argv) {
    char source[16];
    int fd;
    pid_t pid;

    if (argc > 1 && strcmp(argv[1], "kill-child") == 0) {
        return test32_kill_child();
    }
    if (argc > 1 && strcmp(argv[1], "shm-child") == 0) {
        return test32_shm_child();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-kill-child") == 0) {
        return test32_mmap_kill_child();
    }
    if (argc > 1 && strcmp(argv[1], "shared-fault-child") == 0) {
        return test32_shared_fault_child();
    }
    if (argc > 1 && strcmp(argv[1], "invalid-pointer-child") == 0) {
        return test32_invalid_pointer_child();
    }
    if (argc > 1 && strcmp(argv[1], "cow-parent-exit-child") == 0) {
        return test32_cow_parent_exit_child();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-fixed") == 0) {
        return test32_mmap_fixed_partial();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-prot-child") == 0) {
        return test32_mmap_prot_child();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-prot") == 0) {
        return test32_mmap_protection();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-cleanup") == 0) {
        return test32_mmap_fault_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-kill-cleanup") == 0) {
        return test32_mmap_kill_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "shared-fault-cleanup") == 0) {
        return test32_shared_fault_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "invalid-pointer-cleanup") == 0) {
        return test32_invalid_pointer_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "mprotect-child") == 0) {
        return test32_mprotect_child();
    }
    if (argc > 1 && strcmp(argv[1], "mprotect") == 0) {
        return test32_mprotect_case();
    }
    if (argc > 1 && strcmp(argv[1], "mprotect-partial-child") == 0) {
        return test32_mprotect_partial_child();
    }
    if (argc > 1 && strcmp(argv[1], "mprotect-partial") == 0) {
        return test32_mprotect_partial_case();
    }
    if (argc > 1 && strcmp(argv[1], "stack-grow") == 0) {
        return test32_stack_grow_case();
    }
    if (argc > 1 && strcmp(argv[1], "fork") == 0) {
        return test32_fork_case();
    }
    if (argc > 1 && strcmp(argv[1], "fork-cow-cleanup") == 0) {
        return test32_fork_cow_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "fork-cow-ownership") == 0) {
        return test32_fork_cow_ownership_case();
    }
    if (argc > 1 && strcmp(argv[1], "fork-shared") == 0) {
        return test32_fork_shared_mmap_case();
    }
    if (argc > 1 && strcmp(argv[1], "fork-map-table") == 0) {
        return test32_fork_mapping_table_case();
    }
    if (argc > 1 && strcmp(argv[1], "exec-target") == 0) {
        return test32_exec_target_case(argc, argv);
    }
    if (argc > 1 && strcmp(argv[1], "fork-wait-exec") == 0) {
        return test32_fork_wait_exec_case();
    }
    if (argc > 1 && strcmp(argv[1], "fork-mmap-exec") == 0) {
        return test32_fork_mmap_exec_case();
    }
    if (argc > 1 && strcmp(argv[1], "exec-fail-cleanup") == 0) {
        return test32_exec_fail_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "shm-lifecycle") == 0) {
        return test32_shm_lifecycle_case();
    }
    if (puts("[test32] ELF32 C program entered Ring 3") == EOF) {
        return 10;
    }

    if ((fd = test32_libc_string_memory_case()) != 0) {
        return fd;
    }
    if ((fd = test32_libc_string_ctype_strings_case()) != 0) {
        return fd;
    }
    if ((fd = test32_libc_header_sync_case()) != 0) {
        return fd;
    }
    if ((fd = test32_libc_malloc_case()) != 0) {
        return fd;
    }
    if ((fd = test32_libc_stdlib_numeric_case()) != 0) {
        return fd;
    }
    if ((fd = test32_libc_math_case()) != 0) {
        return fd;
    }

    pid = getpid();
    if (pid <= 0) {
        return 22;
    }
    if ((fd = test32_proc_query_case(pid)) != 0) {
        return fd;
    }
    if ((fd = test32_query_syscalls_case()) != 0) {
        return fd;
    }

    if ((fd = test32_query_backend_case()) != 0) {
        return fd;
    }

    if ((fd = test32_proc_getpid_write_puts_case(pid, source)) != 0) {
        return fd;
    }

    if ((fd = test32_fs_open_read_close_case()) != 0) {
        return fd;
    }
    if ((fd = test32_fs_create_truncate_case()) != 0) {
        return fd;
    }

    if ((fd = test32_pseudo_fs_case()) != 0) {
        return fd;
    }

    if ((fd = test32_syscall_helper_case()) != 0) {
        return fd;
    }

    if ((fd = test32_proc_kill_wait_case()) != 0) {
        return fd;
    }
    if ((fd = test32_proc_spawn_background_case()) != 0) {
        return fd;
    }
    if ((fd = test32_proc_fork_surface_case()) != 0) {
        return fd;
    }

    if ((fd = test32_libc_stdio_file_format_case(pid)) != 0) {
        return fd;
    }

    if ((fd = test32_fs_mkdir_remove_rmdir_case(pid)) != 0) {
        return fd;
    }
    if ((fd = test32_fs_mount_umount_case(pid)) != 0) {
        return fd;
    }
    if ((fd = test32_fs_nxfs_mount_case(pid)) != 0) {
        return fd;
    }

    if ((fd = test32_mmap_shm_basic_case()) != 0) {
        return fd;
    }
    if (test32_mmap_fault_cleanup_case() != 0) {
        return 172;
    }
    if (test32_mmap_kill_cleanup_case() != 0) {
        return 176;
    }
    if ((fd = test32_live_shared_mmap_case()) != 0) {
        return fd;
    }

    if ((fd = test32_ipc_mq_sem_case()) != 0) {
        return fd;
    }

    if ((fd = test32_ticks_yield_sleep_case()) != 0) {
        return fd;
    }

    if (putchar('[') != '[' ||
        puts("test32] libnlibc32.a PASS") == EOF) {
        return 16;
    }
    return 0;
}

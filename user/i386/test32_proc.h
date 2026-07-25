#pragma once

#include <nlibc.h>

int test32_kill_child(void);
int test32_cow_parent_exit_child(void);
int test32_exec_target_case(int argc, char **argv);

int test32_proc_query_case(pid_t pid);
int test32_proc_getpid_write_puts_case(pid_t pid, const void *ptr);
int test32_proc_kill_wait_case(void);
int test32_proc_spawn_background_case(void);
int test32_proc_fork_surface_case(void);

int test32_fork_case(void);
int test32_fork_cow_cleanup_case(void);
int test32_fork_cow_ownership_case(void);
int test32_fork_shared_mmap_case(void);
int test32_fork_mapping_table_case(void);
int test32_fork_wait_exec_case(void);
int test32_fork_mmap_exec_case(void);
int test32_exec_fail_cleanup_case(void);

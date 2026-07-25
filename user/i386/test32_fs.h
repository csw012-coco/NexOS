#pragma once

#include <nlibc.h>
#include <nexos/fs.h>

int test32_fs_open_read_close_case(void);
int test32_fs_create_truncate_case(void);
int test32_fs_mkdir_remove_rmdir_case(pid_t pid);
int test32_fs_mount_umount_case(pid_t pid);
int test32_fs_nxfs_mount_case(pid_t pid);

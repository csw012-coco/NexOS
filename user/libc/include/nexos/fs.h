#pragma once

#include "user/public/sysapi.h"

#define NEX_MOUNT_AUTO SYS_MOUNT_AUTO
#define NEX_MOUNT_FAT32 SYS_MOUNT_FAT32
#define NEX_MOUNT_NXFS SYS_MOUNT_NXFS

#define NEX_MOUNT_ERR_BAD_ARGS SYS_MOUNT_ERR_BAD_ARGS
#define NEX_MOUNT_ERR_INVALID_SOURCE SYS_MOUNT_ERR_INVALID_SOURCE
#define NEX_MOUNT_ERR_INVALID_TARGET SYS_MOUNT_ERR_INVALID_TARGET
#define NEX_MOUNT_ERR_RESERVED_TARGET SYS_MOUNT_ERR_RESERVED_TARGET
#define NEX_MOUNT_ERR_TARGET_EXISTS SYS_MOUNT_ERR_TARGET_EXISTS
#define NEX_MOUNT_ERR_NO_SLOTS SYS_MOUNT_ERR_NO_SLOTS
#define NEX_MOUNT_ERR_DISK_NOT_FOUND SYS_MOUNT_ERR_DISK_NOT_FOUND
#define NEX_MOUNT_ERR_PARTITION_NOT_FOUND SYS_MOUNT_ERR_PARTITION_NOT_FOUND
#define NEX_MOUNT_ERR_FS_DETECT SYS_MOUNT_ERR_FS_DETECT
#define NEX_MOUNT_ERR_UNSUPPORTED_KIND SYS_MOUNT_ERR_UNSUPPORTED_KIND
#define NEX_MOUNT_ERR_FS_MOUNT SYS_MOUNT_ERR_FS_MOUNT
#define NEX_MOUNT_ERR_PARTITION_REQUIRED SYS_MOUNT_ERR_PARTITION_REQUIRED
#define NEX_MOUNT_ERR_TARGET_BUSY SYS_MOUNT_ERR_TARGET_BUSY
#define NEX_MOUNT_ERR_TARGET_NOT_FOUND SYS_MOUNT_ERR_TARGET_NOT_FOUND

#define NEX_MOUNT_INFO_NONE SYS_MOUNT_INFO_NONE
#define NEX_MOUNT_INFO_FAT32 SYS_MOUNT_INFO_FAT32
#define NEX_MOUNT_INFO_NXFS SYS_MOUNT_INFO_NXFS
#define NEX_MOUNT_INFO_DEVFS SYS_MOUNT_INFO_DEVFS
#define NEX_MOUNT_INFO_PROCFS SYS_MOUNT_INFO_PROCFS
#define NEX_MOUNT_INFO_EVENTFS SYS_MOUNT_INFO_EVENTFS

int chdir(const char *path);
int getcwd(char *buffer, uint32_t size);
int mount(const char *source, const char *target, uint32_t kind);
int umount(const char *target);
int switch_root(const char *target);
int block_query(uint32_t index, struct syscall_block_info *info);
int part_query(uint32_t disk_index, uint32_t slot, struct syscall_partition_info *info);
int mount_query(uint32_t index, struct syscall_mount_info *info);
int boot_info_query(struct syscall_boot_info *info);
int memmap_query(uint32_t index, struct syscall_memmap_info *info);
int pmm_query(struct syscall_pmm_info *info);
int block_read(uint32_t disk_index, uint64_t lba, struct syscall_block_read_info *info);
int block_write(uint32_t disk_index, uint64_t lba, struct syscall_block_write_info *info);
int program_query(uint32_t index, struct syscall_program_info *info);
int root_query(uint32_t index, struct syscall_root_entry_info *info);
int root_find(const char *name, struct syscall_root_entry_info *info);
int fat_root_query(uint32_t index, struct syscall_fat_entry_info *info);
int fat_root_find(const char *name, struct syscall_fat_entry_info *info);

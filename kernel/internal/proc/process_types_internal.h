#pragma once

#include "kernel/public/proc/process.h"
#include "kernel/public/proc/context.h"
#include "kernel/internal/fs/file_internal.h"

enum {
    PROCESS_FILE_MAX = NOS_PROCESS_FILE_MAX
};

struct process_parent_record {
    uint32_t pid;
};

struct process_wait_record {
    uint32_t pid;
    uint64_t info_user;
};

struct process {
    uint32_t pid;
    uint32_t slot;
    enum process_state state;
    int32_t exit_code;
    uint8_t has_saved_frame;
    uint32_t wake_tick;
    struct process_parent_record parent;
    struct process_wait_record wait;
    const char *name;
    char name_storage[NOS_NAME_BUFFER_SIZE];
    char cwd_storage[NOS_PATH_BUFFER_SIZE];
    enum process_image_kind image_kind;
    uint64_t entry;
    uint64_t stack_top;
    void *console_handle;
    struct file files[NOS_PROCESS_FILE_MAX];
    struct address_space *address_space;
    struct process_context context;
    struct syscall_frame saved_frame;
};

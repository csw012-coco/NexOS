#pragma once

#include <stdint.h>
#include "kernel/public/sys/system_limits.h"

#define KERNEL_DRIVER_NAME_MAX 31u

struct vfs;

enum kernel_driver_state {
    KERNEL_DRIVER_STATE_EMPTY = 0,
    KERNEL_DRIVER_STATE_REGISTERED = 1,
    KERNEL_DRIVER_STATE_ACTIVE = 2,
    KERNEL_DRIVER_STATE_INACTIVE = 3,
    KERNEL_DRIVER_STATE_FAILED = 4
};

enum kernel_driver_kind {
    KERNEL_DRIVER_KIND_UNKNOWN = 0,
    KERNEL_DRIVER_KIND_STORAGE = 1,
    KERNEL_DRIVER_KIND_USB = 2,
    KERNEL_DRIVER_KIND_AUDIO = 3,
    KERNEL_DRIVER_KIND_NET = 4
};

enum kernel_driver_file_state {
    KERNEL_DRIVER_FILE_DISCOVERED = 0,
    KERNEL_DRIVER_FILE_ELF_INVALID = 1,
    KERNEL_DRIVER_FILE_ELF_RELOC = 2,
    KERNEL_DRIVER_FILE_LOADED = 3,
    KERNEL_DRIVER_FILE_LOAD_FAILED = 4
};

typedef int (*kernel_driver_init_fn)(void);
typedef void (*kernel_driver_exit_fn)(void);

struct kernel_driver {
    const char *name;
    enum kernel_driver_kind kind;
    kernel_driver_init_fn init;
    kernel_driver_exit_fn exit;
};

struct kernel_driver_record {
    const struct kernel_driver *driver;
    enum kernel_driver_state state;
    int init_result;
    const char *source;
    const char *path;
};

struct kernel_driver_file {
    char name[NOS_NAME_BUFFER_SIZE];
    char path[NOS_PATH_BUFFER_SIZE];
    uint32_t size;
    enum kernel_driver_file_state state;
};

void driver_manager_init(void);
int driver_register(const struct kernel_driver *driver);
uint32_t driver_init_all(void);
const struct kernel_driver_record *driver_find(const char *name);
const struct kernel_driver_record *driver_get(uint32_t index);
uint32_t driver_count(void);
uint32_t driver_discover_root(struct vfs *vfs, const char *directory);
uint32_t driver_load_all(struct vfs *vfs);
const struct kernel_driver_file *driver_get_file(uint32_t index);
uint32_t driver_file_count(void);

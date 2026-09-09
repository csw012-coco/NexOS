#include "kernel/internal/proc/process_program_registry_internal.h"

const struct process_program *process_find_program_internal(const char *name) {
    (void)name;
    return 0;
}

const char *process_resolve_image_name(const char *name) {
    return name;
}

uint32_t process_program_count(void) {
    return 0u;
}

const char *process_program_name(uint32_t index) {
    (void)index;
    return 0;
}

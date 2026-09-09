#include "user/apps/elf/ush/ush_exec_internal.h"

int ush_open_resolved_path(const char *cwd, const char *arg, uint32_t flags) {
    (void)cwd;
    if (arg == NULL || arg[0] == '\0') {
        return -1;
    }
    return open(arg, flags);
}

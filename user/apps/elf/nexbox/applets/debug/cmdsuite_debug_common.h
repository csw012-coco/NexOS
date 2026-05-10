#ifndef NEXBOX_CMDSUITE_DEBUG_COMMON_H
#define NEXBOX_CMDSUITE_DEBUG_COMMON_H

#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static inline const char *cmdsuite_debug_skip_spaces(const char *text) {
    while (text != NULL && *text == ' ') {
        text++;
    }
    return text != NULL ? text : "";
}

static inline int cmdsuite_debug_query_machine_info(struct syscall_machine_info *info) {
    if (info == NULL) {
        return 0;
    }
    if (machine_info_query(info) <= 0) {
        return 0;
    }
    return 1;
}

#endif

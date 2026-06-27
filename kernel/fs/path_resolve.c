#include "kernel/internal/fs/path_resolve_internal.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/sys/system_limits.h"
#include "lib/string.h"

static uint32_t path_length(const char *text) {
    uint32_t length = 0u;

    while (text != 0 && text[length] != '\0') {
        length++;
    }
    return length;
}

static void path_copy(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void path_pop_segment(char *path) {
    uint32_t length;

    if (path == 0) {
        return;
    }
    length = path_length(path);
    while (length > 1u && path[length - 1u] != '/') {
        length--;
    }
    if (length <= 1u) {
        path[0] = '/';
        path[1] = '\0';
    } else {
        path[length - 1u] = '\0';
    }
}

static int path_append_segment(char *path,
                               uint32_t path_size,
                               const char *segment,
                               uint32_t segment_length) {
    uint32_t length;

    if (path == 0 || segment == 0 || segment_length == 0u) {
        return 0;
    }
    length = path_length(path);
    if (length == 0u || length >= path_size) {
        return 0;
    }
    if (!(length == 1u && path[0] == '/')) {
        if (length + 1u >= path_size) {
            return 0;
        }
        path[length++] = '/';
    }
    if (length + segment_length >= path_size) {
        return 0;
    }
    for (uint32_t i = 0u; i < segment_length; i++) {
        path[length + i] = segment[i];
    }
    path[length + segment_length] = '\0';
    return 1;
}

int fs_resolve_process_path(const struct process *proc,
                            const char *input,
                            char *out,
                            uint32_t out_size) {
    uint32_t position = 0u;

    if (input == 0 || out == 0 || out_size < 2u) {
        return 0;
    }
    if (input[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        position = 1u;
    } else {
        path_copy(out, out_size, process_cwd(proc));
    }
    if (input[0] == '\0' || streq(input, ".")) {
        return 1;
    }
    while (input[position] != '\0') {
        char segment[NOS_NAME_BUFFER_SIZE];
        uint32_t segment_length = 0u;

        while (input[position] == '/') {
            position++;
        }
        if (input[position] == '\0') {
            break;
        }
        while (input[position] != '\0' && input[position] != '/') {
            if (segment_length + 1u >= sizeof(segment)) {
                return 0;
            }
            segment[segment_length++] = input[position++];
        }
        segment[segment_length] = '\0';
        if (segment_length == 1u && segment[0] == '.') {
            continue;
        }
        if (segment_length == 2u &&
            segment[0] == '.' &&
            segment[1] == '.') {
            path_pop_segment(out);
            continue;
        }
        if (!path_append_segment(out,
                                 out_size,
                                 segment,
                                 segment_length)) {
            return 0;
        }
    }
    return 1;
}

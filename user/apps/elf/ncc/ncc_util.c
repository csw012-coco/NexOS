#include "user/apps/elf/ncc/ncc.h"

int ncc_read_file(const char *path, uint8_t **data_out, uint32_t *size_out) {
    struct ncc_buffer buffer = {0};
    uint8_t chunk[256];
    int fd;

    if (path == NULL || data_out == NULL || size_out == NULL) {
        return 0;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    for (;;) {
        ssize_t got = read(fd, chunk, sizeof(chunk));
        uint32_t needed;
        uint8_t *next;

        if (got < 0) {
            close(fd);
            free(buffer.data);
            return 0;
        }
        if (got == 0) {
            break;
        }
        if (buffer.size + (uint32_t)got > NCC_SOURCE_MAX * 16u) {
            close(fd);
            free(buffer.data);
            return 0;
        }
        needed = buffer.size + (uint32_t)got + 1u;
        if (needed > buffer.capacity) {
            uint32_t capacity = buffer.capacity != 0u ? buffer.capacity : 2048u;

            while (capacity < needed) {
                capacity *= 2u;
            }
            next = realloc(buffer.data, capacity);
            if (next == NULL) {
                close(fd);
                free(buffer.data);
                return 0;
            }
            buffer.data = next;
            buffer.capacity = capacity;
        }
        memcpy(buffer.data + buffer.size, chunk, (uint32_t)got);
        buffer.size += (uint32_t)got;
    }
    close(fd);
    if (buffer.data == NULL) {
        buffer.data = malloc(1u);
        if (buffer.data == NULL) {
            return 0;
        }
    }
    buffer.data[buffer.size] = 0;
    *data_out = buffer.data;
    *size_out = buffer.size;
    return 1;
}

int ncc_write_all(int fd, const void *data, uint32_t size) {
    uint32_t written = 0u;

    while (written < size) {
        ssize_t rc = write(fd, (const uint8_t *)data + written, size - written);

        if (rc <= 0) {
            return 0;
        }
        written += (uint32_t)rc;
    }
    return 1;
}

void ncc_copy_text(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0u;

    if (dst == NULL || dst_size == 0u) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

uint64_t ncc_align_up(uint64_t value, uint64_t align) {
    if (align <= 1u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

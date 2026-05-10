#include "user/libc/include/nlibc.h"

#define MALLOC_ALIGN 16u
#define MALLOC_MIN_SPLIT 16u
#define MALLOC_MAGIC_FREE 0x4e58465245454850ull
#define MALLOC_MAGIC_ALLOC 0x4e58414c4c4f4348ull
#define MALLOC_MAX_CHUNK_PAGES 256u

struct malloc_block {
    size_t size;
    struct malloc_block *next;
    uint64_t magic;
    uint64_t reserved;
};

static struct malloc_block *g_malloc_free_list;

static size_t malloc_align_up(size_t value) {
    return (value + (MALLOC_ALIGN - 1u)) & ~(size_t)(MALLOC_ALIGN - 1u);
}

static uint8_t *malloc_block_payload(struct malloc_block *block) {
    return (uint8_t *)block + sizeof(struct malloc_block);
}

static uint8_t *malloc_block_end(struct malloc_block *block) {
    return malloc_block_payload(block) + block->size;
}

static void malloc_insert_free_block(struct malloc_block *block) {
    struct malloc_block *prev = 0;
    struct malloc_block *cur = g_malloc_free_list;

    if (block == 0) {
        return;
    }
    block->magic = MALLOC_MAGIC_FREE;
    block->reserved = 0;
    while (cur != 0 && (uintptr_t)cur < (uintptr_t)block) {
        prev = cur;
        cur = cur->next;
    }
    block->next = cur;
    if (prev != 0) {
        prev->next = block;
    } else {
        g_malloc_free_list = block;
    }

    if (prev != 0 && malloc_block_end(prev) == (uint8_t *)block) {
        prev->size += sizeof(struct malloc_block) + block->size;
        prev->next = block->next;
        block = prev;
    }
    if (block->next != 0 && malloc_block_end(block) == (uint8_t *)block->next) {
        struct malloc_block *next = block->next;

        block->size += sizeof(struct malloc_block) + next->size;
        block->next = next->next;
    }
}

static int malloc_request_pages(size_t payload_size) {
    size_t total_size;
    size_t page_count;
    uint64_t base;

    if (payload_size > (size_t)-1 - sizeof(struct malloc_block)) {
        return 0;
    }
    total_size = payload_size + sizeof(struct malloc_block);
    if (total_size > (size_t)-1 - (NOS_PAGE_SIZE - 1u)) {
        return 0;
    }
    page_count = (total_size + NOS_PAGE_SIZE - 1u) / NOS_PAGE_SIZE;
    if (page_count == 0 || page_count > MALLOC_MAX_CHUNK_PAGES) {
        return 0;
    }

    base = page_alloc();
    if (base == 0) {
        return 0;
    }
    for (size_t i = 1; i < page_count; i++) {
        uint64_t page = page_alloc();

        if (page != base + (uint64_t)i * NOS_PAGE_SIZE) {
            if (page != 0) {
                (void)page_free(page);
            }
            for (size_t j = 0; j < i; j++) {
                (void)page_free(base + (uint64_t)j * NOS_PAGE_SIZE);
            }
            return 0;
        }
    }

    {
        struct malloc_block *block = (struct malloc_block *)(uintptr_t)base;

        block->size = page_count * NOS_PAGE_SIZE - sizeof(struct malloc_block);
        block->next = 0;
        block->magic = MALLOC_MAGIC_FREE;
        block->reserved = 0;
        malloc_insert_free_block(block);
    }
    return 1;
}

static struct malloc_block *malloc_find_free_block(size_t size,
                                                   struct malloc_block **prev_out) {
    struct malloc_block *prev = 0;
    struct malloc_block *cur = g_malloc_free_list;

    while (cur != 0) {
        if (cur->size >= size) {
            if (prev_out != 0) {
                *prev_out = prev;
            }
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }
    if (prev_out != 0) {
        *prev_out = 0;
    }
    return 0;
}

static void malloc_split_allocated_block(struct malloc_block *block, size_t size) {
    struct malloc_block *extra;

    if (block == 0 || block->size < size + sizeof(struct malloc_block) + MALLOC_MIN_SPLIT) {
        return;
    }
    extra = (struct malloc_block *)(malloc_block_payload(block) + size);
    extra->size = block->size - size - sizeof(struct malloc_block);
    extra->next = 0;
    extra->magic = MALLOC_MAGIC_FREE;
    extra->reserved = 0;
    block->size = size;
    malloc_insert_free_block(extra);
}

void *malloc(size_t size) {
    struct malloc_block *prev = 0;
    struct malloc_block *block;

    if (size == 0) {
        return 0;
    }
    if (size > (size_t)-1 - (MALLOC_ALIGN - 1u)) {
        return 0;
    }
    size = malloc_align_up(size);
    block = malloc_find_free_block(size, &prev);
    if (block == 0) {
        if (!malloc_request_pages(size)) {
            return 0;
        }
        block = malloc_find_free_block(size, &prev);
        if (block == 0) {
            return 0;
        }
    }

    if (block->size >= size + sizeof(struct malloc_block) + MALLOC_MIN_SPLIT) {
        struct malloc_block *next = (struct malloc_block *)(malloc_block_payload(block) + size);

        next->size = block->size - size - sizeof(struct malloc_block);
        next->next = block->next;
        next->magic = MALLOC_MAGIC_FREE;
        next->reserved = 0;
        if (prev != 0) {
            prev->next = next;
        } else {
            g_malloc_free_list = next;
        }
        block->size = size;
    } else {
        if (prev != 0) {
            prev->next = block->next;
        } else {
            g_malloc_free_list = block->next;
        }
    }

    block->next = 0;
    block->magic = MALLOC_MAGIC_ALLOC;
    block->reserved = 0;
    return malloc_block_payload(block);
}

void free(void *ptr) {
    struct malloc_block *block;

    if (ptr == 0) {
        return;
    }
    block = (struct malloc_block *)((uint8_t *)ptr - sizeof(struct malloc_block));
    if (block->magic != MALLOC_MAGIC_ALLOC) {
        return;
    }
    malloc_insert_free_block(block);
}

void *calloc(size_t nmemb, size_t size) {
    size_t total;
    uint8_t *ptr;

    if (nmemb != 0 && size > (size_t)-1 / nmemb) {
        return 0;
    }
    total = nmemb * size;
    ptr = (uint8_t *)malloc(total);
    if (ptr == 0) {
        return 0;
    }
    for (size_t i = 0; i < total; i++) {
        ptr[i] = 0;
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    struct malloc_block *block;
    void *new_ptr;
    size_t copy_size;

    if (ptr == 0) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return 0;
    }
    if (size > (size_t)-1 - (MALLOC_ALIGN - 1u)) {
        return 0;
    }
    block = (struct malloc_block *)((uint8_t *)ptr - sizeof(struct malloc_block));
    if (block->magic != MALLOC_MAGIC_ALLOC) {
        return 0;
    }

    size = malloc_align_up(size);
    if (size <= block->size) {
        malloc_split_allocated_block(block, size);
        return ptr;
    }

    new_ptr = malloc(size);
    if (new_ptr == 0) {
        return 0;
    }
    copy_size = block->size < size ? block->size : size;
    for (size_t i = 0; i < copy_size; i++) {
        ((uint8_t *)new_ptr)[i] = ((uint8_t *)ptr)[i];
    }
    free(ptr);
    return new_ptr;
}

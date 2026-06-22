#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    MALLOC_ALIGN = 8u,
    MALLOC_PAGE_SIZE = 4096u,
    MALLOC_MAGIC_FREE = 0x46524545u,
    MALLOC_MAGIC_USED = 0x55534544u
};

struct malloc_block {
    size_t size;
    struct malloc_block *next;
    uint32_t magic;
    uint32_t page_count;
};

static struct malloc_block *malloc_free_list;

static size_t align_up(size_t value) {
    return (value + MALLOC_ALIGN - 1u) & ~(size_t)(MALLOC_ALIGN - 1u);
}

static uint8_t *payload(struct malloc_block *block) {
    return (uint8_t *)block + sizeof(*block);
}

static uint8_t *block_end(struct malloc_block *block) {
    return payload(block) + block->size;
}

static void insert_free(struct malloc_block *block) {
    struct malloc_block *previous = 0;
    struct malloc_block *current = malloc_free_list;

    block->magic = MALLOC_MAGIC_FREE;
    while (current != 0 && current < block) {
        previous = current;
        current = current->next;
    }
    block->next = current;
    if (previous != 0) {
        previous->next = block;
    } else {
        malloc_free_list = block;
    }
    if (previous != 0 &&
        previous->page_count == 0u &&
        block->page_count == 0u &&
        block_end(previous) == (uint8_t *)block) {
        previous->size += sizeof(*block) + block->size;
        previous->next = block->next;
        block = previous;
    }
    if (block->next != 0 &&
        block->page_count == 0u &&
        block->next->page_count == 0u &&
        block_end(block) == (uint8_t *)block->next) {
        block->size += sizeof(*block) + block->next->size;
        block->next = block->next->next;
    }
}

static int request_pages(size_t size) {
    size_t total = size + sizeof(struct malloc_block);
    uint32_t pages = (uint32_t)((total + MALLOC_PAGE_SIZE - 1u) /
                                MALLOC_PAGE_SIZE);
    uint8_t *base = 0;

    for (uint32_t i = 0; i < pages; i++) {
        uint8_t *page = page_alloc();

        if (page == 0 || (i != 0u && page != base + i * MALLOC_PAGE_SIZE)) {
            if (page != 0) {
                (void)page_free(page);
            }
            for (uint32_t j = 0; j < i; j++) {
                (void)page_free(base + j * MALLOC_PAGE_SIZE);
            }
            return 0;
        }
        if (i == 0u) {
            base = page;
        }
    }
    {
        struct malloc_block *block = (struct malloc_block *)base;

        block->size = pages * MALLOC_PAGE_SIZE - sizeof(*block);
        block->next = 0;
        block->magic = MALLOC_MAGIC_FREE;
        block->page_count = pages;
        insert_free(block);
    }
    return 1;
}

void *malloc(size_t size) {
    struct malloc_block *previous = 0;
    struct malloc_block *block;

    if (size == 0u || size > (size_t)-1 - (MALLOC_ALIGN - 1u)) {
        return 0;
    }
    size = align_up(size);
retry:
    for (block = malloc_free_list; block != 0; block = block->next) {
        if (block->size >= size) {
            break;
        }
        previous = block;
    }
    if (block == 0) {
        previous = 0;
        if (!request_pages(size)) {
            return 0;
        }
        goto retry;
    }
    if (block->size >= size + sizeof(*block) + MALLOC_ALIGN) {
        struct malloc_block *remainder =
            (struct malloc_block *)(payload(block) + size);

        remainder->size = block->size - size - sizeof(*block);
        remainder->next = block->next;
        remainder->magic = MALLOC_MAGIC_FREE;
        remainder->page_count = 0u;
        if (previous != 0) {
            previous->next = remainder;
        } else {
            malloc_free_list = remainder;
        }
        block->size = size;
    } else if (previous != 0) {
        previous->next = block->next;
    } else {
        malloc_free_list = block->next;
    }
    block->next = 0;
    block->magic = MALLOC_MAGIC_USED;
    return payload(block);
}

void free(void *ptr) {
    struct malloc_block *block;

    if (ptr == 0) {
        return;
    }
    block = (struct malloc_block *)((uint8_t *)ptr - sizeof(*block));
    if (block->magic != MALLOC_MAGIC_USED) {
        return;
    }
    insert_free(block);
}

void *calloc(size_t count, size_t size) {
    size_t total;
    void *ptr;

    if (count != 0u && size > (size_t)-1 / count) {
        return 0;
    }
    total = count * size;
    ptr = malloc(total);
    if (ptr != 0) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    struct malloc_block *block;
    void *replacement;

    if (ptr == 0) {
        return malloc(size);
    }
    if (size == 0u) {
        free(ptr);
        return 0;
    }
    block = (struct malloc_block *)((uint8_t *)ptr - sizeof(*block));
    if (block->magic != MALLOC_MAGIC_USED) {
        return 0;
    }
    if (block->size >= size) {
        return ptr;
    }
    replacement = malloc(size);
    if (replacement == 0) {
        return 0;
    }
    memcpy(replacement, ptr, block->size);
    free(ptr);
    return replacement;
}

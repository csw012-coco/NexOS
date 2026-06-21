#include "paging.h"
#include "pmm.h"

enum {
    PAGING_ENTRY_PRESENT = 1u << 0,
    PAGING_ENTRY_WRITABLE = 1u << 1,
    PAGING_ENTRY_USER = 1u << 2,
    PAGING_ENTRY_ADDRESS_MASK = 0xfffff000u,
    PAGING_DIRECTORY_ENTRIES = 1024u,
    PAGING_TABLE_ENTRIES = 1024u,
    PAGING_IDENTITY_TABLES = I386_PAGING_IDENTITY_LIMIT / (4u * 1024u * 1024u),
    PAGING_DYNAMIC_TABLES = 16u,
    PAGING_TEMPORARY_SLOTS = 4u,
    PAGING_CR0_PG = 1u << 31
};

static uint32_t page_directory[PAGING_DIRECTORY_ENTRIES]
    __attribute__((aligned(I386_PAGE_SIZE)));
static uint32_t page_tables[PAGING_IDENTITY_TABLES][PAGING_TABLE_ENTRIES]
    __attribute__((aligned(I386_PAGE_SIZE)));
static uint32_t dynamic_page_tables[PAGING_DYNAMIC_TABLES][PAGING_TABLE_ENTRIES]
    __attribute__((aligned(I386_PAGE_SIZE)));
static unsigned char dynamic_page_table_used[PAGING_DYNAMIC_TABLES];
static uint32_t kernel_page_directory_root;

static void invalidate_page(uint32_t virtual_address) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

static uint32_t *allocate_dynamic_table(void) {
    for (uint32_t table = 0; table < PAGING_DYNAMIC_TABLES; table++) {
        if (dynamic_page_table_used[table] == 0u) {
            dynamic_page_table_used[table] = 1u;
            for (uint32_t entry = 0; entry < PAGING_TABLE_ENTRIES; entry++) {
                dynamic_page_tables[table][entry] = 0;
            }
            return dynamic_page_tables[table];
        }
    }
    return 0;
}

static uint32_t read_cr0(void) {
    uint32_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static uint32_t read_cr3(void) {
    uint32_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

int i386_paging_init(void) {
    if ((read_cr0() & PAGING_CR0_PG) != 0u) {
        return 0;
    }

    for (uint32_t i = 0; i < PAGING_DIRECTORY_ENTRIES; i++) {
        page_directory[i] = 0;
    }
    for (uint32_t i = 0; i < PAGING_DYNAMIC_TABLES; i++) {
        dynamic_page_table_used[i] = 0;
    }

    for (uint32_t table = 0; table < PAGING_IDENTITY_TABLES; table++) {
        for (uint32_t page = 0; page < PAGING_TABLE_ENTRIES; page++) {
            uint32_t physical =
                (table * PAGING_TABLE_ENTRIES + page) * I386_PAGE_SIZE;
            page_tables[table][page] =
                physical | PAGING_ENTRY_PRESENT | PAGING_ENTRY_WRITABLE;
        }

        page_directory[table] =
            ((uint32_t)page_tables[table] & PAGING_ENTRY_ADDRESS_MASK) |
            PAGING_ENTRY_PRESENT |
            PAGING_ENTRY_WRITABLE;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)page_directory) : "memory");
    kernel_page_directory_root =
        (uint32_t)page_directory & PAGING_ENTRY_ADDRESS_MASK;
    __asm__ volatile(
        "mov %%cr0, %%eax\n"
        "or %0, %%eax\n"
        "mov %%eax, %%cr0\n"
        :
        : "i"(PAGING_CR0_PG)
        : "eax", "memory");

    return i386_paging_enabled() &&
           i386_paging_root() == ((uint32_t)page_directory & PAGING_ENTRY_ADDRESS_MASK);
}

uint32_t i386_paging_kernel_root(void) {
    return kernel_page_directory_root;
}

int i386_paging_enabled(void) {
    return (read_cr0() & PAGING_CR0_PG) != 0u;
}

uint32_t i386_paging_root(void) {
    return read_cr3() & PAGING_ENTRY_ADDRESS_MASK;
}

int i386_paging_translate(uint32_t virtual_address, uint32_t *physical_address) {
    uint32_t directory_index = virtual_address >> 22;
    uint32_t table_index = (virtual_address >> 12) & 0x3ffu;
    uint32_t offset = virtual_address & 0xfffu;
    uint32_t directory_entry;
    uint32_t *table;
    uint32_t table_entry;

    if (physical_address == 0 || directory_index >= PAGING_DIRECTORY_ENTRIES) {
        return 0;
    }

    directory_entry = page_directory[directory_index];
    if ((directory_entry & PAGING_ENTRY_PRESENT) == 0u) {
        return 0;
    }

    table = (uint32_t *)(directory_entry & PAGING_ENTRY_ADDRESS_MASK);
    table_entry = table[table_index];
    if ((table_entry & PAGING_ENTRY_PRESENT) == 0u) {
        return 0;
    }

    *physical_address = (table_entry & PAGING_ENTRY_ADDRESS_MASK) | offset;
    return 1;
}

int i386_paging_map_page(uint32_t virtual_address,
                         uint32_t physical_address,
                         int writable,
                         int user_accessible) {
    uint32_t directory_index;
    uint32_t table_index;
    uint32_t directory_entry;
    uint32_t *table;
    uint32_t flags = PAGING_ENTRY_PRESENT;

    if ((virtual_address & (I386_PAGE_SIZE - 1u)) != 0u ||
        (physical_address & (I386_PAGE_SIZE - 1u)) != 0u) {
        return 0;
    }

    directory_index = virtual_address >> 22;
    table_index = (virtual_address >> 12) & 0x3ffu;
    directory_entry = page_directory[directory_index];

    if ((directory_entry & PAGING_ENTRY_PRESENT) == 0u) {
        table = allocate_dynamic_table();
        if (table == 0) {
            return 0;
        }
        page_directory[directory_index] =
            ((uint32_t)table & PAGING_ENTRY_ADDRESS_MASK) |
            PAGING_ENTRY_PRESENT |
            PAGING_ENTRY_WRITABLE |
            (user_accessible ? PAGING_ENTRY_USER : 0u);
    } else {
        table = (uint32_t *)(directory_entry & PAGING_ENTRY_ADDRESS_MASK);
        if (user_accessible) {
            page_directory[directory_index] |= PAGING_ENTRY_USER;
        }
    }

    if ((table[table_index] & PAGING_ENTRY_PRESENT) != 0u) {
        return 0;
    }

    if (writable) {
        flags |= PAGING_ENTRY_WRITABLE;
    }
    if (user_accessible) {
        flags |= PAGING_ENTRY_USER;
    }
    table[table_index] = physical_address | flags;
    invalidate_page(virtual_address);
    return 1;
}

int i386_paging_unmap_page(uint32_t virtual_address, uint32_t *physical_address) {
    uint32_t directory_index;
    uint32_t table_index;
    uint32_t directory_entry;
    uint32_t *table;
    uint32_t table_entry;

    if ((virtual_address & (I386_PAGE_SIZE - 1u)) != 0u) {
        return 0;
    }

    directory_index = virtual_address >> 22;
    table_index = (virtual_address >> 12) & 0x3ffu;
    directory_entry = page_directory[directory_index];
    if ((directory_entry & PAGING_ENTRY_PRESENT) == 0u) {
        return 0;
    }

    table = (uint32_t *)(directory_entry & PAGING_ENTRY_ADDRESS_MASK);
    table_entry = table[table_index];
    if ((table_entry & PAGING_ENTRY_PRESENT) == 0u) {
        return 0;
    }

    if (physical_address != 0) {
        *physical_address = table_entry & PAGING_ENTRY_ADDRESS_MASK;
    }
    table[table_index] = 0;
    invalidate_page(virtual_address);
    return 1;
}

int i386_paging_temporary_map(uint32_t physical_address,
                              uint32_t slot,
                              void **virtual_address) {
    uint32_t address;
    uint32_t old_physical;

    if (virtual_address == 0 ||
        slot >= PAGING_TEMPORARY_SLOTS ||
        (physical_address & (I386_PAGE_SIZE - 1u)) != 0u ||
        i386_paging_root() != kernel_page_directory_root) {
        return 0;
    }

    address = I386_PAGING_DYNAMIC_BASE + slot * I386_PAGE_SIZE;
    if (i386_paging_translate(address, &old_physical)) {
        if (!i386_paging_unmap_page(address, 0)) {
            return 0;
        }
    }
    if (!i386_paging_map_page(address, physical_address, 1, 0)) {
        return 0;
    }
    *virtual_address = (void *)address;
    return 1;
}

void i386_paging_temporary_unmap(uint32_t slot) {
    uint32_t address;
    uint32_t physical;

    if (slot >= PAGING_TEMPORARY_SLOTS ||
        i386_paging_root() != kernel_page_directory_root) {
        return;
    }
    address = I386_PAGING_DYNAMIC_BASE + slot * I386_PAGE_SIZE;
    if (i386_paging_translate(address, &physical)) {
        (void)i386_paging_unmap_page(address, 0);
    }
}

uint32_t i386_paging_create_address_space(void) {
    uint32_t root = i386_pmm_alloc_page();
    uint32_t *directory;

    if (root == I386_PMM_INVALID_PAGE ||
        !i386_paging_temporary_map(root, 0u, (void **)&directory)) {
        if (root != I386_PMM_INVALID_PAGE) {
            (void)i386_pmm_free_page(root);
        }
        return 0;
    }

    for (uint32_t i = 0; i < PAGING_DIRECTORY_ENTRIES; i++) {
        directory[i] = 0;
    }
    for (uint32_t i = 0; i < PAGING_IDENTITY_TABLES; i++) {
        directory[i] = page_directory[i];
    }
    i386_paging_temporary_unmap(0u);
    return root;
}

int i386_paging_map_page_in(uint32_t root,
                            uint32_t virtual_address,
                            uint32_t physical_address,
                            int writable,
                            int user_accessible) {
    uint32_t directory_index;
    uint32_t table_index;
    uint32_t *directory;
    uint32_t *table;
    uint32_t table_physical;
    uint32_t flags = PAGING_ENTRY_PRESENT;

    if (root == 0u ||
        (root & (I386_PAGE_SIZE - 1u)) != 0u ||
        (virtual_address & (I386_PAGE_SIZE - 1u)) != 0u ||
        (physical_address & (I386_PAGE_SIZE - 1u)) != 0u ||
        i386_paging_root() != kernel_page_directory_root ||
        !i386_paging_temporary_map(root, 0u, (void **)&directory)) {
        return 0;
    }

    directory_index = virtual_address >> 22;
    table_index = (virtual_address >> 12) & 0x3ffu;
    if ((directory[directory_index] & PAGING_ENTRY_PRESENT) == 0u) {
        table_physical = i386_pmm_alloc_page();
        if (table_physical == I386_PMM_INVALID_PAGE ||
            !i386_paging_temporary_map(table_physical, 1u, (void **)&table)) {
            i386_paging_temporary_unmap(0u);
            return 0;
        }
        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            table[i] = 0;
        }
        directory[directory_index] =
            table_physical |
            PAGING_ENTRY_PRESENT |
            PAGING_ENTRY_WRITABLE |
            (user_accessible ? PAGING_ENTRY_USER : 0u);
    } else {
        table_physical =
            directory[directory_index] & PAGING_ENTRY_ADDRESS_MASK;
        if (user_accessible) {
            directory[directory_index] |= PAGING_ENTRY_USER;
        }
        if (!i386_paging_temporary_map(table_physical, 1u, (void **)&table)) {
            i386_paging_temporary_unmap(0u);
            return 0;
        }
    }

    if ((table[table_index] & PAGING_ENTRY_PRESENT) != 0u) {
        i386_paging_temporary_unmap(1u);
        i386_paging_temporary_unmap(0u);
        return 0;
    }
    if (writable) {
        flags |= PAGING_ENTRY_WRITABLE;
    }
    if (user_accessible) {
        flags |= PAGING_ENTRY_USER;
    }
    table[table_index] = physical_address | flags;
    i386_paging_temporary_unmap(1u);
    i386_paging_temporary_unmap(0u);
    return 1;
}

int i386_paging_translate_in(uint32_t root,
                             uint32_t virtual_address,
                             uint32_t *physical_address) {
    uint32_t *directory;
    uint32_t *table;
    uint32_t directory_entry;
    uint32_t table_entry;

    if (root == 0u || physical_address == 0 ||
        i386_paging_root() != kernel_page_directory_root ||
        !i386_paging_temporary_map(root, 0u, (void **)&directory)) {
        return 0;
    }
    directory_entry = directory[virtual_address >> 22];
    if ((directory_entry & PAGING_ENTRY_PRESENT) == 0u ||
        !i386_paging_temporary_map(directory_entry & PAGING_ENTRY_ADDRESS_MASK,
                                   1u,
                                   (void **)&table)) {
        i386_paging_temporary_unmap(0u);
        return 0;
    }
    table_entry = table[(virtual_address >> 12) & 0x3ffu];
    if ((table_entry & PAGING_ENTRY_PRESENT) == 0u) {
        i386_paging_temporary_unmap(1u);
        i386_paging_temporary_unmap(0u);
        return 0;
    }
    *physical_address =
        (table_entry & PAGING_ENTRY_ADDRESS_MASK) |
        (virtual_address & (I386_PAGE_SIZE - 1u));
    i386_paging_temporary_unmap(1u);
    i386_paging_temporary_unmap(0u);
    return 1;
}

void i386_paging_switch(uint32_t root) {
    if (root != 0u && (root & (I386_PAGE_SIZE - 1u)) == 0u) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(root) : "memory");
    }
}

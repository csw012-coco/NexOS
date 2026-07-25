#include "drivers/storage/ahci.h"

#include "block/blockdev.h"
#include "drivers/bus/pci.h"
#include "hal/hal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/pmm.h"
#include "lib/string.h"

#if defined(__i386__) && !defined(__x86_64__)
#include "arch/x86/i386/paging.h"
#include "arch/x86/i386/pmm.h"
#endif

enum {
    AHCI_MAX_PORTS = 32u,
    AHCI_MAX_DEVICES = 4u,
    AHCI_SECTOR_SIZE = 512u,
    AHCI_BOUNCE_SIZE = 4096u,
    AHCI_MAX_SECTORS_PER_COMMAND = AHCI_BOUNCE_SIZE / AHCI_SECTOR_SIZE,
    AHCI_MMIO_MAP_SIZE = 0x2000u,
#if defined(__i386__) && !defined(__x86_64__)
    AHCI_I386_MMIO_BASE = 0xf9000000u,
    AHCI_I386_DMA_MAP_BASE = 0xf9100000u,
    AHCI_I386_DMA_MAP_PAGES = 32u,
#endif

    AHCI_GHC_AE = 1u << 31,

    AHCI_PORT_CMD_ST = 1u << 0,
    AHCI_PORT_CMD_FRE = 1u << 4,
    AHCI_PORT_CMD_FR = 1u << 14,
    AHCI_PORT_CMD_CR = 1u << 15,
    AHCI_PORT_CMD_ICC_ACTIVE = 1u << 28,

    AHCI_PORT_SSTS_DET_PRESENT = 3u,
    AHCI_PORT_SSTS_IPM_ACTIVE = 1u,

    AHCI_PORT_TFD_BSY = 0x80u,
    AHCI_PORT_TFD_DRQ = 0x08u,
    AHCI_PORT_IS_TFES = 1u << 30,

    AHCI_SIG_ATA = 0x00000101u,

    AHCI_FIS_TYPE_REG_H2D = 0x27u,
    ATA_CMD_IDENTIFY = 0xecu,
    ATA_CMD_READ_DMA_EXT = 0x25u,
    ATA_CMD_WRITE_DMA_EXT = 0x35u,
    ATA_CMD_FLUSH_CACHE_EXT = 0xeau
};

static int ahci_driver_init_local(void) {
    ahci_init();
    return 1;
}

const struct kernel_driver ahci_kernel_driver = {
    .name = "AHCI",
    .kind = KERNEL_DRIVER_KIND_STORAGE,
    .init = ahci_driver_init_local,
    .exit = NULL,
};

struct ahci_hba_port {
    volatile uint32_t clb;
    volatile uint32_t clbu;
    volatile uint32_t fb;
    volatile uint32_t fbu;
    volatile uint32_t is;
    volatile uint32_t ie;
    volatile uint32_t cmd;
    volatile uint32_t rsv0;
    volatile uint32_t tfd;
    volatile uint32_t sig;
    volatile uint32_t ssts;
    volatile uint32_t sctl;
    volatile uint32_t serr;
    volatile uint32_t sact;
    volatile uint32_t ci;
    volatile uint32_t sntf;
    volatile uint32_t fbs;
    volatile uint32_t rsv1[11];
    volatile uint32_t vendor[4];
};

struct ahci_hba_mem {
    volatile uint32_t cap;
    volatile uint32_t ghc;
    volatile uint32_t is;
    volatile uint32_t pi;
    volatile uint32_t vs;
    volatile uint32_t ccc_ctl;
    volatile uint32_t ccc_pts;
    volatile uint32_t em_loc;
    volatile uint32_t em_ctl;
    volatile uint32_t cap2;
    volatile uint32_t bohc;
    volatile uint8_t rsv[0xa0 - 0x2c];
    volatile uint8_t vendor[0x100 - 0xa0];
    struct ahci_hba_port ports[AHCI_MAX_PORTS];
};

struct ahci_cmd_header {
    uint16_t flags;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv[4];
};

struct ahci_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc_i;
};

struct ahci_cmd_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    struct ahci_prdt_entry prdt[8];
};

struct ahci_device {
    char name[12];
    uint8_t port_index;
    uint8_t present;
    volatile struct ahci_hba_port *port;
    struct ahci_cmd_header *cmd_list;
    struct ahci_cmd_table *cmd_table;
    uint64_t cmd_list_phys;
    uint64_t fis_phys;
    uint64_t cmd_table_phys;
    uint64_t bounce_phys;
    uint8_t *bounce;
    uint64_t sector_count;
    struct block_device blockdev;
};

static struct ahci_device g_ahci_devices[AHCI_MAX_DEVICES];
static uint32_t g_ahci_device_count;
static struct ahci_hba_mem *g_ahci_hba;
#if defined(__i386__) && !defined(__x86_64__)
static uint32_t g_ahci_i386_dma_map_next;

static void *ahci_i386_map_dma_page(uint32_t phys) {
    uint32_t virt;

    if (g_ahci_i386_dma_map_next >= AHCI_I386_DMA_MAP_PAGES) {
        return 0;
    }
    virt = AHCI_I386_DMA_MAP_BASE + g_ahci_i386_dma_map_next * I386_PAGE_SIZE;
    g_ahci_i386_dma_map_next++;
    if (!i386_paging_map_page(virt, phys, 1, 0)) {
        return 0;
    }
    return (void *)(uintptr_t)virt;
}
#endif

static uint64_t ahci_alloc_page_phys(void) {
#if defined(__i386__) && !defined(__x86_64__)
    uint32_t phys = i386_pmm_alloc_page();

    return phys != I386_PMM_INVALID_PAGE ? (uint64_t)phys : 0u;
#else
    return pmm_alloc_page();
#endif
}

static void *ahci_phys_map(uint64_t phys) {
#if defined(__i386__) && !defined(__x86_64__)
    if (phys > 0xffffffffull) {
        return 0;
    }
    if (phys < I386_PAGING_IDENTITY_LIMIT) {
        return (void *)(uintptr_t)(uint32_t)phys;
    }
    return ahci_i386_map_dma_page((uint32_t)phys);
#else
    return hal_phys_direct_map(phys);
#endif
}

static void *ahci_existing_phys_map(uint64_t phys) {
#if defined(__i386__) && !defined(__x86_64__)
    uint32_t phys32;

    if (phys > 0xffffffffull) {
        return 0;
    }
    phys32 = (uint32_t)phys;
    if (phys32 < I386_PAGING_IDENTITY_LIMIT) {
        return (void *)(uintptr_t)phys32;
    }
    for (uint32_t i = 0; i < g_ahci_i386_dma_map_next; i++) {
        uint32_t virt = AHCI_I386_DMA_MAP_BASE + i * I386_PAGE_SIZE;
        uint32_t mapped;

        if (i386_paging_translate(virt, &mapped) &&
            (mapped & ~(uint32_t)(I386_PAGE_SIZE - 1u)) == phys32) {
            return (void *)(uintptr_t)virt;
        }
    }
    return 0;
#else
    return hal_phys_direct_map(phys);
#endif
}

static void *ahci_mmio_map(uint64_t phys, uint64_t length) {
#if defined(__i386__) && !defined(__x86_64__)
    uint32_t phys_page;
    uint32_t virt_page = AHCI_I386_MMIO_BASE;
    uint32_t offset;
    uint32_t pages;

    if (phys > 0xffffffffull || length == 0u) {
        return 0;
    }
    phys_page = (uint32_t)phys & ~(uint32_t)(I386_PAGE_SIZE - 1u);
    offset = (uint32_t)phys & (I386_PAGE_SIZE - 1u);
    pages = (uint32_t)((offset + length + I386_PAGE_SIZE - 1u) / I386_PAGE_SIZE);
    if (pages > 16u) {
        return 0;
    }
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t old_phys;
        uint32_t virt = virt_page + i * I386_PAGE_SIZE;

        if (i386_paging_translate(virt, &old_phys)) {
            if (!i386_paging_unmap_page(virt, 0)) {
                return 0;
            }
        }
        if (!i386_paging_map_page(virt, phys_page + i * I386_PAGE_SIZE, 1, 0)) {
            return 0;
        }
    }
    return (void *)(uintptr_t)(virt_page + offset);
#else
    return hal_mmio_map(phys, length);
#endif
}

static void ahci_zero(void *ptr, uint32_t bytes) {
    memset(ptr, 0, bytes);
}

static uint64_t ahci_read_u64le_words(const uint16_t *words, uint32_t lo_word, uint32_t hi_word) {
    uint32_t lo = ((uint32_t)words[lo_word + 1u] << 16) | words[lo_word];
    uint32_t hi = ((uint32_t)words[hi_word + 1u] << 16) | words[hi_word];

    return ((uint64_t)hi << 32) | lo;
}

static void ahci_write_name(char *dst, uint32_t index) {
    if (dst == 0) {
        return;
    }
    dst[0] = 'a';
    dst[1] = 'h';
    dst[2] = 'c';
    dst[3] = 'i';
    dst[4] = (char)('0' + index);
    dst[5] = '\0';
}

static int ahci_port_connected(volatile struct ahci_hba_port *port) {
    uint32_t ssts = port->ssts;
    uint32_t det = ssts & 0x0fu;
    uint32_t ipm = (ssts >> 8) & 0x0fu;

    return det == AHCI_PORT_SSTS_DET_PRESENT &&
           ipm == AHCI_PORT_SSTS_IPM_ACTIVE &&
           port->sig == AHCI_SIG_ATA;
}

static int ahci_wait_not_busy(volatile struct ahci_hba_port *port) {
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if ((port->tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int ahci_wait_clear(volatile uint32_t *reg, uint32_t mask) {
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if ((*reg & mask) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int ahci_stop_port(volatile struct ahci_hba_port *port) {
    port->cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
    return ahci_wait_clear(&port->cmd, AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR);
}

static int ahci_start_port(volatile struct ahci_hba_port *port) {
    if (!ahci_wait_clear(&port->cmd, AHCI_PORT_CMD_CR)) {
        return 0;
    }
    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST | AHCI_PORT_CMD_ICC_ACTIVE;
    return 1;
}

static int ahci_alloc_port_memory(struct ahci_device *dev) {
    uint64_t cmd_list_phys;
    uint64_t fis_phys;
    uint64_t cmd_table_phys;
    uint64_t bounce_phys;

    cmd_list_phys = ahci_alloc_page_phys();
    fis_phys = ahci_alloc_page_phys();
    cmd_table_phys = ahci_alloc_page_phys();
    bounce_phys = ahci_alloc_page_phys();
    if (cmd_list_phys == 0u || fis_phys == 0u || cmd_table_phys == 0u || bounce_phys == 0u ||
        cmd_list_phys > 0xffffffffull || fis_phys > 0xffffffffull ||
        cmd_table_phys > 0xffffffffull || bounce_phys > 0xffffffffull) {
        return 0;
    }
    dev->cmd_list_phys = cmd_list_phys;
    dev->fis_phys = fis_phys;
    dev->cmd_table_phys = cmd_table_phys;
    dev->bounce_phys = bounce_phys;
    dev->cmd_list = (struct ahci_cmd_header *)ahci_phys_map(cmd_list_phys);
    dev->cmd_table = (struct ahci_cmd_table *)ahci_phys_map(cmd_table_phys);
    dev->bounce = (uint8_t *)ahci_phys_map(bounce_phys);
    if (dev->cmd_list == 0 || dev->cmd_table == 0 || dev->bounce == 0) {
        return 0;
    }
    ahci_zero(dev->cmd_list, 4096u);
    ahci_zero(ahci_existing_phys_map(fis_phys), 4096u);
    ahci_zero(dev->cmd_table, 4096u);
    ahci_zero(dev->bounce, 4096u);
    return 1;
}

static int ahci_find_slot(volatile struct ahci_hba_port *port) {
    uint32_t occupied = port->sact | port->ci;

    for (uint32_t i = 0; i < 32u; i++) {
        if ((occupied & (1u << i)) == 0u) {
            return (int)i;
        }
    }
    return -1;
}

static int ahci_issue(volatile struct ahci_hba_port *port, uint32_t slot) {
    uint32_t bit = 1u << slot;

    port->is = 0xffffffffu;
    if (!ahci_wait_not_busy(port)) {
        return 0;
    }
    port->ci = bit;
    for (uint32_t spin = 0; spin < 10000000u; spin++) {
        if ((port->ci & bit) == 0u) {
            return (port->is & AHCI_PORT_IS_TFES) == 0u;
        }
        if ((port->is & AHCI_PORT_IS_TFES) != 0u) {
            return 0;
        }
    }
    return 0;
}

static int ahci_command_dma(struct ahci_device *dev,
                            uint8_t command,
                            uint64_t lba,
                            uint32_t count,
                            uint64_t buffer_phys,
                            uint32_t bytes,
                            int write) {
    int slot;
    struct ahci_cmd_header *header;
    struct ahci_cmd_table *table;
    uint8_t *fis;

    if (dev == 0 || dev->port == 0 || bytes > 4096u || buffer_phys > 0xffffffffull ||
        ((bytes == 0u || count == 0u) && (bytes != 0u || count != 0u))) {
        return 0;
    }
    slot = ahci_find_slot(dev->port);
    if (slot < 0) {
        return 0;
    }

    header = &dev->cmd_list[slot];
    table = dev->cmd_table;
    ahci_zero(header, sizeof(*header));
    ahci_zero(table, sizeof(*table));
    header->flags = (uint16_t)((5u & 0x1fu) | (write ? (1u << 6) : 0u));
    header->prdtl = bytes != 0u ? 1u : 0u;
    header->prdbc = 0u;
    header->ctba = (uint32_t)dev->cmd_table_phys;
    header->ctbau = 0u;
    if (bytes != 0u) {
        table->prdt[0].dba = (uint32_t)buffer_phys;
        table->prdt[0].dbau = 0u;
        table->prdt[0].dbc_i = (bytes - 1u) & 0x003fffffu;
    }

    fis = table->cfis;
    fis[0] = AHCI_FIS_TYPE_REG_H2D;
    fis[1] = 1u << 7;
    fis[2] = command;
    fis[4] = (uint8_t)(lba & 0xffu);
    fis[5] = (uint8_t)((lba >> 8) & 0xffu);
    fis[6] = (uint8_t)((lba >> 16) & 0xffu);
    fis[7] = 1u << 6;
    fis[8] = (uint8_t)((lba >> 24) & 0xffu);
    fis[9] = (uint8_t)((lba >> 32) & 0xffu);
    fis[10] = (uint8_t)((lba >> 40) & 0xffu);
    fis[12] = (uint8_t)(count & 0xffu);
    fis[13] = (uint8_t)((count >> 8) & 0xffu);

    return ahci_issue(dev->port, (uint32_t)slot);
}

static int ahci_flush(struct ahci_device *dev) {
    return ahci_command_dma(dev, ATA_CMD_FLUSH_CACHE_EXT, 0u, 0u, 0u, 0u, 0);
}

static int ahci_identify(struct ahci_device *dev) {
    uint64_t identify_phys = ahci_alloc_page_phys();
    uint16_t *identify;
    uint64_t lba28_count;
    uint64_t lba48_count;

    if (identify_phys == 0u || identify_phys > 0xffffffffull) {
        return 0;
    }
    identify = (uint16_t *)ahci_phys_map(identify_phys);
    if (identify == 0) {
        return 0;
    }
    ahci_zero(identify, 512u);
    if (!ahci_command_dma(dev, ATA_CMD_IDENTIFY, 0u, 1u, identify_phys, 512u, 0)) {
        return 0;
    }
    lba28_count = ((uint32_t)identify[61] << 16) | identify[60];
    lba48_count = ahci_read_u64le_words(identify, 100u, 102u);
    dev->sector_count = lba48_count != 0u ? lba48_count : lba28_count;
    return dev->sector_count != 0u;
}

static int ahci_read_impl(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer) {
    struct ahci_device *dev = (struct ahci_device *)bdev->driver_data;
    uint8_t *out = (uint8_t *)buffer;
    uint32_t done = 0u;

    if (dev == 0 || !dev->present || buffer == 0 || count == 0u ||
        dev->bounce == 0 || lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    while (done < count) {
        uint32_t chunk = count - done;
        uint32_t bytes;

        if (chunk > AHCI_MAX_SECTORS_PER_COMMAND) {
            chunk = AHCI_MAX_SECTORS_PER_COMMAND;
        }
        bytes = chunk * AHCI_SECTOR_SIZE;
        if (!ahci_command_dma(dev,
                              ATA_CMD_READ_DMA_EXT,
                              lba + done,
                              chunk,
                              dev->bounce_phys,
                              bytes,
                              0)) {
            return -1;
        }
        memcpy(out + done * AHCI_SECTOR_SIZE, dev->bounce, bytes);
        done += chunk;
    }
    return 0;
}

static int ahci_write_impl(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer) {
    struct ahci_device *dev = (struct ahci_device *)bdev->driver_data;
    const uint8_t *in = (const uint8_t *)buffer;
    uint32_t done = 0u;

    if (dev == 0 || !dev->present || buffer == 0 || count == 0u ||
        dev->bounce == 0 || lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    while (done < count) {
        uint32_t chunk = count - done;
        uint32_t bytes;

        if (chunk > AHCI_MAX_SECTORS_PER_COMMAND) {
            chunk = AHCI_MAX_SECTORS_PER_COMMAND;
        }
        bytes = chunk * AHCI_SECTOR_SIZE;
        memcpy(dev->bounce, in + done * AHCI_SECTOR_SIZE, bytes);
        if (!ahci_command_dma(dev,
                              ATA_CMD_WRITE_DMA_EXT,
                              lba + done,
                              chunk,
                              dev->bounce_phys,
                              bytes,
                              1)) {
            return -1;
        }
        done += chunk;
    }
    return 0;
}

static int ahci_flush_impl(struct block_device *bdev) {
    struct ahci_device *dev = bdev != 0 ? (struct ahci_device *)bdev->driver_data : 0;

    return dev != 0 && dev->present && ahci_flush(dev) ? 0 : -1;
}

static int ahci_setup_port(struct ahci_device *dev) {
    volatile struct ahci_hba_port *port = dev->port;

    if (!ahci_stop_port(port)) {
        return 0;
    }
    port->clb = (uint32_t)dev->cmd_list_phys;
    port->clbu = 0u;
    port->fb = (uint32_t)dev->fis_phys;
    port->fbu = 0u;
    port->serr = 0xffffffffu;
    port->is = 0xffffffffu;
    port->ie = 0u;
    return ahci_start_port(port);
}

void ahci_init(void) {
    struct pci_ahci_controller ahci;
    uint64_t abar;
    uint32_t pi;

    g_ahci_device_count = 0u;
    g_ahci_hba = 0;
#if defined(__i386__) && !defined(__x86_64__)
    g_ahci_i386_dma_map_next = 0u;
#endif
    for (uint32_t i = 0; i < AHCI_MAX_DEVICES; i++) {
        g_ahci_devices[i].present = 0u;
    }
    if (!pci_find_ahci_controller(&ahci)) {
        return;
    }

    pci_config_write16(ahci.bus, ahci.slot, ahci.function, 0x04,
                       (uint16_t)(pci_config_read16(ahci.bus, ahci.slot, ahci.function, 0x04) | 0x0006u));
    abar = (uint64_t)(ahci.abar & 0xfffffff0u);
    if (abar == 0u) {
        return;
    }
    g_ahci_hba = (struct ahci_hba_mem *)ahci_mmio_map(abar, AHCI_MMIO_MAP_SIZE);
    if (g_ahci_hba == 0) {
        return;
    }
    g_ahci_hba->ghc |= AHCI_GHC_AE;
    pi = g_ahci_hba->pi;
    kprint("ahci: controller bus=%u slot=%u func=%u abar=%lx pi=%x\n",
           ahci.bus, ahci.slot, ahci.function, abar, pi);

    for (uint32_t port_index = 0; port_index < AHCI_MAX_PORTS && g_ahci_device_count < AHCI_MAX_DEVICES; port_index++) {
        struct ahci_device *dev;

        if ((pi & (1u << port_index)) == 0u) {
            continue;
        }
        if (!ahci_port_connected(&g_ahci_hba->ports[port_index])) {
            continue;
        }
        dev = &g_ahci_devices[g_ahci_device_count];
        dev->port_index = (uint8_t)port_index;
        dev->port = &g_ahci_hba->ports[port_index];
        ahci_write_name(dev->name, g_ahci_device_count);
        if (!ahci_alloc_port_memory(dev)) {
            continue;
        }
        if (!ahci_setup_port(dev)) {
            kprint("ahci: port%u start failed cmd=%x\n", port_index, dev->port->cmd);
            continue;
        }
        if (!ahci_identify(dev)) {
            continue;
        }
        dev->blockdev.name = dev->name;
        dev->blockdev.block_size = AHCI_SECTOR_SIZE;
        dev->blockdev.block_count = dev->sector_count;
        dev->blockdev.read = ahci_read_impl;
        dev->blockdev.write = ahci_write_impl;
        dev->blockdev.flush = ahci_flush_impl;
        dev->blockdev.driver_data = dev;
        dev->present = 1u;
        if (blockdev_register(&dev->blockdev) == 0) {
            kprint("ahci: port%u %s sectors=%lx\n", port_index, dev->name, dev->sector_count);
            g_ahci_device_count++;
        }
    }
}

uint32_t ahci_device_count(void) {
    return g_ahci_device_count;
}

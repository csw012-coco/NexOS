#pragma once

#include <stddef.h>
#include <stdint.h>
#include "kernel/public/driver/driver.h"

/*
 * Public ABI for loadable .DRV files.
 *
 * .DRV sources should include this header instead of reaching into unrelated
 * kernel internals. Symbols declared here are resolved by the driver loader.
 */
void driver_log(const char *fmt, ...);

struct driver_pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar[6];
};

enum {
    DRIVER_AUDIO_CAP_PLAYBACK = 1u << 0,
    DRIVER_AUDIO_CAP_TONE = 1u << 1,
    DRIVER_AUDIO_CAP_STREAM = 1u << 2
};

enum {
    DRIVER_AUDIO_KIND_NONE = 0,
    DRIVER_AUDIO_KIND_AC97 = 1,
    DRIVER_AUDIO_KIND_HDA = 2
};

enum {
    DRIVER_AUDIO_PLAY_F_ASYNC = 1u << 0
};

struct driver_audio_pcm_stream {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t data_bytes;
    uint32_t flags;
    void *ctx;
    uint32_t (*read)(void *ctx, void *buffer, uint32_t bytes);
    uint32_t (*cancelled)(void *ctx);
};

struct driver_audio_device_info {
    uint32_t present;
    uint32_t initialized;
    uint32_t caps;
    uint32_t driver_kind;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    char name[32];
};

struct driver_audio_device_ops {
    int (*play_tone)(void *ctx, uint32_t hz, uint32_t duration_ms);
    int (*play_pcm)(void *ctx,
                    const void *data,
                    uint32_t bytes,
                    uint32_t sample_rate,
                    uint32_t channels,
                    uint32_t bits_per_sample,
                    uint32_t flags);
    int (*play_stream)(void *ctx, struct driver_audio_pcm_stream *stream);
};

struct driver_hda_device_info {
    uint32_t present;
    uint32_t initialized;
    uint32_t irq_line;
    uint32_t irq_pin;
    uint32_t bus;
    uint32_t slot;
    uint32_t function;
    uint32_t prog_if;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t mmio_base_lo;
    uint32_t mmio_base_hi;
    uint32_t pci_command;
    uint32_t gcap;
    uint32_t vmaj;
    uint32_t vmin;
    uint32_t outpay;
    uint32_t inpay;
    uint32_t gctl;
    uint32_t statests;
    uint32_t wakeen;
    uint32_t corb_size;
    uint32_t rirb_size;
    uint32_t codec_mask;
};

uint32_t driver_str_len(const char *text);
int driver_streq(const char *lhs, const char *rhs);
int driver_starts_with(const char *text, const char *prefix);

void *driver_memcpy(void *dst, const void *src, uint32_t size);
void *driver_memmove(void *dst, const void *src, uint32_t size);
void *driver_memset(void *dst, int value, uint32_t size);

void *driver_alloc_pages(uint32_t page_count, uint64_t *phys_out);
void *driver_alloc_pages_below(uint32_t page_count,
                               uint64_t max_phys_exclusive,
                               uint64_t *phys_out);
void driver_free_pages(void *virt, uint32_t page_count);
void *driver_mmio_map(uint64_t phys);

int driver_audio_register_device(const struct driver_audio_device_info *info,
                                 const struct driver_audio_device_ops *ops,
                                 void *ctx,
                                 uint32_t *index_out);
int driver_hda_publish_device(const struct driver_hda_device_info *info);

uint8_t driver_io_in8(uint16_t port);
uint16_t driver_io_in16(uint16_t port);
uint32_t driver_io_in32(uint16_t port);
void driver_io_out8(uint16_t port, uint8_t value);
void driver_io_out16(uint16_t port, uint16_t value);
void driver_io_out32(uint16_t port, uint32_t value);

int driver_pci_find_by_class(uint8_t class_code,
                             uint8_t subclass,
                             uint32_t index,
                             struct driver_pci_device *out);
int driver_pci_find_by_id(uint16_t vendor_id,
                          uint16_t device_id,
                          uint32_t index,
                          struct driver_pci_device *out);
uint8_t driver_pci_read8(const struct driver_pci_device *dev, uint8_t offset);
uint16_t driver_pci_read16(const struct driver_pci_device *dev, uint8_t offset);
uint32_t driver_pci_read32(const struct driver_pci_device *dev, uint8_t offset);
void driver_pci_write8(const struct driver_pci_device *dev, uint8_t offset, uint8_t value);
void driver_pci_write16(const struct driver_pci_device *dev, uint8_t offset, uint16_t value);
void driver_pci_write32(const struct driver_pci_device *dev, uint8_t offset, uint32_t value);

uint32_t driver_timer_current_ticks(void);
uint32_t driver_timer_hz(void);

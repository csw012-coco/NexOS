#include "kernel/internal/core/system_query_internal.h"

#include "block/blockdev.h"
#include "drivers/audio/ac97.h"
#include "drivers/audio/audio.h"
#include "drivers/audio/hda.h"
#include "drivers/bus/pci.h"
#include "drivers/net/rtl8139.h"
#include "drivers/rtc/cmos.h"

enum {
    KERNEL_AUDIO_NAME_MAX = 32u
};

static void kernel_query_copy_name(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void kernel_query_pci_info(struct syscall_pci_info *info) {
    struct pci_ide_controller ide;

    if (info == 0) {
        return;
    }

    info->present = 0;
    info->bus = 0;
    info->slot = 0;
    info->function = 0;
    info->prog_if = 0;
    info->vendor_id = 0;
    info->device_id = 0;
    info->bar0 = 0;
    info->bar1 = 0;
    info->bar2 = 0;
    info->bar3 = 0;
    info->bar4 = 0;

    if (!pci_find_ide_controller(&ide)) {
        return;
    }

    info->present = 1;
    info->bus = ide.bus;
    info->slot = ide.slot;
    info->function = ide.function;
    info->prog_if = ide.prog_if;
    info->vendor_id = ide.vendor_id;
    info->device_id = ide.device_id;
    info->bar0 = ide.bar0;
    info->bar1 = ide.bar1;
    info->bar2 = ide.bar2;
    info->bar3 = ide.bar3;
    info->bar4 = ide.bar4;
}

void kernel_query_ac97_info(struct syscall_ac97_info *info) {
    struct ac97_status status;

    if (info == 0) {
        return;
    }

    info->present = 0;
    info->initialized = 0;
    info->bus = 0;
    info->slot = 0;
    info->function = 0;
    info->prog_if = 0;
    info->irq_line = 0;
    info->irq_pin = 0;
    info->vendor_id = 0;
    info->device_id = 0;
    info->nambar = 0;
    info->nabmbar = 0;
    info->mixer_reset = 0;
    info->powerdown = 0;
    info->ext_audio_id = 0;
    info->ext_audio_ctrl = 0;
    info->codec_id = 0;
    info->global_status = 0;
    info->global_control = 0;

    if (!ac97_query_status(&status)) {
        return;
    }

    info->present = status.present;
    info->initialized = status.initialized;
    info->bus = status.bus;
    info->slot = status.slot;
    info->function = status.function;
    info->prog_if = status.prog_if;
    info->irq_line = status.irq_line;
    info->irq_pin = status.irq_pin;
    info->vendor_id = status.vendor_id;
    info->device_id = status.device_id;
    info->nambar = status.nambar;
    info->nabmbar = status.nabmbar;
    info->mixer_reset = status.mixer_reset;
    info->powerdown = status.powerdown;
    info->ext_audio_id = status.ext_audio_id;
    info->ext_audio_ctrl = status.ext_audio_ctrl;
    info->codec_id = status.codec_id;
    info->global_status = status.global_status;
    info->global_control = status.global_control;
}

void kernel_query_hda_info(struct syscall_hda_info *info) {
    struct hda_status status;

    if (info == 0) {
        return;
    }

    info->present = 0;
    info->initialized = 0;
    info->bus = 0;
    info->slot = 0;
    info->function = 0;
    info->prog_if = 0;
    info->irq_line = 0;
    info->irq_pin = 0;
    info->vendor_id = 0;
    info->device_id = 0;
    info->mmio_base_lo = 0;
    info->mmio_base_hi = 0;
    info->pci_command = 0;
    info->gcap = 0;
    info->vmaj = 0;
    info->vmin = 0;
    info->outpay = 0;
    info->inpay = 0;
    info->gctl = 0;
    info->statests = 0;
    info->wakeen = 0;
    info->corb_size = 0;
    info->rirb_size = 0;
    info->codec_mask = 0;

    if (!hda_query_status(&status)) {
        return;
    }

    info->present = status.present;
    info->initialized = status.initialized;
    info->bus = status.bus;
    info->slot = status.slot;
    info->function = status.function;
    info->prog_if = status.prog_if;
    info->irq_line = status.irq_line;
    info->irq_pin = status.irq_pin;
    info->vendor_id = status.vendor_id;
    info->device_id = status.device_id;
    info->mmio_base_lo = status.mmio_base_lo;
    info->mmio_base_hi = status.mmio_base_hi;
    info->pci_command = status.pci_command;
    info->gcap = status.gcap;
    info->vmaj = status.vmaj;
    info->vmin = status.vmin;
    info->outpay = status.outpay;
    info->inpay = status.inpay;
    info->gctl = status.gctl;
    info->statests = status.statests;
    info->wakeen = status.wakeen;
    info->corb_size = status.corb_size;
    info->rirb_size = status.rirb_size;
    info->codec_mask = status.codec_mask;
}

void kernel_query_rtl8139_info(struct syscall_rtl8139_info *info) {
    struct rtl8139_status status;
    uint32_t i;

    if (info == 0) {
        return;
    }

    info->present = 0;
    info->initialized = 0;
    info->bus = 0;
    info->slot = 0;
    info->function = 0;
    info->prog_if = 0;
    info->irq_line = 0;
    info->irq_pin = 0;
    info->vendor_id = 0;
    info->device_id = 0;
    info->io_base = 0;
    info->pci_command = 0;
    info->chip_cmd = 0;
    info->media_status = 0;
    info->intr_mask = 0;
    info->intr_status = 0;
    info->tx_config = 0;
    info->rx_config = 0;
    info->link_up = 0;
    info->speed_mbps = 0;
    for (i = 0; i < sizeof(info->mac); i++) {
        info->mac[i] = 0;
    }
    info->reserved0[0] = 0;
    info->reserved0[1] = 0;

    if (!rtl8139_query_status(&status)) {
        return;
    }

    info->present = status.present;
    info->initialized = status.initialized;
    info->bus = status.bus;
    info->slot = status.slot;
    info->function = status.function;
    info->prog_if = status.prog_if;
    info->irq_line = status.irq_line;
    info->irq_pin = status.irq_pin;
    info->vendor_id = status.vendor_id;
    info->device_id = status.device_id;
    info->io_base = status.io_base;
    info->pci_command = status.pci_command;
    info->chip_cmd = status.chip_cmd;
    info->media_status = status.media_status;
    info->intr_mask = status.intr_mask;
    info->intr_status = status.intr_status;
    info->tx_config = status.tx_config;
    info->rx_config = status.rx_config;
    info->link_up = status.link_up;
    info->speed_mbps = status.speed_mbps;
    info->capr = status.capr;
    info->cbr = status.cbr;
    info->rx_read_offset = status.rx_read_offset;
    for (i = 0; i < sizeof(info->mac); i++) {
        info->mac[i] = status.mac[i];
    }
}

int kernel_rtl8139_send_test_frame(void) {
    return rtl8139_send_test_frame();
}

int kernel_rtl8139_send_frame(const uint8_t *data, uint32_t bytes) {
    return rtl8139_send_frame(data, bytes);
}

int kernel_rtl8139_receive_packet(struct syscall_rtl8139_rx_info *info) {
    struct rtl8139_rx_packet packet;
    uint32_t i;

    if (info == 0) {
        return 0;
    }
    info->packet_status = 0u;
    info->packet_length = 0u;
    info->bytes_copied = 0u;
    for (i = 0; i < sizeof(info->data); i++) {
        info->data[i] = 0u;
    }
    if (!rtl8139_receive_packet(&packet)) {
        return 0;
    }
    info->packet_status = packet.packet_status;
    info->packet_length = packet.packet_length;
    info->bytes_copied = packet.bytes_copied;
    for (i = 0; i < sizeof(info->data); i++) {
        info->data[i] = packet.data[i];
    }
    return 1;
}

int kernel_query_audio_info(uint32_t index, struct syscall_audio_info *info) {
    struct audio_device_info device;

    if (info == 0) {
        return 0;
    }

    info->present = 0;
    info->initialized = 0;
    info->caps = 0;
    info->driver_kind = 0;
    info->sample_rate = 0;
    info->channels = 0;
    info->bits_per_sample = 0;
    info->name[0] = '\0';

    if (!audio_query_device(index, &device)) {
        return 0;
    }

    info->present = device.present;
    info->initialized = device.initialized;
    info->caps = device.caps;
    info->driver_kind = device.driver_kind;
    info->sample_rate = device.sample_rate;
    info->channels = device.channels;
    info->bits_per_sample = device.bits_per_sample;
    kernel_query_copy_name(info->name, KERNEL_AUDIO_NAME_MAX, device.name);
    return info->present != 0u;
}

int kernel_query_rtc_info(struct syscall_rtc_info *info) {
    struct cmos_rtc_info rtc;

    if (info == 0) {
        return 0;
    }

    info->present = 0u;
    info->updating = 0u;
    info->valid = 0u;
    info->binary_mode = 0u;
    info->hour_24 = 0u;
    info->status_a = 0u;
    info->status_b = 0u;
    info->century = 0u;
    info->raw_year = 0u;
    info->second = 0u;
    info->minute = 0u;
    info->hour = 0u;
    info->weekday = 0u;
    info->day = 0u;
    info->month = 0u;
    info->year = 0u;
    info->unix_time = 0u;

    if (!cmos_rtc_query(&rtc)) {
        info->present = rtc.present;
        info->updating = rtc.updating;
        info->valid = rtc.valid;
        return 0;
    }

    info->present = rtc.present;
    info->updating = rtc.updating;
    info->valid = rtc.valid;
    info->binary_mode = rtc.binary_mode;
    info->hour_24 = rtc.hour_24;
    info->status_a = rtc.status_a;
    info->status_b = rtc.status_b;
    info->century = rtc.century;
    info->raw_year = rtc.raw_year;
    info->second = rtc.second;
    info->minute = rtc.minute;
    info->hour = rtc.hour;
    info->weekday = rtc.weekday;
    info->day = rtc.day;
    info->month = rtc.month;
    info->year = rtc.year;
    info->unix_time = rtc.unix_time;
    return info->present != 0u;
}

int kernel_audio_play_tone(uint32_t index, uint32_t hz, uint32_t duration_ms) {
    return audio_play_tone(index, hz, duration_ms);
}

int kernel_audio_play_buffer(uint32_t index,
                             const struct syscall_audio_play_info *play_info,
                             const uint8_t *data) {
    if (play_info == 0 || data == 0) {
        return 0;
    }
    return audio_play_pcm(index,
                          data,
                          play_info->bytes,
                          play_info->sample_rate,
                          play_info->channels,
                          play_info->bits_per_sample);
}

int kernel_query_block_info(uint32_t index, struct syscall_block_info *info) {
    struct blockdev_info block_info;
    struct block_device *dev;

    if (info == 0) {
        return 0;
    }

    dev = blockdev_get(index);
    if (dev == 0 || blockdev_get_info(index, &block_info) != 0) {
        return 0;
    }

    info->index = index;
    info->block_size = block_info.block_size;
    info->partition_count = blockdev_partition_count(dev);
    info->writable = block_info.writable;
    info->block_count = block_info.block_count;
    kernel_query_copy_name(info->name, sizeof(info->name), block_info.name);
    return 1;
}

int kernel_query_part_info(uint32_t disk_index, uint32_t slot, struct syscall_partition_info *info) {
    struct block_device *dev;
    struct blockdev_partition part;

    if (info == 0) {
        return 0;
    }

    dev = blockdev_get(disk_index);
    if (dev == 0 || blockdev_partition_get(dev, slot, &part) != 0) {
        return 0;
    }

    info->disk_index = disk_index;
    info->slot = slot;
    info->part_index = part.index;
    info->start_lba = part.start_lba;
    info->sector_count = part.sector_count;
    info->type = part.type;
    info->bootable = part.bootable;
    return 1;
}

int kernel_block_read(uint32_t disk_index, uint64_t lba, struct syscall_block_read_info *info) {
    struct block_device *dev;
    uint32_t i;

    if (info == 0) {
        return 0;
    }

    dev = blockdev_get(disk_index);
    if (dev == 0 || dev->block_size == 0 || dev->block_size > sizeof(info->data)) {
        return 0;
    }

    info->disk_index = disk_index;
    info->block_size = dev->block_size;
    info->bytes_read = 0;
    info->reserved = 0;
    info->lba = lba;
    for (i = 0; i < sizeof(info->data); i++) {
        info->data[i] = 0;
    }
    if (blockdev_read(dev, lba, 1, info->data) != 0) {
        return 0;
    }
    info->bytes_read = dev->block_size;
    return 1;
}

int kernel_block_write(uint32_t disk_index, uint64_t lba, struct syscall_block_write_info *info) {
    struct block_device *dev;

    if (info == 0) {
        return 0;
    }

    dev = blockdev_get(disk_index);
    if (dev == 0 || dev->write == 0 || dev->block_size == 0 || dev->block_size > sizeof(info->data)) {
        return 0;
    }
    if (info->bytes_to_write != dev->block_size) {
        return 0;
    }
    if (blockdev_write(dev, lba, 1, info->data) != 0) {
        return 0;
    }

    info->disk_index = disk_index;
    info->block_size = dev->block_size;
    info->bytes_written = dev->block_size;
    info->lba = lba;
    return 1;
}

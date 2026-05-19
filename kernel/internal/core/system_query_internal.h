#pragma once

#include <stdint.h>

#include "kernel/public/sys/syscall.h"

void kernel_query_pci_info(struct syscall_pci_info *info);
void kernel_query_ac97_info(struct syscall_ac97_info *info);
void kernel_query_hda_info(struct syscall_hda_info *info);
void kernel_query_rtl8139_info(struct syscall_rtl8139_info *info);
int kernel_rtl8139_send_test_frame(void);
int kernel_rtl8139_send_frame(const uint8_t *data, uint32_t bytes);
int kernel_rtl8139_receive_packet(struct syscall_rtl8139_rx_info *info);
int kernel_query_audio_info(uint32_t index, struct syscall_audio_info *info);
int kernel_audio_play_tone(uint32_t index, uint32_t hz, uint32_t duration_ms);
int kernel_audio_play_buffer(uint32_t index,
                             const struct syscall_audio_play_info *play_info,
                             const uint8_t *data);
int kernel_query_rtc_info(struct syscall_rtc_info *info);
int kernel_query_block_info(uint32_t index, struct syscall_block_info *info);
int kernel_query_part_info(uint32_t disk_index, uint32_t slot, struct syscall_partition_info *info);
int kernel_block_read(uint32_t disk_index, uint64_t lba, struct syscall_block_read_info *info);
int kernel_block_write(uint32_t disk_index, uint64_t lba, struct syscall_block_write_info *info);

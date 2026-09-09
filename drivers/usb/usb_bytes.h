#pragma once

#include <stdint.h>

static inline uint16_t usb_read_u16le(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static inline uint32_t usb_read_u32le(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static inline uint32_t usb_read_u32be(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static inline uint64_t usb_read_u64be(const uint8_t *data) {
    return ((uint64_t)data[0] << 56) |
           ((uint64_t)data[1] << 48) |
           ((uint64_t)data[2] << 40) |
           ((uint64_t)data[3] << 32) |
           ((uint64_t)data[4] << 24) |
           ((uint64_t)data[5] << 16) |
           ((uint64_t)data[6] << 8) |
           (uint64_t)data[7];
}

static inline void usb_write_u16le(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
}

static inline void usb_write_u32le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
    data[2] = (uint8_t)((value >> 16) & 0xffu);
    data[3] = (uint8_t)((value >> 24) & 0xffu);
}

static inline void usb_write_u32be(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)((value >> 24) & 0xffu);
    data[1] = (uint8_t)((value >> 16) & 0xffu);
    data[2] = (uint8_t)((value >> 8) & 0xffu);
    data[3] = (uint8_t)(value & 0xffu);
}

static inline void usb_write_u64be(uint8_t *data, uint64_t value) {
    data[0] = (uint8_t)((value >> 56) & 0xffu);
    data[1] = (uint8_t)((value >> 48) & 0xffu);
    data[2] = (uint8_t)((value >> 40) & 0xffu);
    data[3] = (uint8_t)((value >> 32) & 0xffu);
    data[4] = (uint8_t)((value >> 24) & 0xffu);
    data[5] = (uint8_t)((value >> 16) & 0xffu);
    data[6] = (uint8_t)((value >> 8) & 0xffu);
    data[7] = (uint8_t)(value & 0xffu);
}

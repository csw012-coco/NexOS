#pragma once

#include <stdint.h>

struct rtl8139_status {
    uint8_t present;
    uint8_t initialized;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t io_base;
    uint16_t pci_command;
    uint16_t intr_mask;
    uint16_t intr_status;
    uint8_t chip_cmd;
    uint8_t media_status;
    uint16_t speed_mbps;
    uint8_t link_up;
    uint8_t reserved0;
    uint8_t mac[6];
    uint8_t reserved1[2];
    uint32_t tx_config;
    uint32_t rx_config;
    uint32_t capr;
    uint32_t cbr;
    uint32_t rx_read_offset;
};

struct rtl8139_rx_packet {
    uint16_t packet_status;
    uint16_t packet_length;
    uint32_t bytes_copied;
    uint8_t data[2048];
};

int rtl8139_init(void);
int rtl8139_query_status(struct rtl8139_status *out);
int rtl8139_send_frame(const uint8_t *data, uint32_t bytes);
int rtl8139_send_test_frame(void);
int rtl8139_receive_packet(struct rtl8139_rx_packet *out);
int rtl8139_handle_irq(uint8_t irq_line);

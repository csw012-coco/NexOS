#include "user/apps/elf/nexbox/applets/net/cmdsuite_net_common.h"

static void rtl8139_decode_arp_local(const uint8_t *data, uint32_t size) {
    uint16_t hw_type;
    uint16_t proto_type;
    uint16_t opcode;

    if (size < 28u) {
        write_str("arp: packet too short\n");
        return;
    }

    hw_type = read_be16_local(data + 0u);
    proto_type = read_be16_local(data + 2u);
    opcode = read_be16_local(data + 6u);

    write_str("arp: htype=");
    write_hex_u32(hw_type);
    write_str(" ptype=");
    write_hex_u32(proto_type);
    write_str(" hlen=");
    write_dec(data[4]);
    write_str(" plen=");
    write_dec(data[5]);
    write_str(" op=");
    write_dec(opcode);
    write_str("\n");
    if (data[4] == 6u && data[5] == 4u) {
        write_str("arp: sender ");
        write_mac_local(data + 8u);
        write_str(" ");
        write_ipv4_local(data + 14u);
        write_str("\n");
        write_str("arp: target ");
        write_mac_local(data + 18u);
        write_str(" ");
        write_ipv4_local(data + 24u);
        write_str("\n");
    }
}

static void rtl8139_decode_ipv4_local(const uint8_t *data, uint32_t size) {
    uint8_t version;
    uint8_t ihl_bytes;
    uint16_t total_length;

    if (size < 20u) {
        write_str("ipv4: packet too short\n");
        return;
    }

    version = (uint8_t)(data[0] >> 4);
    ihl_bytes = (uint8_t)((data[0] & 0x0fu) * 4u);
    total_length = read_be16_local(data + 2u);

    write_str("ipv4: ver=");
    write_dec(version);
    write_str(" ihl=");
    write_dec(ihl_bytes);
    write_str(" total=");
    write_dec(total_length);
    write_str(" ttl=");
    write_dec(data[8]);
    write_str(" proto=");
    write_str(ipv4_protocol_name_local(data[9]));
    write_str(" (");
    write_dec(data[9]);
    write_str(")");
    write_str("\n");
    write_str("ipv4: src=");
    write_ipv4_local(data + 12u);
    write_str(" dst=");
    write_ipv4_local(data + 16u);
    write_str("\n");
    if (ihl_bytes > 20u && ihl_bytes <= size) {
        write_str("ipv4: options=");
        write_dec((uint32_t)ihl_bytes - 20u);
        write_str(" bytes\n");
    }
}

static void rtl8139_decode_frame_local(const uint8_t *data, uint32_t size) {
    uint16_t ether_type;

    if (size < 14u) {
        write_str("eth: frame too short\n");
        return;
    }

    ether_type = read_be16_local(data + 12u);
    write_str("eth: dst=");
    write_mac_local(data + 0u);
    write_str(" src=");
    write_mac_local(data + 6u);
    write_str(" type=");
    write_hex_u32(ether_type);
    write_str("\n");

    if (ether_type == 0x0806u) {
        rtl8139_decode_arp_local(data + 14u, size - 14u);
    } else if (ether_type == 0x0800u) {
        rtl8139_decode_ipv4_local(data + 14u, size - 14u);
    } else if (ether_type == 0x86ddu) {
        write_str("ipv6: decode not implemented yet\n");
    } else {
        write_str("eth: unknown payload type\n");
    }
}

int cmd_rtl8139(void) {
    struct syscall_rtl8139_info info;
    uint32_t i;

    if (rtl8139_query(&info) <= 0 || !info.present) {
        write_err_str("rtl8139: controller not found\n");
        return 1;
    }
    write_str("RTL8139 controller\n");
    write_str("bdf ");
    write_dec(info.bus);
    write_str(":");
    write_dec(info.slot);
    write_str(".");
    write_dec(info.function);
    write_str(" vendor=");
    write_hex_u32(info.vendor_id);
    write_str(" device=");
    write_hex_u32(info.device_id);
    write_str(" irq=");
    write_dec(info.irq_line);
    write_str(" pin=");
    write_dec(info.irq_pin);
    write_str("\n");
    write_str("io ");
    write_hex_u32(info.io_base);
    write_str(" pci_cmd=");
    write_hex_u32(info.pci_command);
    write_str(" cmd=");
    write_hex_u32(info.chip_cmd);
    write_str(" imr=");
    write_hex_u32(info.intr_mask);
    write_str(" isr=");
    write_hex_u32(info.intr_status);
    write_str("\n");
    write_str("media=");
    write_hex_u32(info.media_status);
    write_str(" speed=");
    write_dec(info.speed_mbps);
    write_str("Mbps link=");
    write_str(info.link_up ? "up" : "down");
    write_str(" init=");
    write_dec(info.initialized);
    write_str("\n");
    write_str("mac ");
    for (i = 0; i < 6u; i++) {
        if (i != 0u) {
            write_str(":");
        }
        write_hex_byte_local(info.mac[i]);
    }
    write_str("\n");
    write_str("cfg tx=");
    write_hex_u32(info.tx_config);
    write_str(" rx=");
    write_hex_u32(info.rx_config);
    write_str(" capr=");
    write_hex_u32(info.capr);
    write_str(" cbr=");
    write_hex_u32(info.cbr);
    write_str(" cur=");
    write_hex_u32(info.rx_read_offset);
    write_str(" prog_if=");
    write_hex_u32(info.prog_if);
    write_str("\n");
    return 0;
}

int cmd_rtl8139tx(int argc, char **argv) {
    (void)argv;

    if (argc > 1) {
        write_err_usage("rtl8139tx", "\n");
        return 1;
    }
    if (rtl8139_tx_test() <= 0) {
        write_err_str("rtl8139tx: transmit failed\n");
        return 1;
    }
    write_str("rtl8139tx: test frame sent\n");
    return 0;
}

int cmd_rtl8139rx(int argc, char **argv) {
    uint32_t wire_bytes;
    uint32_t row;
    uint32_t i;

    (void)argv;

    if (argc > 1) {
        write_err_usage("rtl8139rx", "\n");
        return 1;
    }
    if (rtl8139_rx_dump(&g_net_rx_info_local) <= 0) {
        write_err_str("rtl8139rx: no packet\n");
        return 1;
    }
    write_str("rtl8139rx: status=");
    write_hex_u32(g_net_rx_info_local.packet_status);
    write_str(" length=");
    write_dec(g_net_rx_info_local.packet_length);
    write_str(" copied=");
    write_dec(g_net_rx_info_local.bytes_copied);
    write_str("\n");
    wire_bytes = g_net_rx_info_local.packet_length > 4u ? g_net_rx_info_local.packet_length - 4u : 0u;
    rtl8139_decode_frame_local(g_net_rx_info_local.data,
                               g_net_rx_info_local.bytes_copied < wire_bytes
                                   ? g_net_rx_info_local.bytes_copied
                                   : wire_bytes);
    for (row = 0; row < g_net_rx_info_local.bytes_copied; row += 16u) {
        write_hex_u32(row);
        write_str(": ");
        for (i = 0; i < 16u && row + i < g_net_rx_info_local.bytes_copied; i++) {
            write_hex_byte_local(g_net_rx_info_local.data[row + i]);
            write_str(" ");
        }
        write_str("\n");
    }
    return 0;
}


int cmd_route(int argc, char **argv) {
    uint8_t network[4];
    uint32_t i;

    (void)argv;
    if (argc > 1) {
        write_err_usage("route", "\n");
        return 1;
    }
    for (i = 0; i < 4u; i++) {
        network[i] = (uint8_t)(g_ipv4_config_local.ip[i] & g_ipv4_config_local.mask[i]);
    }
    write_str("Destination      Gateway          Mask             Iface\n");
    write_str("default          ");
    write_ipv4_local(g_ipv4_config_local.gateway);
    write_str("      0.0.0.0          rtl8139\n");
    write_ipv4_local(network);
    write_str("      0.0.0.0          ");
    write_ipv4_local(g_ipv4_config_local.mask);
    write_str("    rtl8139\n");
    return 0;
}

int cmd_netstat(int argc, char **argv) {
    struct syscall_rtl8139_info nic;
    uint32_t arp_count = 0u;
    uint32_t i;

    (void)argv;
    if (argc > 1) {
        write_err_usage("netstat", "\n");
        return 1;
    }
    if (rtl8139_query(&nic) <= 0 || !nic.present) {
        write_err_str("netstat: rtl8139 not present\n");
        return 1;
    }
    for (i = 0; i < (uint32_t)(sizeof(g_arp_cache_local) / sizeof(g_arp_cache_local[0])); i++) {
        if (g_arp_cache_local[i].valid) {
            arp_count++;
        }
    }
    write_str("Iface    State Link Speed  Address         Gateway         DNS             ARP  RXOFF  CBR\n");
    write_str("rtl8139  ");
    write_str(nic.initialized ? "up   " : "down ");
    write_str(nic.link_up ? "up   " : "down ");
    write_dec(nic.speed_mbps);
    write_str("Mbps ");
    write_ipv4_local(g_ipv4_config_local.ip);
    write_str("  ");
    write_ipv4_local(g_ipv4_config_local.gateway);
    write_str("  ");
    write_ipv4_local(g_ipv4_config_local.dns);
    write_str("  ");
    write_dec(arp_count);
    write_str("    ");
    write_dec(nic.rx_read_offset);
    write_str("    ");
    write_dec(nic.cbr);
    write_str("\n");
    write_str("active sockets: not tracked yet\n");
    return 0;
}




int cmd_ifconfig(int argc, char **argv) {
    struct syscall_rtl8139_info nic;

    (void)argv;
    if (argc > 1) {
        write_err_usage("ifconfig", "\n");
        return 1;
    }
    if (rtl8139_query(&nic) <= 0 || !nic.present) {
        write_str("ifconfig: rtl8139 not present\n");
        return 1;
    }

    write_str("rtl8139\n");
    write_str("  state ");
    write_str(nic.initialized ? "up" : "down");
    write_str(" link=");
    write_str(nic.link_up ? "up" : "down");
    write_str(" speed=");
    write_dec(nic.speed_mbps);
    write_str("Mbps irq=");
    write_dec(nic.irq_line);
    write_str(" io=");
    write_hex_u32(nic.io_base);
    write_str("\n");
    write_str("  mac ");
    write_mac_local(nic.mac);
    write_str("\n");
    write_str("  ipv4 ");
    write_ipv4_local(g_ipv4_config_local.ip);
    write_str("\n");
    if (g_ipv4_config_local.got_mask) {
        write_str("  mask ");
        write_ipv4_local(g_ipv4_config_local.mask);
        write_str("\n");
    }
    if (g_ipv4_config_local.got_gateway) {
        write_str("  gateway ");
        write_ipv4_local(g_ipv4_config_local.gateway);
        write_str("\n");
    }
    if (g_ipv4_config_local.got_dns) {
        write_str("  dns ");
        write_ipv4_local(g_ipv4_config_local.dns);
        write_str("\n");
    }
    if (g_ipv4_config_local.got_server) {
        write_str("  dhcp-server ");
        write_ipv4_local(g_ipv4_config_local.server);
        write_str("\n");
    }
    write_str("  source ");
    write_str(g_ipv4_config_local.from_dhcp ? "dhcp" : "default");
    write_str("\n");
    if (g_ipv4_config_local.got_lease_time) {
        write_str("  lease ");
        write_dec(g_ipv4_config_local.lease_time);
        write_str("s\n");
    }
    return 0;
}

#include "user/apps/elf/nexbox/applets/net/cmdsuite_net_common.h"

struct nexbox_net_context_local g_net_context_local = {
    .ipv4_config = {
        {10u, 0u, 2u, 15u},
        {255u, 255u, 255u, 0u},
        {10u, 0u, 2u, 2u},
        {10u, 0u, 2u, 3u},
        {10u, 0u, 2u, 2u},
        0u,
        1u,
        1u,
        1u,
        1u,
        0u,
        0u
    }
};

static void arp_cache_store_local(const uint8_t ip[4], const uint8_t mac[6]) {
    uint32_t i;
    uint32_t slot = 0u;
    uint32_t oldest = 0xffffffffu;

    for (i = 0; i < (uint32_t)(sizeof(g_arp_cache_local) / sizeof(g_arp_cache_local[0])); i++) {
        if (g_arp_cache_local[i].valid &&
            g_arp_cache_local[i].ip[0] == ip[0] &&
            g_arp_cache_local[i].ip[1] == ip[1] &&
            g_arp_cache_local[i].ip[2] == ip[2] &&
            g_arp_cache_local[i].ip[3] == ip[3]) {
            slot = i;
            oldest = 0u;
            break;
        }
        if (!g_arp_cache_local[i].valid) {
            slot = i;
            oldest = 0u;
            break;
        }
        if (g_arp_cache_local[i].tick < oldest) {
            oldest = g_arp_cache_local[i].tick;
            slot = i;
        }
    }
    for (i = 0; i < 4u; i++) {
        g_arp_cache_local[slot].ip[i] = ip[i];
    }
    for (i = 0; i < 6u; i++) {
        g_arp_cache_local[slot].mac[i] = mac[i];
    }
    g_arp_cache_local[slot].tick = ticks();
    g_arp_cache_local[slot].valid = 1u;
}

static int arp_cache_lookup_local(const uint8_t ip[4], uint8_t mac[6]) {
    uint32_t i;

    for (i = 0; i < (uint32_t)(sizeof(g_arp_cache_local) / sizeof(g_arp_cache_local[0])); i++) {
        if (!g_arp_cache_local[i].valid) {
            continue;
        }
        if (g_arp_cache_local[i].ip[0] == ip[0] &&
            g_arp_cache_local[i].ip[1] == ip[1] &&
            g_arp_cache_local[i].ip[2] == ip[2] &&
            g_arp_cache_local[i].ip[3] == ip[3]) {
            if (mac != 0) {
                uint32_t j;

                for (j = 0; j < 6u; j++) {
                    mac[j] = g_arp_cache_local[i].mac[j];
                }
            }
            return 1;
        }
    }
    return 0;
}

void write_hex_byte_local(uint8_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char text[2];

    text[0] = hex[(value >> 4) & 0x0fu];
    text[1] = hex[value & 0x0fu];
    write_stdout(text, sizeof(text));
}

uint16_t read_be16_local(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

uint32_t read_be32_local(const uint8_t *src) {
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

int parse_ipv4_local(const char *text, uint8_t out[4]) {
    uint32_t index = 0;
    const char *cursor = text;

    if (text == 0) {
        return 0;
    }
    while (index < 4u) {
        char *endptr = 0;
        unsigned long part = strtoul(cursor, &endptr, 10);

        if (endptr == cursor || part > 255u) {
            return 0;
        }
        out[index++] = (uint8_t)part;
        if (index == 4u) {
            return *endptr == '\0';
        }
        if (*endptr != '.') {
            return 0;
        }
        cursor = endptr + 1;
    }
    return 0;
}

void write_mac_local(const uint8_t *mac) {
    uint32_t i;

    for (i = 0; i < 6u; i++) {
        if (i != 0u) {
            write_str(":");
        }
        write_hex_byte_local(mac[i]);
    }
}

void write_ipv4_local(const uint8_t *addr) {
    write_dec(addr[0]);
    write_str(".");
    write_dec(addr[1]);
    write_str(".");
    write_dec(addr[2]);
    write_str(".");
    write_dec(addr[3]);
}

void write_u16_be_local(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xffu);
}

void write_u32_be_local(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

uint16_t checksum16_local(const uint8_t *data, uint32_t bytes) {
    uint32_t sum = 0;
    uint32_t i;

    for (i = 0; i + 1u < bytes; i += 2u) {
        sum += ((uint32_t)data[i] << 8) | (uint32_t)data[i + 1u];
    }
    if ((bytes & 1u) != 0u) {
        sum += (uint32_t)data[bytes - 1u] << 8;
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint32_t checksum16_accumulate_local(uint32_t sum, const uint8_t *data, uint32_t bytes) {
    uint32_t i;

    for (i = 0; i + 1u < bytes; i += 2u) {
        sum += ((uint32_t)data[i] << 8) | (uint32_t)data[i + 1u];
    }
    if ((bytes & 1u) != 0u) {
        sum += (uint32_t)data[bytes - 1u] << 8;
    }
    return sum;
}

uint16_t udp_checksum_ipv4_local(const uint8_t src_ip[4],
                                        const uint8_t dst_ip[4],
                                        const uint8_t *udp,
                                        uint16_t udp_length) {
    uint8_t pseudo[12];
    uint32_t sum;
    uint16_t checksum;

    pseudo[0] = src_ip[0];
    pseudo[1] = src_ip[1];
    pseudo[2] = src_ip[2];
    pseudo[3] = src_ip[3];
    pseudo[4] = dst_ip[0];
    pseudo[5] = dst_ip[1];
    pseudo[6] = dst_ip[2];
    pseudo[7] = dst_ip[3];
    pseudo[8] = 0u;
    pseudo[9] = 17u;
    write_u16_be_local(pseudo + 10u, udp_length);
    sum = checksum16_accumulate_local(0u, pseudo, sizeof(pseudo));
    sum = checksum16_accumulate_local(sum, udp, udp_length);
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    checksum = (uint16_t)~sum;
    return checksum != 0u ? checksum : 0xffffu;
}

uint16_t tcp_checksum_ipv4_local(const uint8_t src_ip[4],
                                        const uint8_t dst_ip[4],
                                        const uint8_t *tcp,
                                        uint16_t tcp_length) {
    uint8_t pseudo[12];
    uint32_t sum;
    uint16_t checksum;

    pseudo[0] = src_ip[0];
    pseudo[1] = src_ip[1];
    pseudo[2] = src_ip[2];
    pseudo[3] = src_ip[3];
    pseudo[4] = dst_ip[0];
    pseudo[5] = dst_ip[1];
    pseudo[6] = dst_ip[2];
    pseudo[7] = dst_ip[3];
    pseudo[8] = 0u;
    pseudo[9] = 6u;
    write_u16_be_local(pseudo + 10u, tcp_length);
    sum = checksum16_accumulate_local(0u, pseudo, sizeof(pseudo));
    sum = checksum16_accumulate_local(sum, tcp, tcp_length);
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    checksum = (uint16_t)~sum;
    return checksum != 0u ? checksum : 0xffffu;
}

void build_arp_request_local(uint8_t *frame,
                                    const uint8_t src_mac[6],
                                    const uint8_t src_ip[4],
                                    const uint8_t target_ip[4]) {
    uint32_t i;

    for (i = 0; i < 6u; i++) {
        frame[i] = 0xffu;
        frame[6u + i] = src_mac[i];
    }
    frame[12] = 0x08u;
    frame[13] = 0x06u;
    write_u16_be_local(frame + 14u, 1u);
    write_u16_be_local(frame + 16u, 0x0800u);
    frame[18] = 6u;
    frame[19] = 4u;
    write_u16_be_local(frame + 20u, 1u);
    for (i = 0; i < 6u; i++) {
        frame[22u + i] = src_mac[i];
        frame[32u + i] = 0u;
    }
    for (i = 0; i < 4u; i++) {
        frame[28u + i] = src_ip[i];
        frame[38u + i] = target_ip[i];
    }
    for (i = 42u; i < 60u; i++) {
        frame[i] = 0u;
    }
}

static void build_icmp_echo_local(uint8_t *frame,
                                  const uint8_t src_mac[6],
                                  const uint8_t dst_mac[6],
                                  const uint8_t src_ip[4],
                                  const uint8_t dst_ip[4],
                                  uint16_t ident,
                                  uint16_t seq) {
    static const uint8_t payload[] = "NexOS ping";
    uint8_t *ip = frame + 14u;
    uint8_t *icmp = ip + 20u;
    uint16_t total_length = 20u + 8u + (uint16_t)(sizeof(payload) - 1u);
    uint32_t i;

    for (i = 0; i < 6u; i++) {
        frame[i] = dst_mac[i];
        frame[6u + i] = src_mac[i];
    }
    frame[12] = 0x08u;
    frame[13] = 0x00u;

    ip[0] = 0x45u;
    ip[1] = 0x00u;
    write_u16_be_local(ip + 2u, total_length);
    write_u16_be_local(ip + 4u, 0u);
    write_u16_be_local(ip + 6u, 0x4000u);
    ip[8] = 64u;
    ip[9] = 1u;
    ip[10] = 0u;
    ip[11] = 0u;
    for (i = 0; i < 4u; i++) {
        ip[12u + i] = src_ip[i];
        ip[16u + i] = dst_ip[i];
    }
    write_u16_be_local(ip + 10u, checksum16_local(ip, 20u));

    icmp[0] = 8u;
    icmp[1] = 0u;
    icmp[2] = 0u;
    icmp[3] = 0u;
    write_u16_be_local(icmp + 4u, ident);
    write_u16_be_local(icmp + 6u, seq);
    for (i = 0; i < sizeof(payload) - 1u; i++) {
        icmp[8u + i] = payload[i];
    }
    write_u16_be_local(icmp + 2u, checksum16_local(icmp, 8u + (uint16_t)(sizeof(payload) - 1u)));
}

int same_subnet24_local(const uint8_t a[4], const uint8_t b[4]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

void ipv4_copy_runtime_config_local(uint8_t ip[4],
                                           uint8_t mask[4],
                                           uint8_t gateway[4],
                                           uint8_t dns[4]) {
    uint32_t i;

    for (i = 0; i < 4u; i++) {
        if (ip != 0) {
            ip[i] = g_ipv4_config_local.ip[i];
        }
        if (mask != 0) {
            mask[i] = g_ipv4_config_local.mask[i];
        }
        if (gateway != 0) {
            gateway[i] = g_ipv4_config_local.gateway[i];
        }
        if (dns != 0) {
            dns[i] = g_ipv4_config_local.dns[i];
        }
    }
}

void ipv4_apply_dhcp_config_local(const struct dhcp_config_local *config) {
    uint32_t i;

    if (config == 0) {
        return;
    }
    for (i = 0; i < 4u; i++) {
        g_ipv4_config_local.ip[i] = config->yiaddr[i];
        if (config->got_subnet_mask) {
            g_ipv4_config_local.mask[i] = config->subnet_mask[i];
        }
        if (config->got_router) {
            g_ipv4_config_local.gateway[i] = config->router[i];
        }
        if (config->got_dns) {
            g_ipv4_config_local.dns[i] = config->dns[i];
        }
        if (config->got_server_id) {
            g_ipv4_config_local.server[i] = config->server_id[i];
        }
    }
    g_ipv4_config_local.got_mask = config->got_subnet_mask;
    g_ipv4_config_local.got_gateway = config->got_router;
    g_ipv4_config_local.got_dns = config->got_dns;
    g_ipv4_config_local.got_server = config->got_server_id;
    g_ipv4_config_local.got_lease_time = config->got_lease_time;
    g_ipv4_config_local.lease_time = config->lease_time;
    g_ipv4_config_local.from_dhcp = 1u;
}

int rtl8139_wait_arp_reply_local(const uint8_t target_ip[4],
                                        uint8_t target_mac[6],
                                        uint32_t timeout_ticks) {
    uint32_t start = ticks();

    while ((uint32_t)(ticks() - start) < timeout_ticks) {
        if (rtl8139_rx_dump(&g_net_rx_info_local) > 0) {
            if (g_net_rx_info_local.bytes_copied >= 42u &&
                read_be16_local(g_net_rx_info_local.data + 12u) == 0x0806u &&
                read_be16_local(g_net_rx_info_local.data + 20u) == 2u &&
                g_net_rx_info_local.data[28] == target_ip[0] &&
                g_net_rx_info_local.data[29] == target_ip[1] &&
                g_net_rx_info_local.data[30] == target_ip[2] &&
                g_net_rx_info_local.data[31] == target_ip[3]) {
                uint32_t i;

                for (i = 0; i < 6u; i++) {
                    target_mac[i] = g_net_rx_info_local.data[22u + i];
                }
                arp_cache_store_local(target_ip, target_mac);
                return 1;
            }
        }
        yield();
    }
    return 0;
}

static int rtl8139_wait_icmp_reply_local(const uint8_t src_ip[4],
                                         const uint8_t target_ip[4],
                                         uint16_t ident,
                                         uint16_t seq,
                                         uint32_t timeout_ticks,
                                         uint32_t *reply_bytes_out) {
    uint32_t start = ticks();

    while ((uint32_t)(ticks() - start) < timeout_ticks) {
        if (rtl8139_rx_dump(&g_net_rx_info_local) > 0) {
            if (g_net_rx_info_local.bytes_copied >= 42u &&
                read_be16_local(g_net_rx_info_local.data + 12u) == 0x0800u &&
                g_net_rx_info_local.data[23] == 1u &&
                g_net_rx_info_local.data[26] == target_ip[0] &&
                g_net_rx_info_local.data[27] == target_ip[1] &&
                g_net_rx_info_local.data[28] == target_ip[2] &&
                g_net_rx_info_local.data[29] == target_ip[3] &&
                g_net_rx_info_local.data[30] == src_ip[0] &&
                g_net_rx_info_local.data[31] == src_ip[1] &&
                g_net_rx_info_local.data[32] == src_ip[2] &&
                g_net_rx_info_local.data[33] == src_ip[3] &&
                g_net_rx_info_local.data[34] == 0u &&
                g_net_rx_info_local.data[35] == 0u &&
                read_be16_local(g_net_rx_info_local.data + 38u) == ident &&
                read_be16_local(g_net_rx_info_local.data + 40u) == seq) {
                if (reply_bytes_out != 0) {
                    *reply_bytes_out = g_net_rx_info_local.packet_length > 4u
                                           ? g_net_rx_info_local.packet_length - 4u
                                           : g_net_rx_info_local.bytes_copied;
                }
                return 1;
            }
        }
        yield();
    }
    return 0;
}

int cmd_arp(int argc, char **argv) {
    struct syscall_rtl8139_info nic;
    uint8_t src_ip[4];
    uint8_t gateway_ip[4];
    uint8_t target_ip[4];
    const uint8_t *arp_ip = target_ip;
    uint8_t target_mac[6];
    uint32_t i;
    int printed = 0;

    if (argc > 2) {
        write_err_usage("arp", " [ipv4]\n");
        return 1;
    }
    if (argc == 1) {
        for (i = 0; i < (uint32_t)(sizeof(g_arp_cache_local) / sizeof(g_arp_cache_local[0])); i++) {
            if (!g_arp_cache_local[i].valid) {
                continue;
            }
            write_ipv4_local(g_arp_cache_local[i].ip);
            write_str(" at ");
            write_mac_local(g_arp_cache_local[i].mac);
            write_str(" age=");
            write_dec((uint32_t)(ticks() - g_arp_cache_local[i].tick));
            write_str("ticks\n");
            printed = 1;
        }
        if (!printed) {
            write_str("arp: cache empty\n");
        }
        return 0;
    }
    if (!parse_ipv4_local(argv[1], target_ip)) {
        write_err_usage("arp", " [ipv4]\n");
        return 1;
    }
    if (arp_cache_lookup_local(target_ip, target_mac)) {
        write_ipv4_local(target_ip);
        write_str(" at ");
        write_mac_local(target_mac);
        write_str(" (cached)\n");
        return 0;
    }
    ipv4_copy_runtime_config_local(src_ip, 0, gateway_ip, 0);
    if (rtl8139_query(&nic) <= 0 || !nic.present || !nic.initialized || !nic.link_up) {
        write_err_str("arp: rtl8139 not ready\n");
        return 1;
    }
    if (!same_subnet24_local(src_ip, target_ip)) {
        arp_ip = gateway_ip;
    }
    build_arp_request_local(g_arp_frame_local, nic.mac, src_ip, arp_ip);
    if (rtl8139_tx_send(g_arp_frame_local, sizeof(g_arp_frame_local)) <= 0) {
        write_err_str("arp: request send failed\n");
        return 1;
    }
    if (!rtl8139_wait_arp_reply_local(arp_ip, target_mac, 100u)) {
        write_err_str("arp: timeout\n");
        return 1;
    }
    write_ipv4_local(arp_ip);
    write_str(" at ");
    write_mac_local(target_mac);
    if (arp_ip != target_ip) {
        write_str(" for ");
        write_ipv4_local(target_ip);
    }
    write_str("\n");
    return 0;
}

int cmd_ping(int argc, char **argv) {
    struct syscall_rtl8139_info nic;
    uint8_t src_ip[4];
    uint8_t gateway_ip[4];
    uint8_t target_ip[4] = {10u, 0u, 2u, 2u};
    const uint8_t *arp_ip = target_ip;
    uint8_t target_mac[6];
    uint8_t icmp_frame[64];
    uint32_t reply_bytes = 0;
    uint32_t send_tick = 0;
    uint32_t recv_tick = 0;
    uint32_t rtt_ticks = 0;
    uint32_t rtt_ms = 0;
    uint16_t ident = 0x1234u;
    uint16_t seq = 1u;

    if (argc > 2) {
        write_err_usage("ping", " [ipv4]\n");
        return 1;
    }
    if (argc == 2 && !parse_ipv4_local(argv[1], target_ip)) {
        write_err_usage("ping", " [ipv4]\n");
        return 1;
    }
    ipv4_copy_runtime_config_local(src_ip, 0, gateway_ip, 0);
    if (rtl8139_query(&nic) <= 0 || !nic.present || !nic.initialized || !nic.link_up) {
        write_err_str("ping: rtl8139 not ready\n");
        return 1;
    }

    write_str("PING ");
    write_ipv4_local(target_ip);
    write_str(" via rtl8139\n");

    if (!same_subnet24_local(src_ip, target_ip)) {
        arp_ip = gateway_ip;
    }

    build_arp_request_local(g_arp_frame_local, nic.mac, src_ip, arp_ip);
    if (rtl8139_tx_send(g_arp_frame_local, sizeof(g_arp_frame_local)) <= 0) {
        write_err_str("ping: arp request send failed\n");
        return 1;
    }
    if (!rtl8139_wait_arp_reply_local(arp_ip, target_mac, 100u)) {
        write_err_str("ping: arp timeout\n");
        return 1;
    }

    write_str("arp: ");
    write_ipv4_local(arp_ip);
    write_str(" is-at ");
    write_mac_local(target_mac);
    write_str("\n");

    build_icmp_echo_local(icmp_frame, nic.mac, target_mac, src_ip, target_ip, ident, seq);
    send_tick = ticks();
    if (rtl8139_tx_send(icmp_frame, 52u) <= 0) {
        write_err_str("ping: icmp send failed\n");
        return 1;
    }
    if (!rtl8139_wait_icmp_reply_local(src_ip, target_ip, ident, seq, 150u, &reply_bytes)) {
        write_err_str("ping: reply timeout\n");
        return 1;
    }
    recv_tick = ticks();
    rtt_ticks = recv_tick - send_tick;
    rtt_ms = rtt_ticks * 10u;

    write_str("reply from ");
    write_ipv4_local(target_ip);
    write_str(": bytes=");
    write_dec(reply_bytes);
    write_str(" seq=");
    write_dec(seq);
    write_str(" ttl=64 rtt=");
    write_dec(rtt_ms);
    write_str("ms\n");
    return 0;
}

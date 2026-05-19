#include "user/apps/elf/nexbox/applets/net/cmdsuite_net_common.h"

uint32_t build_tcp_ipv4_local(uint8_t *frame,
                                     const uint8_t src_mac[6],
                                     const uint8_t dst_mac[6],
                                     const uint8_t src_ip[4],
                                     const uint8_t dst_ip[4],
                                     uint16_t src_port,
                                     uint16_t dst_port,
                                     uint32_t seq,
                                     uint32_t ack,
                                     uint16_t flags,
                                     const uint8_t *payload,
                                     uint16_t payload_bytes) {
    uint8_t *ip = frame + 14u;
    uint8_t *tcp = ip + 20u;
    uint16_t tcp_length = (uint16_t)(20u + payload_bytes);
    uint16_t total_length = (uint16_t)(20u + tcp_length);
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
    write_u16_be_local(ip + 4u, (uint16_t)seq);
    write_u16_be_local(ip + 6u, 0x4000u);
    ip[8] = 64u;
    ip[9] = 6u;
    ip[10] = 0u;
    ip[11] = 0u;
    for (i = 0; i < 4u; i++) {
        ip[12u + i] = src_ip[i];
        ip[16u + i] = dst_ip[i];
    }
    write_u16_be_local(ip + 10u, checksum16_local(ip, 20u));

    write_u16_be_local(tcp + 0u, src_port);
    write_u16_be_local(tcp + 2u, dst_port);
    write_u32_be_local(tcp + 4u, seq);
    write_u32_be_local(tcp + 8u, ack);
    tcp[12] = 0x50u;
    tcp[13] = (uint8_t)flags;
    write_u16_be_local(tcp + 14u, 4096u);
    write_u16_be_local(tcp + 16u, 0u);
    write_u16_be_local(tcp + 18u, 0u);
    for (i = 0; i < payload_bytes; i++) {
        tcp[20u + i] = payload[i];
    }
    write_u16_be_local(tcp + 16u, tcp_checksum_ipv4_local(src_ip, dst_ip, tcp, tcp_length));

    return 14u + (uint32_t)total_length;
}
static int tcp_parse_packet_local(const struct syscall_rtl8139_rx_info *info,
                                  const uint8_t src_ip[4],
                                  const uint8_t dst_ip[4],
                                  uint16_t src_port,
                                  uint16_t dst_port,
                                  struct tcp_packet_local *out) {
    uint32_t ip_offset = 14u;
    uint32_t ihl_bytes;
    uint32_t tcp_offset;
    uint32_t tcp_header_bytes;
    uint32_t ip_bytes;

    if (info == 0 || out == 0 || info->bytes_copied < 14u + 20u + 20u) {
        return 0;
    }
    if (read_be16_local(info->data + 12u) != 0x0800u || info->data[ip_offset + 9u] != 6u) {
        return 0;
    }
    ihl_bytes = (uint32_t)(info->data[ip_offset] & 0x0fu) * 4u;
    ip_bytes = (uint32_t)read_be16_local(info->data + ip_offset + 2u);
    tcp_offset = ip_offset + ihl_bytes;
    if (ihl_bytes < 20u || tcp_offset + 20u > info->bytes_copied || ip_offset + ip_bytes > info->bytes_copied) {
        return 0;
    }
    if (info->data[ip_offset + 12u] != src_ip[0] ||
        info->data[ip_offset + 13u] != src_ip[1] ||
        info->data[ip_offset + 14u] != src_ip[2] ||
        info->data[ip_offset + 15u] != src_ip[3] ||
        info->data[ip_offset + 16u] != dst_ip[0] ||
        info->data[ip_offset + 17u] != dst_ip[1] ||
        info->data[ip_offset + 18u] != dst_ip[2] ||
        info->data[ip_offset + 19u] != dst_ip[3] ||
        read_be16_local(info->data + tcp_offset + 0u) != src_port ||
        read_be16_local(info->data + tcp_offset + 2u) != dst_port) {
        return 0;
    }
    tcp_header_bytes = (uint32_t)((info->data[tcp_offset + 12u] >> 4) & 0x0fu) * 4u;
    if (tcp_header_bytes < 20u || tcp_offset + tcp_header_bytes > info->bytes_copied) {
        return 0;
    }
    out->seq = read_be32_local(info->data + tcp_offset + 4u);
    out->ack = read_be32_local(info->data + tcp_offset + 8u);
    out->flags = (uint16_t)info->data[tcp_offset + 13u];
    out->window = read_be16_local(info->data + tcp_offset + 14u);
    out->payload_offset = tcp_offset + tcp_header_bytes;
    out->payload_length = ip_bytes - ihl_bytes - tcp_header_bytes;
    return 1;
}
int rtl8139_wait_tcp_packet_local(const uint8_t src_ip[4],
                                         const uint8_t dst_ip[4],
                                         uint16_t src_port,
                                         uint16_t dst_port,
                                         uint32_t timeout_ticks,
                                         struct tcp_packet_local *packet_out) {
    uint32_t start = ticks();

    while ((uint32_t)(ticks() - start) < timeout_ticks) {
        if (rtl8139_rx_dump(&g_net_rx_info_local) > 0) {
            if (tcp_parse_packet_local(&g_net_rx_info_local, src_ip, dst_ip, src_port, dst_port, packet_out)) {
                return 1;
            }
        }
        yield();
    }
    return 0;
}
uint32_t rtl8139_drain_rx_local(uint32_t max_packets) {
    uint32_t count = 0;

    while (count < max_packets && rtl8139_rx_dump(&g_net_rx_info_local) > 0) {
        count++;
    }
    return count;
}
const char *ipv4_protocol_name_local(uint8_t protocol) {
    switch (protocol) {
        case 1: return "ICMP";
        case 6: return "TCP";
        case 17: return "UDP";
        default: return "unknown";
    }
}
uint16_t http_next_src_port_local(void) {
    g_http_client_nonce_local++;
    return (uint16_t)(40000u + ((ticks() + g_http_client_nonce_local * 257u) & 0x1fffu));
}
uint32_t http_next_client_seq_local(void) {
    g_http_client_nonce_local++;
    return 0x48545450u ^ ticks() ^ (g_http_client_nonce_local * 0x01010101u);
}
int http_send_tcp_segment_local(const struct syscall_rtl8139_info *nic,
                                const uint8_t target_mac[6],
                                const uint8_t src_ip[4],
                                const uint8_t target_ip[4],
                                uint16_t src_port,
                                uint16_t dst_port,
                                uint32_t seq,
                                uint32_t ack,
                                uint16_t flags,
                                const uint8_t *payload,
                                uint16_t payload_bytes) {
    uint32_t frame_bytes;

    if (nic == 0 || target_mac == 0 || src_ip == 0 || target_ip == 0) {
        return 0;
    }
    frame_bytes = build_tcp_ipv4_local(g_http_frame_local,
                                       nic->mac,
                                       target_mac,
                                       src_ip,
                                       target_ip,
                                       src_port,
                                       dst_port,
                                       seq,
                                       ack,
                                       flags,
                                       payload,
                                       payload_bytes);
    return rtl8139_tx_send(g_http_frame_local, frame_bytes) > 0;
}
void http_close_connection_local(const struct syscall_rtl8139_info *nic,
                                 const uint8_t target_mac[6],
                                 const uint8_t src_ip[4],
                                 const uint8_t target_ip[4],
                                 uint16_t src_port,
                                 uint32_t *client_seq_io,
                                 uint32_t *server_seq_io) {
    struct tcp_packet_local packet;
    uint32_t client_seq;
    uint32_t server_seq;
    uint32_t start;

    if (nic == 0 || target_mac == 0 || src_ip == 0 || target_ip == 0 ||
        client_seq_io == 0 || server_seq_io == 0) {
        return;
    }
    client_seq = *client_seq_io;
    server_seq = *server_seq_io;
    if (!http_send_tcp_segment_local(nic,
                                     target_mac,
                                     src_ip,
                                     target_ip,
                                     src_port,
                                     80u,
                                     client_seq,
                                     server_seq,
                                     TCP_FLAG_ACK | TCP_FLAG_FIN,
                                     0,
                                     0u)) {
        return;
    }
    client_seq += 1u;
    start = ticks();
    while ((uint32_t)(ticks() - start) < 60u) {
        if (!rtl8139_wait_tcp_packet_local(target_ip, src_ip, 80u, src_port, 20u, &packet)) {
            continue;
        }
        if ((packet.flags & TCP_FLAG_RST) != 0u) {
            break;
        }
        if ((packet.flags & TCP_FLAG_FIN) != 0u && packet.seq + packet.payload_length == server_seq) {
            server_seq += packet.payload_length + 1u;
            (void)http_send_tcp_segment_local(nic,
                                              target_mac,
                                              src_ip,
                                              target_ip,
                                              src_port,
                                              80u,
                                              client_seq,
                                              server_seq,
                                              TCP_FLAG_ACK,
                                              0,
                                              0u);
            break;
        }
        if ((packet.flags & TCP_FLAG_ACK) != 0u && packet.ack == client_seq) {
            break;
        }
    }
    *client_seq_io = client_seq;
    *server_seq_io = server_seq;
}
int tcp_connect_local(const char *host,
                             uint16_t dst_port,
                             struct tcp_session_local *session,
                             int print_status) {
    struct tcp_packet_local packet;
    uint8_t dns_ip[4];
    uint8_t gateway_ip[4];
    const uint8_t *arp_ip;
    uint32_t syn_attempt;

    if (host == 0 || session == 0 || dst_port == 0u) {
        return 0;
    }
    if (rtl8139_query(&session->nic) <= 0 || !session->nic.present || !session->nic.initialized || !session->nic.link_up) {
        write_str("tcp: rtl8139 not ready\n");
        return 0;
    }
    ipv4_copy_runtime_config_local(session->src_ip, 0, gateway_ip, dns_ip);
    if (!parse_ipv4_local(host, session->target_ip)) {
        if (print_status) {
            write_str("tcp: resolving ");
            write_str(host);
            write_str("\n");
        }
        if (!dns_lookup_ipv4_local(&session->nic, session->src_ip, dns_ip, host, session->target_ip)) {
            write_str("tcp: dns lookup failed\n");
            return 0;
        }
        if (print_status) {
            write_str("tcp: ");
            write_str(host);
            write_str(" -> ");
            write_ipv4_local(session->target_ip);
            write_str("\n");
        }
    }
    arp_ip = same_subnet24_local(session->src_ip, session->target_ip) ? session->target_ip : gateway_ip;
    rtl8139_drain_rx_local(32u);
    build_arp_request_local(g_arp_frame_local, session->nic.mac, session->src_ip, arp_ip);
    if (rtl8139_tx_send(g_arp_frame_local, sizeof(g_arp_frame_local)) <= 0) {
        write_str("tcp: arp request send failed\n");
        return 0;
    }
    if (!rtl8139_wait_arp_reply_local(arp_ip, session->target_mac, 100u)) {
        write_str("tcp: arp timeout\n");
        return 0;
    }

    session->dst_port = dst_port;
    session->src_port = http_next_src_port_local();
    session->client_seq = http_next_client_seq_local();
    session->server_seq = 0u;

    for (syn_attempt = 0u; syn_attempt < 3u; syn_attempt++) {
        if (!http_send_tcp_segment_local(&session->nic,
                                         session->target_mac,
                                         session->src_ip,
                                         session->target_ip,
                                         session->src_port,
                                         session->dst_port,
                                         session->client_seq,
                                         0u,
                                         TCP_FLAG_SYN,
                                         0,
                                         0u)) {
            write_str("tcp: syn send failed\n");
            return 0;
        }
        if (rtl8139_wait_tcp_packet_local(session->target_ip,
                                          session->src_ip,
                                          session->dst_port,
                                          session->src_port,
                                          300u,
                                          &packet)) {
            break;
        }
    }
    if (syn_attempt >= 3u) {
        write_str("tcp: synack timeout\n");
        return 0;
    }
    if ((packet.flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) != (TCP_FLAG_SYN | TCP_FLAG_ACK) ||
        packet.ack != session->client_seq + 1u) {
        write_str("tcp: unexpected handshake packet flags=");
        write_hex_u32(packet.flags);
        write_str("\n");
        return 0;
    }
    session->client_seq += 1u;
    session->server_seq = packet.seq + 1u;
    if (!http_send_tcp_segment_local(&session->nic,
                                     session->target_mac,
                                     session->src_ip,
                                     session->target_ip,
                                     session->src_port,
                                     session->dst_port,
                                     session->client_seq,
                                     session->server_seq,
                                     TCP_FLAG_ACK,
                                     0,
                                     0u)) {
        write_str("tcp: ack send failed\n");
        return 0;
    }
    return 1;
}
int tcp_send_local(struct tcp_session_local *session,
                          uint16_t flags,
                          const uint8_t *payload,
                          uint16_t payload_bytes) {
    uint32_t advance = payload_bytes;

    if (session == 0) {
        return 0;
    }
    if (!http_send_tcp_segment_local(&session->nic,
                                     session->target_mac,
                                     session->src_ip,
                                     session->target_ip,
                                     session->src_port,
                                     session->dst_port,
                                     session->client_seq,
                                     session->server_seq,
                                     flags,
                                     payload,
                                     payload_bytes)) {
        return 0;
    }
    if ((flags & TCP_FLAG_SYN) != 0u || (flags & TCP_FLAG_FIN) != 0u) {
        advance += 1u;
    }
    session->client_seq += advance;
    return 1;
}
int tcp_ack_local(struct tcp_session_local *session) {
    if (session == 0) {
        return 0;
    }
    return http_send_tcp_segment_local(&session->nic,
                                       session->target_mac,
                                       session->src_ip,
                                       session->target_ip,
                                       session->src_port,
                                       session->dst_port,
                                       session->client_seq,
                                       session->server_seq,
                                       TCP_FLAG_ACK,
                                       0,
                                       0u);
}
int tcp_recv_local(struct tcp_session_local *session,
                          uint32_t timeout_ticks,
                          struct tcp_packet_local *packet_out) {
    if (session == 0) {
        return 0;
    }
    return rtl8139_wait_tcp_packet_local(session->target_ip,
                                         session->src_ip,
                                         session->dst_port,
                                         session->src_port,
                                         timeout_ticks,
                                         packet_out);
}
void tcp_close_local(struct tcp_session_local *session) {
    if (session == 0) {
        return;
    }
    http_close_connection_local(&session->nic,
                                session->target_mac,
                                session->src_ip,
                                session->target_ip,
                                session->src_port,
                                &session->client_seq,
                                &session->server_seq);
}

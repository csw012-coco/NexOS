#include "user/apps/elf/nexbox/applets/net/cmdsuite_net_common.h"

enum {
    DNS_TYPE_A = 1u,
    DNS_TYPE_CNAME = 5u,
    DNS_TYPE_MX = 15u,
    DNS_TYPE_AAAA = 28u,
    DNS_TYPE_ANY = 255u
};

static int dns_skip_name_local(const uint8_t *msg,
                               uint32_t size,
                               uint32_t offset,
                               uint32_t *next_offset_out);
static int dns_read_name_local(const uint8_t *msg,
                               uint32_t size,
                               uint32_t offset,
                               char *out,
                               uint32_t out_size,
                               uint32_t *next_offset_out);
static const char *dns_type_name_local(uint16_t type);
static int dns_parse_qtype_local(const char *text, uint16_t *out_type);
static void write_ipv6_local(const uint8_t *addr);
static int dns_answer_matches_qtype_local(uint16_t qtype, uint16_t rr_type);
struct dns_wait_debug_local {
    uint32_t saw_packet;
    uint16_t ether_type;
    uint8_t ip_proto;
    uint8_t src_ip[4];
    uint8_t dst_ip[4];
    uint16_t udp_src_port;
    uint16_t udp_dst_port;
    uint16_t dns_id;
    uint8_t icmp_type;
    uint8_t icmp_code;
};

static int dns_encode_name_local(const char *name,
                                 uint8_t *dst,
                                 uint32_t dst_size,
                                 uint32_t *written_out) {
    const char *label = name;
    uint32_t total = 0;
    uint32_t label_len = 0;
    uint32_t i = 0;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    while (1) {
        char ch = name[i];

        if (ch == '.' || ch == '\0') {
            uint32_t j;

            if (label_len == 0u || label_len > 63u || total + 1u + label_len >= dst_size) {
                return 0;
            }
            dst[total++] = (uint8_t)label_len;
            for (j = 0; j < label_len; j++) {
                dst[total++] = (uint8_t)label[j];
            }
            if (ch == '\0') {
                break;
            }
            label = name + i + 1u;
            label_len = 0u;
        } else {
            label_len++;
        }
        i++;
    }
    if (total >= dst_size) {
        return 0;
    }
    dst[total++] = 0u;
    if (written_out != 0) {
        *written_out = total;
    }
    return 1;
}

static int dns_extract_first_a_local(const uint8_t *dns,
                                     uint32_t dns_size,
                                     uint8_t out_ip[4]) {
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t flags;
    uint32_t offset = 12u;
    uint32_t i;

    if (dns_size < 12u) {
        return 0;
    }
    flags = read_be16_local(dns + 2u);
    qdcount = read_be16_local(dns + 4u);
    ancount = read_be16_local(dns + 6u);
    if ((flags & 0x8000u) == 0u || (flags & 0x000fu) != 0u) {
        return 0;
    }
    for (i = 0; i < qdcount; i++) {
        if (!dns_skip_name_local(dns, dns_size, offset, &offset) || offset + 4u > dns_size) {
            return 0;
        }
        offset += 4u;
    }
    for (i = 0; i < ancount; i++) {
        uint16_t rr_type;
        uint16_t rr_class;
        uint32_t rdata_len;

        if (!dns_skip_name_local(dns, dns_size, offset, &offset) || offset + 10u > dns_size) {
            return 0;
        }
        rr_type = read_be16_local(dns + offset);
        rr_class = read_be16_local(dns + offset + 2u);
        rdata_len = (uint32_t)read_be16_local(dns + offset + 8u);
        offset += 10u;
        if (offset + rdata_len > dns_size) {
            return 0;
        }
        if (rr_type == 1u && rr_class == 1u && rdata_len == 4u) {
            out_ip[0] = dns[offset + 0u];
            out_ip[1] = dns[offset + 1u];
            out_ip[2] = dns[offset + 2u];
            out_ip[3] = dns[offset + 3u];
            return 1;
        }
        offset += rdata_len;
    }
    return 0;
}

static uint32_t build_dns_query_local(uint8_t *frame,
                                      const uint8_t src_mac[6],
                                      const uint8_t dst_mac[6],
                                      const uint8_t src_ip[4],
                                      const uint8_t dst_ip[4],
                                      const char *name,
                                      uint16_t qtype,
                                      uint16_t dns_id,
                                      uint16_t src_port) {
    uint32_t qname_bytes = 0;
    uint8_t *ip = frame + 14u;
    uint8_t *udp = ip + 20u;
    uint8_t *dns = udp + 8u;
    uint16_t udp_length;
    uint16_t total_length;
    uint32_t i;

    if (!dns_encode_name_local(name, g_dns_name_local, sizeof(g_dns_name_local), &qname_bytes)) {
        return 0;
    }
    udp_length = (uint16_t)(8u + 12u + qname_bytes + 4u);
    total_length = (uint16_t)(20u + udp_length);

    for (i = 0; i < 6u; i++) {
        frame[i] = dst_mac[i];
        frame[6u + i] = src_mac[i];
    }
    frame[12] = 0x08u;
    frame[13] = 0x00u;

    ip[0] = 0x45u;
    ip[1] = 0x00u;
    write_u16_be_local(ip + 2u, total_length);
    write_u16_be_local(ip + 4u, dns_id);
    write_u16_be_local(ip + 6u, 0x4000u);
    ip[8] = 64u;
    ip[9] = 17u;
    ip[10] = 0u;
    ip[11] = 0u;
    for (i = 0; i < 4u; i++) {
        ip[12u + i] = src_ip[i];
        ip[16u + i] = dst_ip[i];
    }
    write_u16_be_local(ip + 10u, checksum16_local(ip, 20u));

    write_u16_be_local(udp + 0u, src_port);
    write_u16_be_local(udp + 2u, 53u);
    write_u16_be_local(udp + 4u, udp_length);
    write_u16_be_local(udp + 6u, 0u);

    write_u16_be_local(dns + 0u, dns_id);
    write_u16_be_local(dns + 2u, 0x0100u);
    write_u16_be_local(dns + 4u, 1u);
    write_u16_be_local(dns + 6u, 0u);
    write_u16_be_local(dns + 8u, 0u);
    write_u16_be_local(dns + 10u, 0u);
    for (i = 0; i < qname_bytes; i++) {
        dns[12u + i] = g_dns_name_local[i];
    }
    write_u16_be_local(dns + 12u + qname_bytes, qtype);
    write_u16_be_local(dns + 14u + qname_bytes, 1u);
    write_u16_be_local(udp + 6u, udp_checksum_ipv4_local(src_ip, dst_ip, udp, udp_length));

    return 14u + (uint32_t)total_length;
}





static int dns_skip_name_local(const uint8_t *msg,
                               uint32_t size,
                               uint32_t offset,
                               uint32_t *next_offset_out) {
    uint32_t cursor = offset;
    uint32_t consumed = offset;
    uint32_t jumps = 0;

    while (cursor < size) {
        uint8_t len = msg[cursor];

        if ((len & 0xc0u) == 0xc0u) {
            if (cursor + 1u >= size) {
                return 0;
            }
            if (next_offset_out != 0) {
                *next_offset_out = consumed + 2u;
            }
            return 1;
        }
        if (len == 0u) {
            if (next_offset_out != 0) {
                *next_offset_out = consumed + 1u;
            }
            return 1;
        }
        cursor++;
        consumed++;
        if (len > 63u || cursor + len > size) {
            return 0;
        }
        cursor += len;
        consumed += len;
        jumps++;
        if (jumps > 64u) {
            return 0;
        }
    }
    return 0;
}

static int dns_read_name_local(const uint8_t *msg,
                               uint32_t size,
                               uint32_t offset,
                               char *out,
                               uint32_t out_size,
                               uint32_t *next_offset_out) {
    uint32_t cursor = offset;
    uint32_t next_offset = offset;
    uint32_t out_len = 0u;
    uint32_t jumps = 0u;
    uint8_t jumped = 0u;

    if (out == 0 || out_size == 0u || msg == 0) {
        return 0;
    }
    out[0] = '\0';
    while (cursor < size) {
        uint8_t len = msg[cursor];

        if ((len & 0xc0u) == 0xc0u) {
            uint32_t target;

            if (cursor + 1u >= size) {
                return 0;
            }
            target = (uint32_t)(((uint32_t)(len & 0x3fu) << 8) | (uint32_t)msg[cursor + 1u]);
            if (!jumped) {
                next_offset = cursor + 2u;
            }
            if (target >= size || jumps++ > 64u) {
                return 0;
            }
            cursor = target;
            jumped = 1u;
            continue;
        }
        if (len == 0u) {
            if (!jumped) {
                next_offset = cursor + 1u;
            }
            out[out_len] = '\0';
            if (next_offset_out != 0) {
                *next_offset_out = next_offset;
            }
            return 1;
        }
        cursor++;
        if (len > 63u || cursor + len > size) {
            return 0;
        }
        if (out_len != 0u) {
            if (out_len + 1u >= out_size) {
                return 0;
            }
            out[out_len++] = '.';
        }
        if (out_len + len >= out_size) {
            return 0;
        }
        while (len-- != 0u) {
            out[out_len++] = (char)msg[cursor++];
        }
        if (!jumped) {
            next_offset = cursor;
        }
    }
    return 0;
}

static int rtl8139_wait_dns_reply_local(const uint8_t src_ip[4],
                                        const uint8_t server_ip[4],
                                        uint16_t src_port,
                                        uint16_t dns_id,
                                        uint32_t timeout_ticks,
                                        struct syscall_rtl8139_rx_info *reply_out,
                                        struct dns_wait_debug_local *debug_out) {
    uint32_t start = ticks();

    if (debug_out != 0) {
        uint32_t i;

        debug_out->saw_packet = 0u;
        debug_out->ether_type = 0u;
        debug_out->ip_proto = 0u;
        debug_out->udp_src_port = 0u;
        debug_out->udp_dst_port = 0u;
        debug_out->dns_id = 0u;
        debug_out->icmp_type = 0u;
        debug_out->icmp_code = 0u;
        for (i = 0; i < 4u; i++) {
            debug_out->src_ip[i] = 0u;
            debug_out->dst_ip[i] = 0u;
        }
    }

    while ((uint32_t)(ticks() - start) < timeout_ticks) {
        if (rtl8139_rx_dump(&g_net_rx_info_local) > 0) {
            if (debug_out != 0 && g_net_rx_info_local.bytes_copied >= 14u) {
                uint32_t i;

                debug_out->saw_packet = 1u;
                debug_out->ether_type = read_be16_local(g_net_rx_info_local.data + 12u);
                if (debug_out->ether_type == 0x0800u && g_net_rx_info_local.bytes_copied >= 34u) {
                    debug_out->ip_proto = g_net_rx_info_local.data[23];
                    for (i = 0; i < 4u; i++) {
                        debug_out->src_ip[i] = g_net_rx_info_local.data[26u + i];
                        debug_out->dst_ip[i] = g_net_rx_info_local.data[30u + i];
                    }
                    if (debug_out->ip_proto == 17u && g_net_rx_info_local.bytes_copied >= 46u) {
                        debug_out->udp_src_port = read_be16_local(g_net_rx_info_local.data + 34u);
                        debug_out->udp_dst_port = read_be16_local(g_net_rx_info_local.data + 36u);
                        debug_out->dns_id = read_be16_local(g_net_rx_info_local.data + 42u);
                    } else if (debug_out->ip_proto == 1u && g_net_rx_info_local.bytes_copied >= 36u) {
                        debug_out->icmp_type = g_net_rx_info_local.data[34];
                        debug_out->icmp_code = g_net_rx_info_local.data[35];
                    }
                }
            }
            if (g_net_rx_info_local.bytes_copied >= 42u &&
                read_be16_local(g_net_rx_info_local.data + 12u) == 0x0800u) {
                uint32_t ip_offset = 14u;
                uint32_t ihl_bytes = (uint32_t)(g_net_rx_info_local.data[ip_offset] & 0x0fu) * 4u;
                uint32_t udp_offset = ip_offset + ihl_bytes;

                if (ihl_bytes >= 20u &&
                    udp_offset + 12u <= g_net_rx_info_local.bytes_copied &&
                    g_net_rx_info_local.data[ip_offset + 9u] == 17u &&
                    g_net_rx_info_local.data[ip_offset + 12u] == server_ip[0] &&
                    g_net_rx_info_local.data[ip_offset + 13u] == server_ip[1] &&
                    g_net_rx_info_local.data[ip_offset + 14u] == server_ip[2] &&
                    g_net_rx_info_local.data[ip_offset + 15u] == server_ip[3] &&
                    g_net_rx_info_local.data[ip_offset + 16u] == src_ip[0] &&
                    g_net_rx_info_local.data[ip_offset + 17u] == src_ip[1] &&
                    g_net_rx_info_local.data[ip_offset + 18u] == src_ip[2] &&
                    g_net_rx_info_local.data[ip_offset + 19u] == src_ip[3] &&
                    read_be16_local(g_net_rx_info_local.data + udp_offset + 0u) == 53u &&
                    read_be16_local(g_net_rx_info_local.data + udp_offset + 2u) == src_port &&
                    read_be16_local(g_net_rx_info_local.data + udp_offset + 8u) == dns_id) {
                    if (reply_out != 0) {
                        *reply_out = g_net_rx_info_local;
                    }
                    return 1;
                }
            }
        }
        yield();
    }
    return 0;
}

static int dns_print_answers_local(const uint8_t *dns, uint32_t dns_size, uint16_t qtype) {
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t flags;
    uint32_t offset = 12u;
    uint32_t i;
    int printed = 0;

    if (dns_size < 12u) {
        write_str("dns: reply too short\n");
        return 0;
    }
    flags = read_be16_local(dns + 2u);
    qdcount = read_be16_local(dns + 4u);
    ancount = read_be16_local(dns + 6u);

    write_str("dns: flags=");
    write_hex_u32(flags);
    write_str(" answers=");
    write_dec(ancount);
    write_str("\n");

    if ((flags & 0x8000u) == 0u) {
        write_str("dns: not a response\n");
        return 0;
    }
    if ((flags & 0x000fu) != 0u) {
        write_str("dns: server returned error rcode=");
        write_dec((uint32_t)(flags & 0x000fu));
        write_str("\n");
        return 0;
    }

    for (i = 0; i < qdcount; i++) {
        if (!dns_skip_name_local(dns, dns_size, offset, &offset) || offset + 4u > dns_size) {
            write_str("dns: malformed question\n");
            return 0;
        }
        offset += 4u;
    }

    for (i = 0; i < ancount; i++) {
        uint32_t rdata_len;
        uint16_t rr_type;
        uint16_t rr_class;
        uint32_t ttl;

        if (!dns_skip_name_local(dns, dns_size, offset, &offset) || offset + 10u > dns_size) {
            write_str("dns: malformed answer\n");
            return printed;
        }
        rr_type = read_be16_local(dns + offset);
        rr_class = read_be16_local(dns + offset + 2u);
        ttl = read_be32_local(dns + offset + 4u);
        rdata_len = (uint32_t)read_be16_local(dns + offset + 8u);
        offset += 10u;
        if (offset + rdata_len > dns_size) {
            write_str("dns: truncated rdata\n");
            return printed;
        }

        if (rr_class == 1u && dns_answer_matches_qtype_local(qtype, rr_type)) {
            if (rr_type == DNS_TYPE_A && rdata_len == 4u) {
                write_str("A ");
                write_ipv4_local(dns + offset);
                write_str(" ttl=");
                write_dec(ttl);
                write_str("\n");
                printed = 1;
            } else if (rr_type == DNS_TYPE_AAAA && rdata_len == 16u) {
                write_str("AAAA ");
                write_ipv6_local(dns + offset);
                write_str(" ttl=");
                write_dec(ttl);
                write_str("\n");
                printed = 1;
            } else if (rr_type == DNS_TYPE_CNAME) {
                char cname[256];

                if (dns_read_name_local(dns, dns_size, offset, cname, sizeof(cname), 0)) {
                    write_str("CNAME ");
                    write_str(cname);
                    write_str(" ttl=");
                    write_dec(ttl);
                    write_str("\n");
                    printed = 1;
                }
            } else if (rr_type == DNS_TYPE_MX && rdata_len >= 3u) {
                char exchange[256];
                uint16_t preference = read_be16_local(dns + offset);

                if (dns_read_name_local(dns, dns_size, offset + 2u, exchange, sizeof(exchange), 0)) {
                    write_str("MX ");
                    write_dec(preference);
                    write_str(" ");
                    write_str(exchange);
                    write_str(" ttl=");
                    write_dec(ttl);
                    write_str("\n");
                    printed = 1;
                }
            }
        }
        offset += rdata_len;
    }
    if (!printed) {
        write_str("dns: no matching ");
        write_str(dns_type_name_local(qtype));
        write_str(" records\n");
    }
    return printed;
}







static const char *dns_type_name_local(uint16_t type) {
    switch (type) {
        case DNS_TYPE_A: return "A";
        case DNS_TYPE_AAAA: return "AAAA";
        case DNS_TYPE_CNAME: return "CNAME";
        case DNS_TYPE_MX: return "MX";
        case DNS_TYPE_ANY: return "ANY";
        default: return "UNKNOWN";
    }
}

static int dns_parse_qtype_local(const char *text, uint16_t *out_type) {
    if (text == 0 || out_type == 0) {
        return 0;
    }
    if (streq_ignore_case_local(text, "A")) {
        *out_type = DNS_TYPE_A;
        return 1;
    }
    if (streq_ignore_case_local(text, "AAAA")) {
        *out_type = DNS_TYPE_AAAA;
        return 1;
    }
    if (streq_ignore_case_local(text, "CNAME")) {
        *out_type = DNS_TYPE_CNAME;
        return 1;
    }
    if (streq_ignore_case_local(text, "MX")) {
        *out_type = DNS_TYPE_MX;
        return 1;
    }
    if (streq_ignore_case_local(text, "ANY")) {
        *out_type = DNS_TYPE_ANY;
        return 1;
    }
    return 0;
}

static void write_ipv6_local(const uint8_t *addr) {
    uint32_t i;

    for (i = 0u; i < 8u; i++) {
        if (i != 0u) {
            write_str(":");
        }
        write_hex_byte_local(addr[i * 2u]);
        write_hex_byte_local(addr[i * 2u + 1u]);
    }
}

static int dns_answer_matches_qtype_local(uint16_t qtype, uint16_t rr_type) {
    if (qtype == DNS_TYPE_ANY) {
        return rr_type == DNS_TYPE_A ||
               rr_type == DNS_TYPE_AAAA ||
               rr_type == DNS_TYPE_CNAME ||
               rr_type == DNS_TYPE_MX;
    }
    if (rr_type == DNS_TYPE_CNAME) {
        return 1;
    }
    return rr_type == qtype;
}

int dns_lookup_ipv4_local(const struct syscall_rtl8139_info *nic,
                                 const uint8_t src_ip[4],
                                 const uint8_t dns_ip[4],
                                 const char *name,
                                 uint8_t out_ip[4]) {
    uint8_t arp_ip[4];
    uint8_t gateway_ip[4] = {10u, 0u, 2u, 2u};
    uint8_t target_mac[6];
    uint16_t dns_id;
    uint16_t src_port;
    uint32_t frame_bytes;
    uint32_t ip_offset = 14u;
    uint32_t ihl_bytes;
    uint32_t udp_offset;
    uint32_t udp_bytes;

    if (nic == 0 || name == 0 || out_ip == 0) {
        return 0;
    }
    if (same_subnet24_local(src_ip, dns_ip)) {
        arp_ip[0] = dns_ip[0];
        arp_ip[1] = dns_ip[1];
        arp_ip[2] = dns_ip[2];
        arp_ip[3] = dns_ip[3];
    } else {
        arp_ip[0] = gateway_ip[0];
        arp_ip[1] = gateway_ip[1];
        arp_ip[2] = gateway_ip[2];
        arp_ip[3] = gateway_ip[3];
    }

    rtl8139_drain_rx_local(32u);
    build_arp_request_local(g_arp_frame_local, nic->mac, src_ip, arp_ip);
    if (rtl8139_tx_send(g_arp_frame_local, sizeof(g_arp_frame_local)) <= 0) {
        return 0;
    }
    if (!rtl8139_wait_arp_reply_local(arp_ip, target_mac, 100u)) {
        return 0;
    }
    rtl8139_drain_rx_local(8u);

    dns_id = (uint16_t)(0x4800u ^ (uint16_t)ticks());
    src_port = (uint16_t)(49152u + (ticks() & 0x0fffu));
    frame_bytes = build_dns_query_local(g_dns_frame_local,
                                        nic->mac,
                                        target_mac,
                                        src_ip,
                                        dns_ip,
                                        name,
                                        DNS_TYPE_A,
                                        dns_id,
                                        src_port);
    if (frame_bytes == 0u || rtl8139_tx_send(g_dns_frame_local, frame_bytes) <= 0) {
        return 0;
    }
    if (!rtl8139_wait_dns_reply_local(src_ip, dns_ip, src_port, dns_id, 200u, &g_net_rx_info_local, 0)) {
        return 0;
    }
    ihl_bytes = (uint32_t)(g_net_rx_info_local.data[ip_offset] & 0x0fu) * 4u;
    udp_offset = ip_offset + ihl_bytes;
    udp_bytes = (uint32_t)read_be16_local(g_net_rx_info_local.data + udp_offset + 4u);
    if (udp_bytes < 20u || udp_offset + udp_bytes > g_net_rx_info_local.bytes_copied) {
        return 0;
    }
    return dns_extract_first_a_local(g_net_rx_info_local.data + udp_offset + 8u, udp_bytes - 8u, out_ip);
}

int cmd_dns(int argc, char **argv) {
    struct syscall_rtl8139_info nic;
    struct dns_wait_debug_local wait_debug;
    uint8_t src_ip[4];
    uint8_t gateway_ip[4];
    uint8_t server_ip[4];
    const uint8_t *arp_ip = server_ip;
    uint8_t target_mac[6];
    const char *name = "example.com";
    uint16_t dns_id;
    uint16_t src_port;
    uint32_t frame_bytes;
    uint32_t ip_offset = 14u;
    uint32_t ihl_bytes;
    uint32_t udp_offset;
    uint32_t ip_bytes;
    uint32_t udp_bytes;
    uint32_t dns_bytes;
    uint32_t stale_packets;
    uint16_t qtype = DNS_TYPE_A;
    int argi = 1;

    if (argc > 5) {
        write_err_usage("dns", " [-t A|AAAA|CNAME|MX|ANY] [server-ip] <name>\n");
        return 1;
    }
    if (argi + 1 < argc && streq_local(argv[argi], "-t")) {
        if (argi + 2 >= argc || !dns_parse_qtype_local(argv[argi + 1], &qtype)) {
            write_err_usage("dns", " [-t A|AAAA|CNAME|MX|ANY] [server-ip] <name>\n");
            return 1;
        }
        argi += 2;
    }
    ipv4_copy_runtime_config_local(src_ip, 0, gateway_ip, server_ip);
    if (argc - argi == 1) {
        name = argv[argi];
    } else if (argc - argi == 2) {
        if (!parse_ipv4_local(argv[argi], server_ip)) {
            write_err_usage("dns", " [-t A|AAAA|CNAME|MX|ANY] [server-ip] <name>\n");
            return 1;
        }
        name = argv[argi + 1];
    } else if (argc - argi != 0) {
        write_err_usage("dns", " [-t A|AAAA|CNAME|MX|ANY] [server-ip] <name>\n");
        return 1;
    }
    if (name == 0 || name[0] == '\0') {
        write_err_usage("dns", " [-t A|AAAA|CNAME|MX|ANY] [server-ip] <name>\n");
        return 1;
    }
    if (rtl8139_query(&nic) <= 0 || !nic.present || !nic.initialized || !nic.link_up) {
        write_str("dns: rtl8139 not ready\n");
        return 1;
    }
    dns_id = (uint16_t)(0x4e58u ^ (uint16_t)ticks());
    src_port = (uint16_t)(49152u + (ticks() & 0x0fffu));

    write_str("DNS ");
    write_ipv4_local(server_ip);
    write_str(" query ");
    write_str(dns_type_name_local(qtype));
    write_str(" ");
    write_str(name);
    write_str("\n");

    stale_packets = rtl8139_drain_rx_local(32u);
    if (stale_packets != 0u) {
        write_str("dns: drained stale packets=");
        write_dec(stale_packets);
        write_str("\n");
    }

    if (!same_subnet24_local(src_ip, server_ip)) {
        arp_ip = gateway_ip;
    }

    build_arp_request_local(g_arp_frame_local, nic.mac, src_ip, arp_ip);
    if (rtl8139_tx_send(g_arp_frame_local, sizeof(g_arp_frame_local)) <= 0) {
        write_str("dns: arp request send failed\n");
        return 1;
    }
    if (!rtl8139_wait_arp_reply_local(arp_ip, target_mac, 100u)) {
        write_str("dns: arp timeout\n");
        return 1;
    }

    write_str("arp: ");
    write_ipv4_local(arp_ip);
    write_str(" is-at ");
    write_mac_local(target_mac);
    write_str("\n");

    stale_packets = rtl8139_drain_rx_local(8u);
    if (stale_packets != 0u) {
        write_str("dns: drained post-arp packets=");
        write_dec(stale_packets);
        write_str("\n");
    }

    frame_bytes =
        build_dns_query_local(g_dns_frame_local, nic.mac, target_mac, src_ip, server_ip, name, qtype, dns_id, src_port);
    if (frame_bytes == 0u) {
        write_str("dns: invalid query name\n");
        return 1;
    }
    if (rtl8139_tx_send(g_dns_frame_local, frame_bytes) <= 0) {
        write_str("dns: udp send failed\n");
        return 1;
    }
    write_str("dns: udp sent bytes=");
    write_dec(frame_bytes);
    write_str(" sport=");
    write_dec(src_port);
    write_str(" id=");
    write_hex_u32(dns_id);
    write_str("\n");
    write_str("dns: waiting for reply\n");
    if (!rtl8139_wait_dns_reply_local(src_ip, server_ip, src_port, dns_id, 200u, &g_net_rx_info_local, &wait_debug)) {
        write_str("dns: reply timeout");
        if (wait_debug.saw_packet) {
            write_str(" last ether=");
            write_hex_u32(wait_debug.ether_type);
            if (wait_debug.ether_type == 0x0800u) {
                write_str(" proto=");
                write_dec(wait_debug.ip_proto);
                write_str(" src=");
                write_ipv4_local(wait_debug.src_ip);
                write_str(" dst=");
                write_ipv4_local(wait_debug.dst_ip);
                if (wait_debug.ip_proto == 17u) {
                    write_str(" sport=");
                    write_dec(wait_debug.udp_src_port);
                    write_str(" dport=");
                    write_dec(wait_debug.udp_dst_port);
                    write_str(" dnsid=");
                    write_hex_u32(wait_debug.dns_id);
                } else if (wait_debug.ip_proto == 1u) {
                    write_str(" icmp=");
                    write_dec(wait_debug.icmp_type);
                    write_str("/");
                    write_dec(wait_debug.icmp_code);
                }
            }
        }
        write_str("\n");
        return 1;
    }
    write_str("dns: wait returned copied=");
    write_dec(g_net_rx_info_local.bytes_copied);
    write_str(" pkt=");
    write_dec(g_net_rx_info_local.packet_length);
    write_str("\n");

    ihl_bytes = (uint32_t)(g_net_rx_info_local.data[ip_offset] & 0x0fu) * 4u;
    udp_offset = ip_offset + ihl_bytes;
    ip_bytes = (uint32_t)read_be16_local(g_net_rx_info_local.data + ip_offset + 2u);
    if (ihl_bytes < 20u || ip_bytes < ihl_bytes + 8u || g_net_rx_info_local.bytes_copied < ip_offset + ip_bytes) {
        write_str("dns: short ipv4 reply\n");
        return 1;
    }
    udp_bytes = (uint32_t)read_be16_local(g_net_rx_info_local.data + udp_offset + 4u);
    if (udp_bytes < 20u || g_net_rx_info_local.bytes_copied < udp_offset + udp_bytes) {
        write_str("dns: short udp reply\n");
        return 1;
    }
    dns_bytes = udp_bytes - 8u;
    write_str("dns: parse ip=");
    write_dec(ip_bytes);
    write_str(" udp=");
    write_dec(udp_bytes);
    write_str(" dns=");
    write_dec(dns_bytes);
    write_str("\n");
    if (!dns_print_answers_local(g_net_rx_info_local.data + udp_offset + 8u, dns_bytes, qtype)) {
        return 1;
    }
    return 0;
}

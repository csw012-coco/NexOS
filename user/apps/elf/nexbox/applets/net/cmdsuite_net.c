#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static struct syscall_rtl8139_rx_info g_net_rx_info_local;
static uint8_t g_arp_frame_local[60];
static uint8_t g_dns_frame_local[512];
static uint8_t g_dhcp_frame_local[512];
static uint8_t g_dns_name_local[256];
static uint8_t g_http_frame_local[1600];
static char g_tcp_payload_local[512];
static char g_http_host_local[128];
static char g_http_path_local[256];
static char g_http_request_local[512];
static uint32_t g_http_client_nonce_local;
static struct ipv4_runtime_config_local {
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint8_t server[4];
    uint32_t lease_time;
    uint8_t got_mask;
    uint8_t got_gateway;
    uint8_t got_dns;
    uint8_t got_server;
    uint8_t got_lease_time;
    uint8_t from_dhcp;
} g_ipv4_config_local = {
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
};

struct arp_cache_entry_local {
    uint8_t ip[4];
    uint8_t mac[6];
    uint32_t tick;
    uint8_t valid;
};

static struct arp_cache_entry_local g_arp_cache_local[8];

struct http_result_local {
    uint32_t status_code;
    uint32_t header_parsed;
    uint32_t location_present;
    uint32_t header_len;
    uint32_t content_length_present;
    uint32_t content_length;
    uint32_t transfer_chunked;
    char location[256];
    uint8_t header_buf[1024];
};

struct http_body_state_local {
    uint32_t content_length_present;
    uint32_t content_length;
    uint32_t received;
    uint32_t transfer_chunked;
    uint32_t chunk_bytes_remaining;
    uint32_t chunk_post_bytes_remaining;
    uint32_t chunk_header_len;
    uint8_t done;
    char chunk_header[32];
};

struct tcp_session_local {
    struct syscall_rtl8139_info nic;
    uint8_t src_ip[4];
    uint8_t target_ip[4];
    uint8_t target_mac[6];
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t client_seq;
    uint32_t server_seq;
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

static void write_hex_byte_local(uint8_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char text[2];

    text[0] = hex[(value >> 4) & 0x0fu];
    text[1] = hex[value & 0x0fu];
    write_stdout(text, sizeof(text));
}

static uint16_t read_be16_local(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

static uint32_t read_be32_local(const uint8_t *src) {
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static int parse_ipv4_local(const char *text, uint8_t out[4]) {
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

static void write_mac_local(const uint8_t *mac) {
    uint32_t i;

    for (i = 0; i < 6u; i++) {
        if (i != 0u) {
            write_str(":");
        }
        write_hex_byte_local(mac[i]);
    }
}

static void write_ipv4_local(const uint8_t *addr) {
    write_dec(addr[0]);
    write_str(".");
    write_dec(addr[1]);
    write_str(".");
    write_dec(addr[2]);
    write_str(".");
    write_dec(addr[3]);
}

static void write_u16_be_local(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xffu);
}

static void write_u32_be_local(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static uint16_t checksum16_local(const uint8_t *data, uint32_t bytes) {
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

static uint16_t udp_checksum_ipv4_local(const uint8_t src_ip[4],
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

static uint16_t tcp_checksum_ipv4_local(const uint8_t src_ip[4],
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

static void build_arp_request_local(uint8_t *frame,
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

static int same_subnet24_local(const uint8_t a[4], const uint8_t b[4]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

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

struct dhcp_config_local {
    uint8_t yiaddr[4];
    uint8_t server_id[4];
    uint8_t subnet_mask[4];
    uint8_t router[4];
    uint8_t dns[4];
    uint32_t lease_time;
    uint8_t message_type;
    uint8_t got_server_id;
    uint8_t got_subnet_mask;
    uint8_t got_router;
    uint8_t got_dns;
    uint8_t got_lease_time;
};

static void ipv4_copy_runtime_config_local(uint8_t ip[4],
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

static void ipv4_apply_dhcp_config_local(const struct dhcp_config_local *config) {
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

struct tcp_packet_local {
    uint32_t seq;
    uint32_t ack;
    uint32_t payload_offset;
    uint32_t payload_length;
    uint16_t flags;
    uint16_t window;
};

enum {
    TCP_FLAG_FIN = 0x0001u,
    TCP_FLAG_SYN = 0x0002u,
    TCP_FLAG_RST = 0x0004u,
    TCP_FLAG_PSH = 0x0008u,
    TCP_FLAG_ACK = 0x0010u
};

enum {
    DHCP_OP_BOOTREQUEST = 1u,
    DHCP_HTYPE_ETHERNET = 1u,
    DHCP_HLEN_ETHERNET = 6u,
    DHCP_FLAGS_BROADCAST = 0x8000u,
    DHCP_OPTION_SUBNET_MASK = 1u,
    DHCP_OPTION_ROUTER = 3u,
    DHCP_OPTION_DNS = 6u,
    DHCP_OPTION_REQUESTED_IP = 50u,
    DHCP_OPTION_LEASE_TIME = 51u,
    DHCP_OPTION_MESSAGE_TYPE = 53u,
    DHCP_OPTION_SERVER_ID = 54u,
    DHCP_OPTION_PARAM_REQUEST_LIST = 55u,
    DHCP_OPTION_MAX_MESSAGE_SIZE = 57u,
    DHCP_OPTION_CLIENT_ID = 61u,
    DHCP_OPTION_END = 255u,
    DHCP_MSG_DISCOVER = 1u,
    DHCP_MSG_OFFER = 2u,
    DHCP_MSG_REQUEST = 3u,
    DHCP_MSG_ACK = 5u,
    DHCP_MSG_NAK = 6u
};

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

static uint32_t build_dhcp_packet_local(uint8_t *frame,
                                        const uint8_t src_mac[6],
                                        uint32_t xid,
                                        uint8_t message_type,
                                        const uint8_t requested_ip[4],
                                        const uint8_t server_id[4]) {
    uint8_t *ip = frame + 14u;
    uint8_t *udp = ip + 20u;
    uint8_t *bootp = udp + 8u;
    uint8_t *opt = bootp + 240u;
    uint16_t udp_length;
    uint16_t total_length;
    uint32_t i;

    for (i = 0; i < 14u + 20u + 8u + 300u; i++) {
        frame[i] = 0u;
    }
    for (i = 0; i < 6u; i++) {
        frame[i] = 0xffu;
        frame[6u + i] = src_mac[i];
    }
    frame[12] = 0x08u;
    frame[13] = 0x00u;

    bootp[0] = DHCP_OP_BOOTREQUEST;
    bootp[1] = DHCP_HTYPE_ETHERNET;
    bootp[2] = DHCP_HLEN_ETHERNET;
    bootp[3] = 0u;
    write_u32_be_local(bootp + 4u, xid);
    write_u16_be_local(bootp + 8u, 0u);
    write_u16_be_local(bootp + 10u, DHCP_FLAGS_BROADCAST);
    for (i = 0; i < 6u; i++) {
        bootp[28u + i] = src_mac[i];
    }
    bootp[236] = 99u;
    bootp[237] = 130u;
    bootp[238] = 83u;
    bootp[239] = 99u;

    *opt++ = DHCP_OPTION_MESSAGE_TYPE;
    *opt++ = 1u;
    *opt++ = message_type;

    *opt++ = DHCP_OPTION_CLIENT_ID;
    *opt++ = 7u;
    *opt++ = 1u;
    for (i = 0; i < 6u; i++) {
        *opt++ = src_mac[i];
    }

    *opt++ = DHCP_OPTION_MAX_MESSAGE_SIZE;
    *opt++ = 2u;
    write_u16_be_local(opt, 1024u);
    opt += 2u;

    *opt++ = DHCP_OPTION_PARAM_REQUEST_LIST;
    *opt++ = 4u;
    *opt++ = DHCP_OPTION_SUBNET_MASK;
    *opt++ = DHCP_OPTION_ROUTER;
    *opt++ = DHCP_OPTION_DNS;
    *opt++ = DHCP_OPTION_LEASE_TIME;

    if (message_type == DHCP_MSG_REQUEST && requested_ip != 0 && server_id != 0) {
        *opt++ = DHCP_OPTION_REQUESTED_IP;
        *opt++ = 4u;
        for (i = 0; i < 4u; i++) {
            *opt++ = requested_ip[i];
        }
        *opt++ = DHCP_OPTION_SERVER_ID;
        *opt++ = 4u;
        for (i = 0; i < 4u; i++) {
            *opt++ = server_id[i];
        }
    }

    *opt++ = DHCP_OPTION_END;

    udp_length = (uint16_t)(8u + (uint32_t)(opt - bootp));
    total_length = (uint16_t)(20u + udp_length);

    ip[0] = 0x45u;
    ip[1] = 0x00u;
    write_u16_be_local(ip + 2u, total_length);
    write_u16_be_local(ip + 4u, (uint16_t)xid);
    write_u16_be_local(ip + 6u, 0u);
    ip[8] = 64u;
    ip[9] = 17u;
    ip[10] = 0u;
    ip[11] = 0u;
    ip[16] = 255u;
    ip[17] = 255u;
    ip[18] = 255u;
    ip[19] = 255u;
    write_u16_be_local(ip + 10u, checksum16_local(ip, 20u));

    write_u16_be_local(udp + 0u, 68u);
    write_u16_be_local(udp + 2u, 67u);
    write_u16_be_local(udp + 4u, udp_length);
    write_u16_be_local(udp + 6u, 0u);
    write_u16_be_local(udp + 6u, udp_checksum_ipv4_local(ip + 12u, ip + 16u, udp, udp_length));

    return 14u + (uint32_t)total_length;
}

static uint32_t build_tcp_ipv4_local(uint8_t *frame,
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

static int dhcp_parse_reply_local(const struct syscall_rtl8139_rx_info *info,
                                  uint32_t xid,
                                  struct dhcp_config_local *out) {
    uint32_t ip_offset = 14u;
    uint32_t ihl_bytes;
    uint32_t udp_offset;
    uint32_t bootp_offset;
    uint32_t dhcp_size;
    uint32_t cursor;
    uint32_t i;

    if (info == 0 || out == 0 || info->bytes_copied < 14u + 20u + 8u + 240u) {
        return 0;
    }
    if (read_be16_local(info->data + 12u) != 0x0800u) {
        return 0;
    }
    ihl_bytes = (uint32_t)(info->data[ip_offset] & 0x0fu) * 4u;
    udp_offset = ip_offset + ihl_bytes;
    bootp_offset = udp_offset + 8u;
    if (ihl_bytes < 20u || bootp_offset + 240u > info->bytes_copied) {
        return 0;
    }
    if (info->data[ip_offset + 9u] != 17u ||
        read_be16_local(info->data + udp_offset + 0u) != 67u ||
        read_be16_local(info->data + udp_offset + 2u) != 68u) {
        return 0;
    }
    if (read_be32_local(info->data + bootp_offset + 4u) != xid) {
        return 0;
    }
    if (info->data[bootp_offset + 236u] != 99u ||
        info->data[bootp_offset + 237u] != 130u ||
        info->data[bootp_offset + 238u] != 83u ||
        info->data[bootp_offset + 239u] != 99u) {
        return 0;
    }

    out->message_type = 0u;
    out->got_server_id = 0u;
    out->got_subnet_mask = 0u;
    out->got_router = 0u;
    out->got_dns = 0u;
    out->got_lease_time = 0u;
    out->lease_time = 0u;
    for (i = 0; i < 4u; i++) {
        out->yiaddr[i] = info->data[bootp_offset + 16u + i];
        out->server_id[i] = 0u;
        out->subnet_mask[i] = 0u;
        out->router[i] = 0u;
        out->dns[i] = 0u;
    }

    dhcp_size = (uint32_t)read_be16_local(info->data + udp_offset + 4u);
    if (dhcp_size < 8u + 240u) {
        return 0;
    }
    cursor = bootp_offset + 240u;
    while (cursor < info->bytes_copied) {
        uint8_t code = info->data[cursor++];
        uint8_t len;

        if (code == 0u) {
            continue;
        }
        if (code == DHCP_OPTION_END) {
            break;
        }
        if (cursor >= info->bytes_copied) {
            break;
        }
        len = info->data[cursor++];
        if (cursor + len > info->bytes_copied) {
            break;
        }
        if (code == DHCP_OPTION_MESSAGE_TYPE && len >= 1u) {
            out->message_type = info->data[cursor];
        } else if (code == DHCP_OPTION_SERVER_ID && len >= 4u) {
            for (i = 0; i < 4u; i++) {
                out->server_id[i] = info->data[cursor + i];
            }
            out->got_server_id = 1u;
        } else if (code == DHCP_OPTION_SUBNET_MASK && len >= 4u) {
            for (i = 0; i < 4u; i++) {
                out->subnet_mask[i] = info->data[cursor + i];
            }
            out->got_subnet_mask = 1u;
        } else if (code == DHCP_OPTION_ROUTER && len >= 4u) {
            for (i = 0; i < 4u; i++) {
                out->router[i] = info->data[cursor + i];
            }
            out->got_router = 1u;
        } else if (code == DHCP_OPTION_DNS && len >= 4u) {
            for (i = 0; i < 4u; i++) {
                out->dns[i] = info->data[cursor + i];
            }
            out->got_dns = 1u;
        } else if (code == DHCP_OPTION_LEASE_TIME && len >= 4u) {
            out->lease_time = read_be32_local(info->data + cursor);
            out->got_lease_time = 1u;
        }
        cursor += len;
    }

    if (!out->got_server_id) {
        for (i = 0; i < 4u; i++) {
            out->server_id[i] = info->data[ip_offset + 12u + i];
        }
    }
    return out->message_type != 0u;
}

static int rtl8139_wait_dhcp_reply_local(uint32_t xid,
                                         uint8_t expect_type,
                                         uint32_t timeout_ticks,
                                         struct dhcp_config_local *config_out) {
    struct dhcp_config_local parsed;
    uint32_t start = ticks();

    while ((uint32_t)(ticks() - start) < timeout_ticks) {
        if (rtl8139_rx_dump(&g_net_rx_info_local) > 0) {
            if (!dhcp_parse_reply_local(&g_net_rx_info_local, xid, &parsed)) {
                yield();
                continue;
            }
            if (parsed.message_type == expect_type || parsed.message_type == DHCP_MSG_NAK) {
                if (config_out != 0) {
                    *config_out = parsed;
                }
                return 1;
            }
        }
        yield();
    }
    return 0;
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

static int rtl8139_wait_arp_reply_local(const uint8_t target_ip[4],
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

static int rtl8139_wait_tcp_packet_local(const uint8_t src_ip[4],
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

static uint32_t rtl8139_drain_rx_local(uint32_t max_packets) {
    uint32_t count = 0;

    while (count < max_packets && rtl8139_rx_dump(&g_net_rx_info_local) > 0) {
        count++;
    }
    return count;
}

static const char *ipv4_protocol_name_local(uint8_t protocol) {
    switch (protocol) {
        case 1: return "ICMP";
        case 6: return "TCP";
        case 17: return "UDP";
        default: return "unknown";
    }
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

static int dns_lookup_ipv4_local(const struct syscall_rtl8139_info *nic,
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

static int http_split_target_local(const char *arg,
                                   char *host_out,
                                   uint32_t host_size,
                                   char *path_out,
                                   uint32_t path_size) {
    const char *cursor = arg;
    const char *slash = 0;
    uint32_t host_len;
    uint32_t i;

    if (arg == 0 || host_out == 0 || path_out == 0 || host_size < 2u || path_size < 2u) {
        return 0;
    }
    if (strncmp(cursor, "http://", 7u) == 0) {
        cursor += 7;
    }
    for (i = 0; cursor[i] != '\0'; i++) {
        if (cursor[i] == '/') {
            slash = cursor + i;
            break;
        }
    }
    host_len = slash != 0 ? (uint32_t)(slash - cursor) : str_len_local(cursor);
    if (host_len == 0u || host_len >= host_size) {
        return 0;
    }
    for (i = 0; i < host_len; i++) {
        host_out[i] = cursor[i];
    }
    host_out[host_len] = '\0';
    if (slash == 0) {
        path_out[0] = '/';
        path_out[1] = '\0';
        return 1;
    }
    copy_line_local(path_out, slash, path_size);
    return path_out[0] == '/';
}

static int http_write_local(int fd, const uint8_t *data, uint32_t bytes) {
    uint32_t total = 0u;

    if (bytes == 0u) {
        return 1;
    }
    if (fd >= 0) {
        while (total < bytes) {
            int rc = write(fd, data + total, bytes - total);

            if (rc <= 0) {
                return 0;
            }
            total += (uint32_t)rc;
        }
        return 1;
    }
    return (uint32_t)write_stdout(data, bytes) == bytes;
}

static int http_header_name_eq_local(const char *line, const char *name) {
    uint32_t i = 0u;

    while (name[i] != '\0') {
        char a = line[i];
        char b = name[i];

        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
        i++;
    }
    return line[i] == ':';
}

static int http_contains_token_ignore_case_local(const char *text, const char *token) {
    uint32_t i;
    uint32_t token_len;

    if (text == 0 || token == 0) {
        return 0;
    }
    token_len = str_len_local(token);
    if (token_len == 0u) {
        return 0;
    }
    for (i = 0u; text[i] != '\0'; i++) {
        uint32_t j = 0u;

        while (text[i + j] != '\0' && token[j] != '\0') {
            char a = text[i + j];
            char b = token[j];

            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            j++;
        }
        if (j == token_len) {
            return 1;
        }
    }
    return 0;
}

static uint32_t http_parse_chunk_size_local(const char *line, int *ok_out) {
    uint32_t value = 0u;
    uint32_t digits = 0u;
    uint32_t i;

    if (ok_out != 0) {
        *ok_out = 0;
    }
    if (line == 0) {
        return 0u;
    }
    for (i = 0u; line[i] != '\0'; i++) {
        char ch = line[i];
        uint32_t nibble;

        if (ch == ';' || ch == ' ' || ch == '\t') {
            break;
        }
        if (ch >= '0' && ch <= '9') {
            nibble = (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            nibble = (uint32_t)(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            nibble = (uint32_t)(ch - 'A' + 10);
        } else {
            return 0u;
        }
        value = (value << 4) | nibble;
        digits++;
    }
    if (digits == 0u) {
        return 0u;
    }
    if (ok_out != 0) {
        *ok_out = 1;
    }
    return value;
}

static void http_body_state_init_local(struct http_body_state_local *state,
                                       const struct http_result_local *result) {
    if (state == 0) {
        return;
    }
    state->content_length_present = result != 0 ? result->content_length_present : 0u;
    state->content_length = result != 0 ? result->content_length : 0u;
    state->received = 0u;
    state->transfer_chunked = result != 0 ? result->transfer_chunked : 0u;
    state->chunk_bytes_remaining = 0u;
    state->chunk_post_bytes_remaining = 0u;
    state->chunk_header_len = 0u;
    state->done = 0u;
    state->chunk_header[0] = '\0';
}

static int http_body_complete_local(const struct http_body_state_local *state) {
    if (state == 0) {
        return 0;
    }
    if (state->done) {
        return 1;
    }
    return state->content_length_present && state->received >= state->content_length;
}

static int http_consume_body_raw_local(int fd,
                                       const uint8_t *data,
                                       uint32_t bytes,
                                       struct http_body_state_local *state,
                                       uint32_t *written_out) {
    uint32_t body_bytes = bytes;

    if (state != 0 && state->content_length_present) {
        uint32_t remaining =
            state->received < state->content_length ? (state->content_length - state->received) : 0u;

        if (body_bytes > remaining) {
            body_bytes = remaining;
        }
    }
    if (body_bytes != 0u && !http_write_local(fd, data, body_bytes)) {
        return 0;
    }
    if (state != 0) {
        state->received += body_bytes;
    }
    if (written_out != 0) {
        *written_out += body_bytes;
    }
    return 1;
}

static int http_consume_body_chunked_local(int fd,
                                           const uint8_t *data,
                                           uint32_t bytes,
                                           struct http_body_state_local *state,
                                           uint32_t *written_out) {
    uint32_t pos = 0u;

    if (state == 0) {
        return 0;
    }
    while (pos < bytes && !state->done) {
        if (state->chunk_post_bytes_remaining != 0u) {
            uint32_t drain = bytes - pos;

            if (drain > state->chunk_post_bytes_remaining) {
                drain = state->chunk_post_bytes_remaining;
            }
            pos += drain;
            state->chunk_post_bytes_remaining -= drain;
            continue;
        }
        if (state->chunk_bytes_remaining == 0u) {
            while (pos < bytes) {
                uint8_t ch = data[pos++];

                if (ch == '\r') {
                    continue;
                }
                if (ch == '\n') {
                    int ok = 0;
                    uint32_t chunk_size;

                    state->chunk_header[state->chunk_header_len] = '\0';
                    chunk_size = http_parse_chunk_size_local(state->chunk_header, &ok);
                    state->chunk_header_len = 0u;
                    if (!ok) {
                        return 0;
                    }
                    if (chunk_size == 0u) {
                        state->done = 1u;
                        break;
                    }
                    state->chunk_bytes_remaining = chunk_size;
                    break;
                }
                if (state->chunk_header_len + 1u >= sizeof(state->chunk_header)) {
                    return 0;
                }
                state->chunk_header[state->chunk_header_len++] = (char)ch;
            }
            continue;
        }
        {
            uint32_t take = bytes - pos;

            if (take > state->chunk_bytes_remaining) {
                take = state->chunk_bytes_remaining;
            }
            if (take != 0u && !http_write_local(fd, data + pos, take)) {
                return 0;
            }
            pos += take;
            state->chunk_bytes_remaining -= take;
            state->received += take;
            if (written_out != 0) {
                *written_out += take;
            }
            if (state->chunk_bytes_remaining == 0u) {
                state->chunk_post_bytes_remaining = 2u;
            }
        }
    }
    return 1;
}

static void http_parse_headers_local(struct http_result_local *result) {
    uint32_t offset = 0u;

    if (result == 0 || result->header_parsed) {
        return;
    }
    result->header_parsed = 1u;
    result->location_present = 0u;
    result->location[0] = '\0';
    result->status_code = 0u;
    result->content_length_present = 0u;
    result->content_length = 0u;
    result->transfer_chunked = 0u;

    while (offset < result->header_len) {
        char line[256];
        uint32_t line_len = 0u;
        uint32_t i;

        while (offset < result->header_len &&
               result->header_buf[offset] != '\n' &&
               line_len + 1u < sizeof(line)) {
            char ch = (char)result->header_buf[offset++];

            if (ch != '\r') {
                line[line_len++] = ch;
            }
        }
        while (offset < result->header_len && result->header_buf[offset] != '\n') {
            offset++;
        }
        if (offset < result->header_len && result->header_buf[offset] == '\n') {
            offset++;
        }
        line[line_len] = '\0';
        if (line_len == 0u) {
            break;
        }
        if (result->status_code == 0u && starts_with(line, "HTTP/")) {
            for (i = 0u; line[i] != '\0'; i++) {
                if (line[i] == ' ' && line[i + 1u] >= '0' && line[i + 1u] <= '9') {
                    result->status_code = (uint32_t)strtoul(line + i + 1u, 0, 10);
                    break;
                }
            }
            continue;
        }
        if (!result->location_present && http_header_name_eq_local(line, "Location")) {
            const char *value = line + 9u;

            while (*value == ' ' || *value == '\t') {
                value++;
            }
            copy_line_local(result->location, value, sizeof(result->location));
            result->location_present = result->location[0] != '\0';
            continue;
        }
        if (!result->content_length_present && http_header_name_eq_local(line, "Content-Length")) {
            const char *value = line + 15u;

            while (*value == ' ' || *value == '\t') {
                value++;
            }
            result->content_length = (uint32_t)strtoul(value, 0, 10);
            result->content_length_present = 1u;
            continue;
        }
        if (!result->transfer_chunked && http_header_name_eq_local(line, "Transfer-Encoding")) {
            const char *value = line + 18u;

            while (*value == ' ' || *value == '\t') {
                value++;
            }
            result->transfer_chunked = http_contains_token_ignore_case_local(value, "chunked") ? 1u : 0u;
        }
    }
}

static int http_apply_redirect_local(const char *location,
                                     const char *current_path,
                                     char *host_io,
                                     uint32_t host_size,
                                     char *path_io,
                                     uint32_t path_size) {
    uint32_t i;
    uint32_t slash = 0u;

    if (location == 0 || host_io == 0 || path_io == 0) {
        return 0;
    }
    if (starts_with(location, "http://")) {
        return http_split_target_local(location, host_io, host_size, path_io, path_size);
    }
    if (starts_with(location, "//")) {
        char url[384];
        int rc;

        rc = snprintf(url, sizeof(url), "http:%s", location);
        if (rc < 0 || (uint32_t)rc >= sizeof(url)) {
            return 0;
        }
        return http_split_target_local(url, host_io, host_size, path_io, path_size);
    }
    if (location[0] == '/') {
        copy_line_local(path_io, location, path_size);
        return path_io[0] == '/';
    }
    if (current_path == 0 || current_path[0] != '/') {
        return 0;
    }
    for (i = 0u; current_path[i] != '\0'; i++) {
        if (current_path[i] == '/') {
            slash = i;
        }
    }
    if (slash + 1u >= path_size) {
        return 0;
    }
    for (i = 0u; i <= slash; i++) {
        path_io[i] = current_path[i];
    }
    path_io[slash + 1u] = '\0';
    copy_line_local(path_io + slash + 1u, location, path_size - slash - 1u);
    return path_io[0] == '/';
}

static uint16_t http_next_src_port_local(void) {
    g_http_client_nonce_local++;
    return (uint16_t)(40000u + ((ticks() + g_http_client_nonce_local * 257u) & 0x1fffu));
}

static uint32_t http_next_client_seq_local(void) {
    g_http_client_nonce_local++;
    return 0x48545450u ^ ticks() ^ (g_http_client_nonce_local * 0x01010101u);
}

static int http_consume_payload_local(int fd,
                                      const uint8_t *data,
                                      uint32_t bytes,
                                      int body_only,
                                      int show_headers,
                                      int *header_done,
                                      uint32_t *header_state,
                                      struct http_result_local *result,
                                      struct http_body_state_local *body_state,
                                      uint32_t *written_out) {
    uint32_t i = 0u;
    uint32_t body_offset = 0u;

    (void)body_only;
    if (*header_done) {
        if (body_state != 0 && body_state->transfer_chunked) {
            return http_consume_body_chunked_local(fd, data, bytes, body_state, written_out);
        }
        return http_consume_body_raw_local(fd, data, bytes, body_state, written_out);
    }

    for (i = 0u; i < bytes; i++) {
        uint8_t ch = data[i];

        if (result != 0 && result->header_len + 1u < sizeof(result->header_buf)) {
            result->header_buf[result->header_len++] = ch;
        }
        if (show_headers && !http_write_local(-1, &ch, 1u)) {
            return 0;
        }
        switch (*header_state) {
            case 0u:
                *header_state = ch == '\r' ? 1u : 0u;
                break;
            case 1u:
                *header_state = ch == '\n' ? 2u : (ch == '\r' ? 1u : 0u);
                break;
            case 2u:
                *header_state = ch == '\r' ? 3u : 0u;
                break;
            case 3u:
                if (ch == '\n') {
                    *header_done = 1;
                    body_offset = i + 1u;
                    if (result != 0) {
                        http_parse_headers_local(result);
                        http_body_state_init_local(body_state, result);
                    }
                }
                *header_state = 0u;
                break;
            default:
                *header_state = 0u;
                break;
        }
        if (*header_done) {
            break;
        }
    }

    if (*header_done && body_offset < bytes) {
        if (body_state != 0 && body_state->transfer_chunked) {
            if (!http_consume_body_chunked_local(fd, data + body_offset, bytes - body_offset, body_state, written_out)) {
                return 0;
            }
        } else if (!http_consume_body_raw_local(fd, data + body_offset, bytes - body_offset, body_state, written_out)) {
            return 0;
        }
    }
    return 1;
}

static void http_default_output_path_local(const char *path, char *out, uint32_t out_size) {
    const char *base = path;
    uint32_t i = 0;
    uint32_t len;

    if (out == 0 || out_size == 0u) {
        return;
    }
    if (path == 0 || path[0] == '\0' || streq_local(path, "/")) {
        copy_line_local(out, "index.html", out_size);
        return;
    }
    while (path[i] != '\0') {
        if (path[i] == '/') {
            base = path + i + 1u;
        }
        i++;
    }
    len = str_len_local(base);
    if (len == 0u) {
        copy_line_local(out, "index.html", out_size);
        return;
    }
    copy_line_local(out, base, out_size);
}

static int http_send_tcp_segment_local(const struct syscall_rtl8139_info *nic,
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

static void http_close_connection_local(const struct syscall_rtl8139_info *nic,
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

static int tcp_connect_local(const char *host,
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

static int tcp_send_local(struct tcp_session_local *session,
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

static int tcp_ack_local(struct tcp_session_local *session) {
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

static int tcp_recv_local(struct tcp_session_local *session,
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

static void tcp_close_local(struct tcp_session_local *session) {
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

static int http_fetch_local(const char *host,
                            const char *path,
                            int output_fd,
                            int body_only,
                            int print_status,
                            int show_headers,
                            struct http_result_local *result,
                            uint32_t *written_out) {
    struct syscall_rtl8139_info nic;
    struct tcp_packet_local packet;
    uint8_t src_ip[4];
    uint8_t dns_ip[4];
    uint8_t gateway_ip[4];
    uint8_t target_ip[4];
    const uint8_t *arp_ip = target_ip;
    uint8_t target_mac[6];
    uint16_t src_port;
    uint32_t client_seq;
    uint32_t server_seq;
    uint32_t frame_bytes;
    uint32_t request_bytes;
    uint32_t payload_written = 0u;
    int saw_data = 0;
    int header_done = 0;
    uint32_t header_state = 0u;
    uint32_t syn_attempt;
    uint32_t request_timeouts = 0u;
    uint32_t body_timeouts = 0u;
    int emit_headers = show_headers || !body_only;
    struct http_body_state_local body_state;

    if (written_out != 0) {
        *written_out = 0u;
    }
    if (result != 0) {
        result->status_code = 0u;
        result->header_parsed = 0u;
        result->location_present = 0u;
        result->header_len = 0u;
        result->content_length_present = 0u;
        result->content_length = 0u;
        result->transfer_chunked = 0u;
        result->location[0] = '\0';
    }
    http_body_state_init_local(&body_state, result);
    if (host == 0 || path == 0 || path[0] != '/') {
        return 0;
    }
    ipv4_copy_runtime_config_local(src_ip, 0, gateway_ip, dns_ip);
    if (rtl8139_query(&nic) <= 0 || !nic.present || !nic.initialized || !nic.link_up) {
        write_str("http: rtl8139 not ready\n");
        return 0;
    }

    if (!parse_ipv4_local(host, target_ip)) {
        if (print_status) {
            write_str("http: resolving ");
            write_str(host);
            write_str("\n");
        }
        if (!dns_lookup_ipv4_local(&nic, src_ip, dns_ip, host, target_ip)) {
            write_str("http: dns lookup failed\n");
            return 0;
        }
        if (print_status) {
            write_str("http: ");
            write_str(host);
            write_str(" -> ");
            write_ipv4_local(target_ip);
            write_str("\n");
        }
    }

    if (!same_subnet24_local(src_ip, target_ip)) {
        arp_ip = gateway_ip;
    }
    rtl8139_drain_rx_local(32u);
    build_arp_request_local(g_arp_frame_local, nic.mac, src_ip, arp_ip);
    if (rtl8139_tx_send(g_arp_frame_local, sizeof(g_arp_frame_local)) <= 0) {
        write_str("http: arp request send failed\n");
        return 0;
    }
    if (!rtl8139_wait_arp_reply_local(arp_ip, target_mac, 100u)) {
        write_str("http: arp timeout\n");
        return 0;
    }

    src_port = http_next_src_port_local();
    client_seq = http_next_client_seq_local();

    if (print_status) {
        write_str("HTTP GET http://");
        write_str(host);
        write_str(path);
        write_str("\n");
    }

    for (syn_attempt = 0u; syn_attempt < 3u; syn_attempt++) {
        if (!http_send_tcp_segment_local(&nic,
                                         target_mac,
                                         src_ip,
                                         target_ip,
                                         src_port,
                                         80u,
                                         client_seq,
                                         0u,
                                         TCP_FLAG_SYN,
                                         0,
                                         0u)) {
            write_str("http: syn send failed\n");
            return 0;
        }
        if (rtl8139_wait_tcp_packet_local(target_ip, src_ip, 80u, src_port, 300u, &packet)) {
            break;
        }
    }
    if (syn_attempt >= 3u) {
        write_str("http: synack timeout\n");
        return 0;
    }
    if ((packet.flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) != (TCP_FLAG_SYN | TCP_FLAG_ACK) ||
        packet.ack != client_seq + 1u) {
        write_str("http: unexpected handshake packet flags=");
        write_hex_u32(packet.flags);
        write_str("\n");
        return 0;
    }
    client_seq += 1u;
    server_seq = packet.seq + 1u;

    if (!http_send_tcp_segment_local(&nic,
                                     target_mac,
                                     src_ip,
                                     target_ip,
                                     src_port,
                                     80u,
                                     client_seq,
                                     server_seq,
                                     TCP_FLAG_ACK,
                                     0,
                                     0u)) {
        write_str("http: ack send failed\n");
        return 0;
    }

    request_bytes = (uint32_t)snprintf(g_http_request_local,
                                       sizeof(g_http_request_local),
                                       "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: NexOS/0\r\nConnection: close\r\n\r\n",
                                       path,
                                       host);
    if (request_bytes == 0u || request_bytes >= sizeof(g_http_request_local)) {
        write_str("http: request too large\n");
        return 0;
    }
    if (!http_send_tcp_segment_local(&nic,
                                     target_mac,
                                     src_ip,
                                     target_ip,
                                     src_port,
                                     80u,
                                     client_seq,
                                     server_seq,
                                     TCP_FLAG_ACK | TCP_FLAG_PSH,
                                     (const uint8_t *)g_http_request_local,
                                     (uint16_t)request_bytes)) {
        write_str("http: get send failed\n");
        return 0;
    }
    client_seq += request_bytes;

    for (;;) {
        if (!rtl8139_wait_tcp_packet_local(target_ip, src_ip, 80u, src_port, 400u, &packet)) {
            if (!saw_data) {
                if (request_timeouts >= 2u) {
                    write_str("http: reply timeout\n");
                    return 0;
                }
                request_timeouts++;
                if (!http_send_tcp_segment_local(&nic,
                                                 target_mac,
                                                 src_ip,
                                                 target_ip,
                                                 src_port,
                                                 80u,
                                                 client_seq - request_bytes,
                                                 server_seq,
                                                 TCP_FLAG_ACK | TCP_FLAG_PSH,
                                                 (const uint8_t *)g_http_request_local,
                                                 (uint16_t)request_bytes)) {
                    write_str("http: get resend failed\n");
                    return 0;
                }
                continue;
            }
            if (header_done &&
                (body_state.content_length_present || body_state.transfer_chunked) &&
                !http_body_complete_local(&body_state)) {
                if (body_timeouts >= 2u) {
                    write_str("http: body timeout\n");
                    return 0;
                }
                body_timeouts++;
                if (!http_send_tcp_segment_local(&nic,
                                                 target_mac,
                                                 src_ip,
                                                 target_ip,
                                                 src_port,
                                                 80u,
                                                 client_seq,
                                                 server_seq,
                                                 TCP_FLAG_ACK,
                                                 0,
                                                 0u)) {
                    write_str("http: ack resend failed\n");
                    return 0;
                }
                continue;
            }
            break;
        }
        request_timeouts = 0u;
        body_timeouts = 0u;
        if ((packet.flags & TCP_FLAG_RST) != 0u) {
            write_str("http: connection reset\n");
            return 0;
        }
        if (packet.payload_length != 0u && packet.seq == server_seq) {
            if (!http_consume_payload_local(output_fd,
                                            g_net_rx_info_local.data + packet.payload_offset,
                                            packet.payload_length,
                                            body_only,
                                            emit_headers,
                                            &header_done,
                                            &header_state,
                                            result,
                                            &body_state,
                                            &payload_written)) {
                write_str("http: write failed\n");
                return 0;
            }
            server_seq += packet.payload_length;
            saw_data = 1;
            if (header_done &&
                (body_state.content_length_present || body_state.transfer_chunked) &&
                http_body_complete_local(&body_state)) {
                break;
            }
        }
        if ((packet.flags & TCP_FLAG_FIN) != 0u && packet.seq + packet.payload_length == server_seq) {
            server_seq += 1u;
            (void)http_send_tcp_segment_local(&nic,
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
        if (!http_send_tcp_segment_local(&nic,
                                         target_mac,
                                         src_ip,
                                         target_ip,
                                         src_port,
                                         80u,
                                         client_seq,
                                         server_seq,
                                         TCP_FLAG_ACK,
                                         0,
                                         0u)) {
            write_str("http: ack send failed\n");
            return 0;
        }
    }
    if (!saw_data && (result == 0 || !result->header_parsed)) {
        write_str("http: empty response\n");
        return 0;
    }
    frame_bytes = build_tcp_ipv4_local(g_http_frame_local,
                                       nic.mac,
                                       target_mac,
                                       src_ip,
                                       target_ip,
                                       src_port,
                                       80u,
                                       client_seq,
                                       server_seq,
                                       TCP_FLAG_ACK | TCP_FLAG_FIN,
                                       0,
                                       0u);
    (void)rtl8139_tx_send(g_http_frame_local, frame_bytes);
    if (result != 0 && !result->header_parsed) {
        http_parse_headers_local(result);
    }
    if (print_status && result != 0 && result->status_code != 0u) {
        write_str("http: status ");
        write_dec(result->status_code);
        write_str("\n");
    }
    http_close_connection_local(&nic, target_mac, src_ip, target_ip, src_port, &client_seq, &server_seq);
    if (written_out != 0) {
        *written_out = payload_written;
    }
    return 1;
}

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

int cmd_dhcp(int argc, char **argv) {
    struct syscall_rtl8139_info nic;
    struct dhcp_config_local offer;
    struct dhcp_config_local ack;
    uint32_t xid;
    uint32_t frame_bytes;

    (void)argv;

    if (argc > 1) {
        write_err_usage("dhcp", "\n");
        return 1;
    }
    if (rtl8139_query(&nic) <= 0 || !nic.present || !nic.initialized || !nic.link_up) {
        write_str("dhcp: rtl8139 not ready\n");
        return 1;
    }

    xid = 0x44480000u | (ticks() & 0x0000ffffu);
    write_str("DHCP discover via rtl8139 xid=");
    write_hex_u32(xid);
    write_str("\n");

    frame_bytes = build_dhcp_packet_local(g_dhcp_frame_local, nic.mac, xid, DHCP_MSG_DISCOVER, 0, 0);
    if (rtl8139_tx_send(g_dhcp_frame_local, frame_bytes) <= 0) {
        write_str("dhcp: discover send failed\n");
        return 1;
    }
    if (!rtl8139_wait_dhcp_reply_local(xid, DHCP_MSG_OFFER, 300u, &offer)) {
        write_str("dhcp: offer timeout\n");
        return 1;
    }
    if (offer.message_type == DHCP_MSG_NAK) {
        write_str("dhcp: received nak during discover\n");
        return 1;
    }

    write_str("offer ip=");
    write_ipv4_local(offer.yiaddr);
    write_str(" server=");
    write_ipv4_local(offer.server_id);
    if (offer.got_subnet_mask) {
        write_str(" mask=");
        write_ipv4_local(offer.subnet_mask);
    }
    if (offer.got_router) {
        write_str(" gw=");
        write_ipv4_local(offer.router);
    }
    if (offer.got_dns) {
        write_str(" dns=");
        write_ipv4_local(offer.dns);
    }
    write_str("\n");

    frame_bytes =
        build_dhcp_packet_local(g_dhcp_frame_local, nic.mac, xid, DHCP_MSG_REQUEST, offer.yiaddr, offer.server_id);
    if (rtl8139_tx_send(g_dhcp_frame_local, frame_bytes) <= 0) {
        write_str("dhcp: request send failed\n");
        return 1;
    }
    write_str("dhcp: request sent\n");
    if (!rtl8139_wait_dhcp_reply_local(xid, DHCP_MSG_ACK, 300u, &ack)) {
        write_str("dhcp: ack timeout\n");
        return 1;
    }
    if (ack.message_type == DHCP_MSG_NAK) {
        write_str("dhcp: received nak\n");
        return 1;
    }

    write_str("lease ip=");
    write_ipv4_local(ack.yiaddr);
    write_str("\n");
    if (ack.got_subnet_mask) {
        write_str("mask ");
        write_ipv4_local(ack.subnet_mask);
        write_str("\n");
    }
    if (ack.got_router) {
        write_str("gateway ");
        write_ipv4_local(ack.router);
        write_str("\n");
    }
    if (ack.got_dns) {
        write_str("dns ");
        write_ipv4_local(ack.dns);
        write_str("\n");
    }
    write_str("server ");
    write_ipv4_local(ack.server_id);
    write_str("\n");
    if (ack.got_lease_time) {
        write_str("lease_time ");
        write_dec(ack.lease_time);
        write_str("s\n");
    }
    ipv4_apply_dhcp_config_local(&ack);
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

int cmd_http(int argc, char **argv) {
    struct http_result_local result;

    if (argc < 2 || argc > 3) {
        write_err_usage("http", " <host|ipv4|http://host/path> [path]\n");
        return 1;
    }
    if (argc == 2) {
        if (!http_split_target_local(argv[1], g_http_host_local, sizeof(g_http_host_local),
                                     g_http_path_local, sizeof(g_http_path_local))) {
            write_err_usage("http", " <host|ipv4|http://host/path> [path]\n");
            return 1;
        }
    } else {
        copy_line_local(g_http_host_local, argv[1], sizeof(g_http_host_local));
        copy_line_local(g_http_path_local, argv[2], sizeof(g_http_path_local));
        if (g_http_host_local[0] == '\0' || g_http_path_local[0] != '/') {
            write_err_usage("http", " <host|ipv4|http://host/path> [path]\n");
            return 1;
        }
    }
    return http_fetch_local(g_http_host_local, g_http_path_local, -1, 0, 1, 0, &result, 0) ? 0 : 1;
}

int cmd_wget(int argc, char **argv) {
    char out_path[CMD_PATH_MAX];
    const char *target = 0;
    const char *output_override = 0;
    uint32_t written = 0u;
    int fd;
    int ok;
    int quiet = 0;
    int show_headers = 0;
    int redirects = 0;
    int i;

    if (argc < 2) {
        write_err_usage("wget", " [-q] [-S] [-O file] <url|host|ipv4|http://host/path>\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (streq_local(argv[i], "-q")) {
            quiet = 1;
            continue;
        }
        if (streq_local(argv[i], "-S")) {
            show_headers = 1;
            continue;
        }
        if (streq_local(argv[i], "-O")) {
            if (i + 1 >= argc) {
                write_err_usage("wget", " [-q] [-S] [-O file] <url|host|ipv4|http://host/path>\n");
                return 1;
            }
            output_override = argv[++i];
            continue;
        }
        if (target == 0) {
            target = argv[i];
            continue;
        }
        if (output_override == 0) {
            output_override = argv[i];
            continue;
        }
        write_err_usage("wget", " [-q] [-S] [-O file] <url|host|ipv4|http://host/path>\n");
        return 1;
    }
    if (target == 0) {
        write_err_usage("wget", " [-q] [-S] [-O file] <url|host|ipv4|http://host/path>\n");
        return 1;
    }
    if (!http_split_target_local(target, g_http_host_local, sizeof(g_http_host_local),
                                 g_http_path_local, sizeof(g_http_path_local))) {
        write_err_usage("wget", " [-q] [-S] [-O file] <url|host|ipv4|http://host/path>\n");
        return 1;
    }
    if (output_override != 0) {
        copy_line_local(out_path, output_override, sizeof(out_path));
        if (out_path[0] == '\0') {
            write_err_usage("wget", " [-q] [-S] [-O file] <url|host|ipv4|http://host/path>\n");
            return 1;
        }
    } else {
        http_default_output_path_local(g_http_path_local, out_path, sizeof(out_path));
    }

    for (;;) {
        struct http_result_local result;

        fd = open(out_path, O_CREAT | O_TRUNC);
        if (fd < 0) {
            write_str("wget: output open failed: ");
            write_str(out_path);
            write_str("\n");
            return 1;
        }

        ok = http_fetch_local(g_http_host_local,
                              g_http_path_local,
                              fd,
                              1,
                              quiet ? 0 : 1,
                              show_headers,
                              &result,
                              &written);
        close((uint32_t)fd);
        if (!ok) {
            (void)remove(out_path);
            return 1;
        }
        if ((result.status_code == 301u || result.status_code == 302u ||
             result.status_code == 303u || result.status_code == 307u ||
             result.status_code == 308u) &&
            result.location_present && redirects < 4 &&
            http_apply_redirect_local(result.location,
                                      g_http_path_local,
                                      g_http_host_local,
                                      sizeof(g_http_host_local),
                                      g_http_path_local,
                                      sizeof(g_http_path_local))) {
            redirects++;
            (void)remove(out_path);
            if (!quiet) {
                write_str("wget: redirect -> http://");
                write_str(g_http_host_local);
                write_str(g_http_path_local);
                write_str("\n");
            }
            continue;
        }
        break;
    }

    if (!quiet) {
        write_str("wget: saved ");
        write_dec(written);
        write_str(" bytes to ");
        write_str(out_path);
        write_str("\n");
    }
    return 0;
}

int cmd_nc(int argc, char **argv) {
    struct tcp_session_local session;
    struct tcp_packet_local packet;
    uint32_t port = 0u;
    uint32_t payload_len = 0u;
    uint32_t i;
    uint32_t saw_data = 0u;
    uint8_t last_was_newline = 1u;

    if (argc < 3) {
        write_err_usage("nc", " <host|ipv4> <port> [text...]\n");
        return 1;
    }
    if (!parse_u32_local(argv[2], &port) || port == 0u || port > 65535u) {
        write_err_usage("nc", " <host|ipv4> <port> [text...]\n");
        return 1;
    }
    g_tcp_payload_local[0] = '\0';
    for (i = 3u; i < (uint32_t)argc; i++) {
        uint32_t used = str_len_local(g_tcp_payload_local);
        uint32_t remaining = sizeof(g_tcp_payload_local) - used;

        if (used != 0u) {
            if (remaining <= 1u) {
                write_err_str("nc: payload too large\n");
                return 1;
            }
            g_tcp_payload_local[used++] = ' ';
            g_tcp_payload_local[used] = '\0';
            remaining = sizeof(g_tcp_payload_local) - used;
        }
        copy_line_local(g_tcp_payload_local + used, argv[i], remaining);
        if (str_len_local(g_tcp_payload_local + used) + 1u >= remaining) {
            write_err_str("nc: payload too large\n");
            return 1;
        }
    }
    if (!tcp_connect_local(argv[1], (uint16_t)port, &session, 1)) {
        return 1;
    }
    write_str("nc: connected ");
    write_str(argv[1]);
    write_str(":");
    write_dec(port);
    write_str("\n");

    payload_len = str_len_local(g_tcp_payload_local);
    if (payload_len != 0u) {
        if (!tcp_send_local(&session, TCP_FLAG_ACK | TCP_FLAG_PSH, (const uint8_t *)g_tcp_payload_local, (uint16_t)payload_len)) {
            write_err_str("nc: send failed\n");
            tcp_close_local(&session);
            return 1;
        }
    }

    for (;;) {
        if (!tcp_recv_local(&session, 300u, &packet)) {
            break;
        }
        if ((packet.flags & TCP_FLAG_RST) != 0u) {
            write_err_str("nc: connection reset\n");
            return 1;
        }
        if (packet.payload_length != 0u && packet.seq == session.server_seq) {
            (void)write_stdout(g_net_rx_info_local.data + packet.payload_offset, packet.payload_length);
            last_was_newline =
                g_net_rx_info_local.data[packet.payload_offset + packet.payload_length - 1u] == '\n' ? 1u : 0u;
            session.server_seq += packet.payload_length;
            saw_data = 1u;
        }
        if ((packet.flags & TCP_FLAG_FIN) != 0u && packet.seq + packet.payload_length == session.server_seq) {
            session.server_seq += 1u;
            (void)tcp_ack_local(&session);
            break;
        }
        if (!tcp_ack_local(&session)) {
            write_err_str("nc: ack failed\n");
            return 1;
        }
    }

    tcp_close_local(&session);
    if (saw_data && !last_was_newline) {
        write_str("\n");
    }
    return 0;
}

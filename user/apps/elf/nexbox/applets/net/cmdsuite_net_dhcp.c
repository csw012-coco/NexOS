#include "user/apps/elf/nexbox/applets/net/cmdsuite_net_common.h"

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

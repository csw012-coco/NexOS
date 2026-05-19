#ifndef NEXBOX_CMDSUITE_NET_COMMON_H
#define NEXBOX_CMDSUITE_NET_COMMON_H

#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

struct ipv4_runtime_config_local {
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
};

struct arp_cache_entry_local {
    uint8_t ip[4];
    uint8_t mac[6];
    uint32_t tick;
    uint8_t valid;
};

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

struct nexbox_net_context_local {
    struct syscall_rtl8139_rx_info rx_info;
    uint8_t arp_frame[60];
    uint8_t dns_frame[512];
    uint8_t dhcp_frame[512];
    uint8_t dns_name[256];
    uint8_t http_frame[1600];
    char tcp_payload[512];
    char http_host[128];
    char http_path[256];
    char http_request[512];
    uint32_t http_client_nonce;
    struct ipv4_runtime_config_local ipv4_config;
    struct arp_cache_entry_local arp_cache[8];
};

extern struct nexbox_net_context_local g_net_context_local;

#define g_net_rx_info_local (g_net_context_local.rx_info)
#define g_arp_frame_local (g_net_context_local.arp_frame)
#define g_dns_frame_local (g_net_context_local.dns_frame)
#define g_dhcp_frame_local (g_net_context_local.dhcp_frame)
#define g_dns_name_local (g_net_context_local.dns_name)
#define g_http_frame_local (g_net_context_local.http_frame)
#define g_tcp_payload_local (g_net_context_local.tcp_payload)
#define g_http_host_local (g_net_context_local.http_host)
#define g_http_path_local (g_net_context_local.http_path)
#define g_http_request_local (g_net_context_local.http_request)
#define g_http_client_nonce_local (g_net_context_local.http_client_nonce)
#define g_ipv4_config_local (g_net_context_local.ipv4_config)
#define g_arp_cache_local (g_net_context_local.arp_cache)

void write_hex_byte_local(uint8_t value);
uint16_t read_be16_local(const uint8_t *src);
uint32_t read_be32_local(const uint8_t *src);
int parse_ipv4_local(const char *text, uint8_t out[4]);
void write_mac_local(const uint8_t *mac);
void write_ipv4_local(const uint8_t *addr);
void write_u16_be_local(uint8_t *dst, uint16_t value);
void write_u32_be_local(uint8_t *dst, uint32_t value);
uint16_t checksum16_local(const uint8_t *data, uint32_t bytes);
uint16_t udp_checksum_ipv4_local(const uint8_t src_ip[4],
                                 const uint8_t dst_ip[4],
                                 const uint8_t *udp,
                                 uint16_t udp_length);
uint16_t tcp_checksum_ipv4_local(const uint8_t src_ip[4],
                                 const uint8_t dst_ip[4],
                                 const uint8_t *tcp,
                                 uint16_t tcp_length);
void build_arp_request_local(uint8_t *frame,
                             const uint8_t src_mac[6],
                             const uint8_t src_ip[4],
                             const uint8_t target_ip[4]);
int same_subnet24_local(const uint8_t a[4], const uint8_t b[4]);
void ipv4_copy_runtime_config_local(uint8_t ip[4],
                                    uint8_t mask[4],
                                    uint8_t gateway[4],
                                    uint8_t dns[4]);
void ipv4_apply_dhcp_config_local(const struct dhcp_config_local *config);
int rtl8139_wait_arp_reply_local(const uint8_t target_ip[4],
                                 uint8_t target_mac[6],
                                 uint32_t timeout_ticks);

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
                              uint16_t payload_bytes);
int rtl8139_wait_tcp_packet_local(const uint8_t src_ip[4],
                                  const uint8_t dst_ip[4],
                                  uint16_t src_port,
                                  uint16_t dst_port,
                                  uint32_t timeout_ticks,
                                  struct tcp_packet_local *packet_out);
uint32_t rtl8139_drain_rx_local(uint32_t max_packets);
const char *ipv4_protocol_name_local(uint8_t protocol);
int dns_lookup_ipv4_local(const struct syscall_rtl8139_info *nic,
                          const uint8_t src_ip[4],
                          const uint8_t dns_ip[4],
                          const char *name,
                          uint8_t out_ip[4]);
uint16_t http_next_src_port_local(void);
uint32_t http_next_client_seq_local(void);
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
                                uint16_t payload_bytes);
void http_close_connection_local(const struct syscall_rtl8139_info *nic,
                                 const uint8_t target_mac[6],
                                 const uint8_t src_ip[4],
                                 const uint8_t target_ip[4],
                                 uint16_t src_port,
                                 uint32_t *client_seq_io,
                                 uint32_t *server_seq_io);

int tcp_connect_local(const char *host,
                      uint16_t dst_port,
                      struct tcp_session_local *session,
                      int print_status);
int tcp_send_local(struct tcp_session_local *session,
                   uint16_t flags,
                   const uint8_t *payload,
                   uint16_t payload_bytes);
int tcp_ack_local(struct tcp_session_local *session);
int tcp_recv_local(struct tcp_session_local *session,
                   uint32_t timeout_ticks,
                   struct tcp_packet_local *packet_out);
void tcp_close_local(struct tcp_session_local *session);

#endif

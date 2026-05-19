#include "user/apps/elf/nexbox/applets/net/cmdsuite_net_common.h"

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

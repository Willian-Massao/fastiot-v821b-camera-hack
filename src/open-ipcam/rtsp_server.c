#include "rtsp_server.h"

#define MAX_RTSP_CLIENTS 8
#define RTSP_BUFFER_SIZE 4096

typedef enum {
    TRANSPORT_RTP_UDP,
    TRANSPORT_RTP_TCP_INTERLEAVED
} rtp_transport_t;

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    bool is_active;
    bool is_playing;
    rtp_transport_t transport;
    int rtp_channel;
    int rtcp_channel;
    int rtp_udp_port;
    int rtcp_udp_port;
    int rtp_udp_fd;
    uint16_t rtp_seq;
    uint32_t session_id;
    int stream_id; // 0: main, 1: sub
    time_t last_activity;
} rtsp_client_t;

static int g_rtsp_server_fd = -1;
static rtsp_client_t g_clients[MAX_RTSP_CLIENTS];
static pthread_mutex_t g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t generate_session_id(void) {
    static uint32_t counter = 100000;
    return ++counter;
}

static void send_rtsp_response(int fd, int cseq, const char *extra_headers, const char *body) {
    char resp[RTSP_BUFFER_SIZE];
    int body_len = body ? strlen(body) : 0;
    int len;

    if (body_len > 0) {
        len = snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Server: open-ipcam/%s\r\n"
            "%s"
            "Content-Type: application/sdp\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "%s",
            cseq, OPEN_IPCAM_VERSION, extra_headers ? extra_headers : "", body_len, body);
    } else {
        len = snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "Server: open-ipcam/%s\r\n"
            "%s"
            "\r\n",
            cseq, OPEN_IPCAM_VERSION, extra_headers ? extra_headers : "");
    }
    if (len > 0) {
        send(fd, resp, len, 0);
    }
}

static void handle_options(int fd, int cseq) {
    const char headers[] = "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER\r\n";
    send_rtsp_response(fd, cseq, headers, NULL);
}

static void handle_describe(int fd, int cseq, const char *url, const char *ip_str) {
    (void)url;
    char sdp[1024];
    snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=- %u 1 IN IP4 %s\r\n"
        "s=open-ipcam live stream\r\n"
        "t=0 0\r\n"
        "a=control:*\r\n"
        "a=range:npt=0-\r\n"
        "m=video 0 RTP/AVP 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "b=AS:2000\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 packetization-mode=1;profile-level-id=42e01f\r\n"
        "a=control:trackID=0\r\n"
        "m=audio 0 RTP/AVP 8\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtpmap:8 PCMA/8000/1\r\n"
        "a=control:trackID=1\r\n",
        (unsigned int)time(NULL), ip_str);

    send_rtsp_response(fd, cseq, NULL, sdp);
}

static void handle_setup(rtsp_client_t *client, int cseq, const char *transport_header) {
    client->session_id = generate_session_id();
    char headers[256];

    if (strstr(transport_header, "RTP/AVP/TCP") || strstr(transport_header, "interleaved")) {
        client->transport = TRANSPORT_RTP_TCP_INTERLEAVED;
        int rtp_ch = 0, rtcp_ch = 1;
        const char *ch = strstr(transport_header, "interleaved=");
        if (ch) {
            sscanf(ch, "interleaved=%d-%d", &rtp_ch, &rtcp_ch);
        }
        client->rtp_channel = rtp_ch;
        client->rtcp_channel = rtcp_ch;
        snprintf(headers, sizeof(headers),
            "Session: %u;timeout=60\r\n"
            "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n",
            client->session_id, rtp_ch, rtcp_ch);
    } else {
        client->transport = TRANSPORT_RTP_UDP;
        int client_rtp = 0, client_rtcp = 0;
        const char *port_str = strstr(transport_header, "client_port=");
        if (port_str) {
            sscanf(port_str, "client_port=%d-%d", &client_rtp, &client_rtcp);
        }
        client->rtp_udp_port = client_rtp;
        client->rtcp_udp_port = client_rtcp;
        snprintf(headers, sizeof(headers),
            "Session: %u;timeout=60\r\n"
            "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=60000-60001\r\n",
            client->session_id, client_rtp, client_rtcp);
    }

    send_rtsp_response(client->client_fd, cseq, headers, NULL);
}

static void handle_play(rtsp_client_t *client, int cseq) {
    client->is_playing = true;
    char headers[256];
    snprintf(headers, sizeof(headers),
        "Session: %u\r\n"
        "Range: npt=0.000-\r\n"
        "RTP-Info: url=trackID=0;seq=%u\r\n",
        client->session_id, client->rtp_seq);
    send_rtsp_response(client->client_fd, cseq, headers, NULL);
    LOG_INFO("RTSP Client [%u] started playback", client->session_id);
}

static void handle_teardown(rtsp_client_t *client, int cseq) {
    char headers[128];
    snprintf(headers, sizeof(headers), "Session: %u\r\n", client->session_id);
    send_rtsp_response(client->client_fd, cseq, headers, NULL);
    client->is_playing = false;
    client->is_active = false;
    LOG_INFO("RTSP Client [%u] teardown", client->session_id);
}

static void handle_client_request(rtsp_client_t *client, const char *req, size_t req_len) {
    (void)req_len;
    char method[16], url[256], proto[16];
    int cseq = 1;

    if (sscanf(req, "%15s %255s %15s", method, url, proto) < 3) return;

    const char *cseq_str = strstr(req, "CSeq:");
    if (cseq_str) {
        cseq = atoi(cseq_str + 5);
    }

    client->last_activity = time(NULL);

    if (strcmp(method, "OPTIONS") == 0) {
        handle_options(client->client_fd, cseq);
    } else if (strcmp(method, "DESCRIBE") == 0) {
        char ip_str[INET_ADDRSTRLEN] = "127.0.0.1";
        inet_ntop(AF_INET, &(client->client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        handle_describe(client->client_fd, cseq, url, ip_str);
    } else if (strcmp(method, "SETUP") == 0) {
        const char *tr = strstr(req, "Transport:");
        handle_setup(client, cseq, tr ? tr : "");
    } else if (strcmp(method, "PLAY") == 0) {
        handle_play(client, cseq);
    } else if (strcmp(method, "TEARDOWN") == 0) {
        handle_teardown(client, cseq);
    } else if (strcmp(method, "GET_PARAMETER") == 0) {
        send_rtsp_response(client->client_fd, cseq, NULL, NULL);
    }
}

int rtsp_server_init(int port) {
    memset(g_clients, 0, sizeof(g_clients));

    g_rtsp_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_rtsp_server_fd < 0) {
        LOG_ERROR("Failed to create RTSP socket: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(g_rtsp_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port > 0 ? port : 554);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_rtsp_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind RTSP to port %d: %s", port, strerror(errno));
        close(g_rtsp_server_fd);
        g_rtsp_server_fd = -1;
        return -1;
    }

    if (listen(g_rtsp_server_fd, 10) < 0) {
        LOG_ERROR("Failed to listen on RTSP socket: %s", strerror(errno));
        close(g_rtsp_server_fd);
        g_rtsp_server_fd = -1;
        return -1;
    }

    LOG_INFO("RTSP Server listening on port %d", port > 0 ? port : 554);
    return 0;
}

void rtsp_server_stop(void) {
    pthread_mutex_lock(&g_clients_mutex);
    for (int i = 0; i < MAX_RTSP_CLIENTS; i++) {
        if (g_clients[i].is_active) {
            close(g_clients[i].client_fd);
            g_clients[i].is_active = false;
        }
    }
    if (g_rtsp_server_fd >= 0) {
        close(g_rtsp_server_fd);
        g_rtsp_server_fd = -1;
    }
    pthread_mutex_unlock(&g_clients_mutex);
}

void *rtsp_server_thread(void *arg) {
    (void)arg;
    char buffer[RTSP_BUFFER_SIZE];

    while (g_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = g_rtsp_server_fd;

        if (g_rtsp_server_fd >= 0) {
            FD_SET(g_rtsp_server_fd, &read_fds);
        }

        pthread_mutex_lock(&g_clients_mutex);
        int active_count = 0;
        for (int i = 0; i < MAX_RTSP_CLIENTS; i++) {
            if (g_clients[i].is_active) {
                FD_SET(g_clients[i].client_fd, &read_fds);
                if (g_clients[i].client_fd > max_fd) {
                    max_fd = g_clients[i].client_fd;
                }
                active_count++;
            }
        }
        pthread_mutex_unlock(&g_clients_mutex);

        pthread_mutex_lock(&g_state_mutex);
        g_state.rtsp_clients_count = active_count;
        pthread_mutex_unlock(&g_state_mutex);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ret <= 0) continue;

        // New incoming connection
        if (g_rtsp_server_fd >= 0 && FD_ISSET(g_rtsp_server_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int new_fd = accept(g_rtsp_server_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (new_fd >= 0) {
                pthread_mutex_lock(&g_clients_mutex);
                bool added = false;
                for (int i = 0; i < MAX_RTSP_CLIENTS; i++) {
                    if (!g_clients[i].is_active) {
                        g_clients[i].client_fd = new_fd;
                        g_clients[i].client_addr = client_addr;
                        g_clients[i].is_active = true;
                        g_clients[i].is_playing = false;
                        g_clients[i].rtp_seq = 0;
                        g_clients[i].last_activity = time(NULL);
                        added = true;
                        char ip_buf[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
                        LOG_INFO("New RTSP connection from %s:%d (slot %d)", ip_buf, ntohs(client_addr.sin_port), i);
                        break;
                    }
                }
                if (!added) {
                    LOG_WARN("Max RTSP clients reached (%d). Connection rejected.", MAX_RTSP_CLIENTS);
                    close(new_fd);
                }
                pthread_mutex_unlock(&g_clients_mutex);
            }
        }

        // Process data on active clients
        pthread_mutex_lock(&g_clients_mutex);
        for (int i = 0; i < MAX_RTSP_CLIENTS; i++) {
            if (g_clients[i].is_active && FD_ISSET(g_clients[i].client_fd, &read_fds)) {
                ssize_t n = recv(g_clients[i].client_fd, buffer, sizeof(buffer) - 1, 0);
                if (n <= 0) {
                    close(g_clients[i].client_fd);
                    g_clients[i].is_active = false;
                    LOG_INFO("RTSP client disconnected (slot %d)", i);
                } else {
                    buffer[n] = '\0';
                    handle_client_request(&g_clients[i], buffer, n);
                }
            }
        }
        pthread_mutex_unlock(&g_clients_mutex);
    }
    return NULL;
}

int rtsp_broadcast_h264_nalu(int stream_id, const uint8_t *nalu, size_t len, uint32_t timestamp) {
    (void)stream_id;
    if (!nalu || len == 0) return 0;

    pthread_mutex_lock(&g_clients_mutex);
    for (int i = 0; i < MAX_RTSP_CLIENTS; i++) {
        if (!g_clients[i].is_active || !g_clients[i].is_playing) continue;

        // Package as RTP packet
        uint8_t rtp_pkt[1500];
        // RTP Header (12 bytes)
        rtp_pkt[0] = 0x80; // V=2, P=0, X=0, CC=0
        rtp_pkt[1] = 96;   // M=0, PT=96 (H.264 dynamic)
        uint16_t seq = htons(g_clients[i].rtp_seq++);
        memcpy(&rtp_pkt[2], &seq, 2);
        uint32_t ts = htonl(timestamp);
        memcpy(&rtp_pkt[4], &ts, 4);
        uint32_t ssrc = htonl(0x12345678);
        memcpy(&rtp_pkt[8], &ssrc, 4);

        if (len <= 1400) {
            // Single NAL unit packet
            rtp_pkt[1] |= 0x80; // Marker bit = 1
            memcpy(&rtp_pkt[12], nalu, len);
            size_t total_rtp_len = 12 + len;

            if (g_clients[i].transport == TRANSPORT_RTP_TCP_INTERLEAVED) {
                // Interleaved header: '$' (0x24) + channel + 2-byte length
                uint8_t tcp_hdr[4];
                tcp_hdr[0] = '$';
                tcp_hdr[1] = (uint8_t)g_clients[i].rtp_channel;
                uint16_t plen = htons((uint16_t)total_rtp_len);
                memcpy(&tcp_hdr[2], &plen, 2);

                send(g_clients[i].client_fd, tcp_hdr, 4, MSG_NOSIGNAL);
                send(g_clients[i].client_fd, rtp_pkt, total_rtp_len, MSG_NOSIGNAL);
            }
        }
    }
    pthread_mutex_unlock(&g_clients_mutex);
    return 0;
}

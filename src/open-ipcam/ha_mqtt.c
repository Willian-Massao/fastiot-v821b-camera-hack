#include "ha_mqtt.h"
#include "motor_ctrl.h"
#include "gpio_ctrl.h"

static int g_mqtt_fd = -1;
static bool g_mqtt_connected = false;
static pthread_mutex_t g_mqtt_mutex = PTHREAD_MUTEX_INITIALIZER;

static int mqtt_send_packet(const uint8_t *pkt, size_t len) {
    if (g_mqtt_fd < 0) return -1;
    return send(g_mqtt_fd, pkt, len, MSG_NOSIGNAL);
}

static int mqtt_connect_broker(void) {
    if (!g_config.mqtt_enabled || strlen(g_config.mqtt_broker) == 0) {
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_config.mqtt_port > 0 ? g_config.mqtt_port : 1883);
    addr.sin_addr.s_addr = inet_addr(g_config.mqtt_broker);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    // MQTT 3.1.1 CONNECT packet
    uint8_t pkt[256];
    const char client_id[] = "open_ipcam_v821b";
    uint8_t var_hdr[] = { 0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x02, 0x00, 0x3c }; // Keepalive 60s, Clean Session

    size_t cid_len = strlen(client_id);
    size_t rem_len = sizeof(var_hdr) + 2 + cid_len;

    pkt[0] = 0x10; // CONNECT
    pkt[1] = (uint8_t)rem_len;
    memcpy(&pkt[2], var_hdr, sizeof(var_hdr));
    size_t off = 2 + sizeof(var_hdr);
    pkt[off++] = (cid_len >> 8) & 0xff;
    pkt[off++] = cid_len & 0xff;
    memcpy(&pkt[off], client_id, cid_len);
    off += cid_len;

    if (send(fd, pkt, off, 0) < 0) {
        close(fd);
        return -1;
    }

    // Read CONNACK (4 bytes: 0x20 0x02 0x00 0x00)
    uint8_t ack[4];
    if (recv(fd, ack, 4, 0) != 4 || ack[0] != 0x20 || ack[3] != 0x00) {
        close(fd);
        return -1;
    }

    g_mqtt_fd = fd;
    g_mqtt_connected = true;
    LOG_INFO("Connected to Home Assistant MQTT Broker (%s:%d)", g_config.mqtt_broker, g_config.mqtt_port);
    return 0;
}

static int mqtt_publish(const char *topic, const char *payload, bool retain) {
    if (!g_mqtt_connected || g_mqtt_fd < 0) return -1;

    size_t tlen = strlen(topic);
    size_t plen = strlen(payload);
    size_t rem_len = 2 + tlen + plen;

    uint8_t pkt[2048];
    if (rem_len + 5 > sizeof(pkt)) return -1;

    size_t off = 0;
    pkt[off++] = 0x30 | (retain ? 0x01 : 0x00); // PUBLISH, QoS 0

    // Remaining length encoding (variable length)
    if (rem_len < 128) {
        pkt[off++] = (uint8_t)rem_len;
    } else {
        pkt[off++] = (uint8_t)((rem_len & 0x7f) | 0x80);
        pkt[off++] = (uint8_t)(rem_len >> 7);
    }

    pkt[off++] = (tlen >> 8) & 0xff;
    pkt[off++] = tlen & 0xff;
    memcpy(&pkt[off], topic, tlen);
    off += tlen;

    memcpy(&pkt[off], payload, plen);
    off += plen;

    pthread_mutex_lock(&g_mqtt_mutex);
    int ret = mqtt_send_packet(pkt, off);
    pthread_mutex_unlock(&g_mqtt_mutex);
    return ret;
}

int ha_mqtt_publish_discovery(void) {
    if (!g_mqtt_connected) return -1;

    LOG_INFO("Publishing Home Assistant MQTT Autodiscovery definitions...");

    // 1. PTZ Buttons (Up, Down, Left, Right, Home)
    const char *buttons[] = { "up", "down", "left", "right", "home" };
    for (size_t i = 0; i < sizeof(buttons)/sizeof(buttons[0]); i++) {
        char topic[128], payload[512];
        snprintf(topic, sizeof(topic), "homeassistant/button/open_ipcam/ptz_%s/config", buttons[i]);
        snprintf(payload, sizeof(payload),
            "{\"name\":\"Camera PTZ %s\","
            "\"unique_id\":\"open_ipcam_ptz_%s\","
            "\"command_topic\":\"open_ipcam/ptz/set\","
            "\"payload_press\":\"%c\","
            "\"device\":{\"identifiers\":[\"open_ipcam_v821b\"],\"name\":\"Open-IPCam V821B\",\"model\":\"V821B\",\"manufacturer\":\"FastIoT / Open-IPCam\"}}",
            buttons[i], buttons[i], buttons[i][0]);
        mqtt_publish(topic, payload, true);
    }

    // 2. Night Vision Switch
    {
        char topic[128], payload[512];
        snprintf(topic, sizeof(topic), "homeassistant/switch/open_ipcam/ir_led/config");
        snprintf(payload, sizeof(payload),
            "{\"name\":\"Camera IR LED\","
            "\"unique_id\":\"open_ipcam_ir_led\","
            "\"command_topic\":\"open_ipcam/irled/set\","
            "\"state_topic\":\"open_ipcam/irled/state\","
            "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
            "\"device\":{\"identifiers\":[\"open_ipcam_v821b\"]}}");
        mqtt_publish(topic, payload, true);
    }

    // 3. Spotlight Light Entity
    {
        char topic[128], payload[512];
        snprintf(topic, sizeof(topic), "homeassistant/light/open_ipcam/spotlight/config");
        snprintf(payload, sizeof(payload),
            "{\"name\":\"Camera Spotlight\","
            "\"unique_id\":\"open_ipcam_spotlight\","
            "\"command_topic\":\"open_ipcam/light/set\","
            "\"state_topic\":\"open_ipcam/light/state\","
            "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
            "\"device\":{\"identifiers\":[\"open_ipcam_v821b\"]}}");
        mqtt_publish(topic, payload, true);
    }

    // 4. Siren Entity
    {
        char topic[128], payload[512];
        snprintf(topic, sizeof(topic), "homeassistant/siren/open_ipcam/siren/config");
        snprintf(payload, sizeof(payload),
            "{\"name\":\"Camera Siren\","
            "\"unique_id\":\"open_ipcam_siren\","
            "\"command_topic\":\"open_ipcam/siren/set\","
            "\"state_topic\":\"open_ipcam/siren/state\","
            "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
            "\"device\":{\"identifiers\":[\"open_ipcam_v821b\"]}}");
        mqtt_publish(topic, payload, true);
    }

    return 0;
}

int ha_mqtt_publish_state(void) {
    if (!g_mqtt_connected) return -1;

    pthread_mutex_lock(&g_state_mutex);
    bool ir = g_state.ir_led_state;
    int wled = g_state.white_led_brightness;
    bool siren = g_state.siren_state;
    pthread_mutex_unlock(&g_state_mutex);

    mqtt_publish("open_ipcam/irled/state", ir ? "ON" : "OFF", false);
    mqtt_publish("open_ipcam/light/state", wled > 0 ? "ON" : "OFF", false);
    mqtt_publish("open_ipcam/siren/state", siren ? "ON" : "OFF", false);
    return 0;
}

int ha_mqtt_init(void) {
    if (!g_config.mqtt_enabled) {
        LOG_INFO("MQTT integration disabled in configuration.");
        return 0;
    }
    return mqtt_connect_broker();
}

void ha_mqtt_stop(void) {
    pthread_mutex_lock(&g_mqtt_mutex);
    if (g_mqtt_fd >= 0) {
        uint8_t disconnect_pkt[] = { 0xe0, 0x00 };
        send(g_mqtt_fd, disconnect_pkt, 2, MSG_NOSIGNAL);
        close(g_mqtt_fd);
        g_mqtt_fd = -1;
    }
    g_mqtt_connected = false;
    pthread_mutex_unlock(&g_mqtt_mutex);
}

void *ha_mqtt_thread(void *arg) {
    (void)arg;
    if (!g_config.mqtt_enabled) return NULL;

    ha_mqtt_publish_discovery();

    time_t last_ping = time(NULL);
    while (g_running) {
        if (!g_mqtt_connected) {
            sleep(5);
            if (mqtt_connect_broker() == 0) {
                ha_mqtt_publish_discovery();
            }
            continue;
        }

        // Send PINGREQ every 30s
        time_t now = time(NULL);
        if (now - last_ping >= 30) {
            uint8_t ping[] = { 0xc0, 0x00 };
            pthread_mutex_lock(&g_mqtt_mutex);
            mqtt_send_packet(ping, 2);
            pthread_mutex_unlock(&g_mqtt_mutex);
            last_ping = now;
            ha_mqtt_publish_state();
        }

        usleep(500000); // 500ms
    }
    return NULL;
}

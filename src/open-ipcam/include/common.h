#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define OPEN_IPCAM_VERSION "1.0.0"

#define LOG_INFO(fmt, ...)  fprintf(stdout, "[INFO] [%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  fprintf(stderr, "[WARN] [%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] [%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

typedef enum {
    NIGHT_MODE_AUTO = 0,
    NIGHT_MODE_DAY,
    NIGHT_MODE_IR,
    NIGHT_MODE_COLOR
} night_mode_t;

typedef struct {
    int http_port;
    int rtsp_port;
    char rtsp_username[64];
    char rtsp_password[64];
    char rtsp_main_path[64];
    char rtsp_sub_path[64];

    // MQTT Settings for Home Assistant
    bool mqtt_enabled;
    char mqtt_broker[128];
    int mqtt_port;
    char mqtt_username[64];
    char mqtt_password[64];
    char mqtt_topic_prefix[64];

    // Hardware defaults
    int default_motor_speed;
    int motor_step_size;
    night_mode_t default_night_mode;
} app_config_t;

typedef struct {
    int pos_x;
    int pos_y;
    bool is_moving;
    night_mode_t night_mode;
    bool ircut_state;
    bool ir_led_state;
    int white_led_brightness;
    bool siren_state;
    time_t start_time;
    uint32_t rtsp_clients_count;
} app_state_t;

extern app_config_t g_config;
extern app_state_t g_state;
extern pthread_mutex_t g_state_mutex;
extern volatile bool g_running;

#endif // COMMON_H

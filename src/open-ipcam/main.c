#include "common.h"
#include "motor_ctrl.h"
#include "gpio_ctrl.h"
#include "rtsp_server.h"
#include "http_server.h"
#include "ha_mqtt.h"
#include "wifi_mgr.h"

app_config_t g_config;
app_state_t g_state;
pthread_mutex_t g_state_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile bool g_running = true;

static void handle_signal(int sig) {
    LOG_INFO("Received signal %d, shutting down gracefully...", sig);
    g_running = false;
}

static void load_config_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            // Trim whitespace
            if (strcmp(key, "http_port") == 0) g_config.http_port = atoi(val);
            else if (strcmp(key, "rtsp_port") == 0) g_config.rtsp_port = atoi(val);
            else if (strcmp(key, "motor_speed") == 0) g_config.default_motor_speed = atoi(val);
            else if (strcmp(key, "motor_step") == 0) g_config.motor_step_size = atoi(val);
            else if (strcmp(key, "mqtt_enabled") == 0) g_config.mqtt_enabled = (atoi(val) == 1 || strcmp(val, "true") == 0);
            else if (strcmp(key, "mqtt_broker") == 0) snprintf(g_config.mqtt_broker, sizeof(g_config.mqtt_broker), "%s", val);
            else if (strcmp(key, "mqtt_port") == 0) g_config.mqtt_port = atoi(val);
            else if (strcmp(key, "mqtt_user") == 0) snprintf(g_config.mqtt_username, sizeof(g_config.mqtt_username), "%s", val);
            else if (strcmp(key, "mqtt_pass") == 0) snprintf(g_config.mqtt_password, sizeof(g_config.mqtt_password), "%s", val);
        }
    }
    fclose(f);
    LOG_INFO("Configuration loaded from %s", path);
}

static void set_default_config(void) {
    memset(&g_config, 0, sizeof(g_config));
    g_config.http_port = 8080;
    g_config.rtsp_port = 554;
    strcpy(g_config.rtsp_main_path, "/live/main");
    strcpy(g_config.rtsp_sub_path, "/live/sub");
    g_config.default_motor_speed = 5;
    g_config.motor_step_size = 100;
    g_config.default_night_mode = NIGHT_MODE_DAY;

    g_config.mqtt_enabled = false;
    strcpy(g_config.mqtt_broker, "192.168.15.100");
    g_config.mqtt_port = 1883;

    // Check config locations
    load_config_file("/userdata/Config/open-ipcam.conf");
    load_config_file("/media/FastIoT_factoryTest/open-ipcam.conf");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    printf("====================================================\n");
    printf("   Open-IPCam v%s (V821B / Tina Linux RISC-V 32)     \n", OPEN_IPCAM_VERSION);
    printf("   100%% Local, Zero-Cloud, Home Assistant Native     \n");
    printf("====================================================\n");

    set_default_config();

    memset(&g_state, 0, sizeof(g_state));
    g_state.start_time = time(NULL);

    LOG_INFO("Initializing Hardware Subsystems...");
    gpio_init_all();
    motor_init();

    LOG_INFO("Initializing Wi-Fi Subsystem...");
    wifi_mgr_init();

    LOG_INFO("Starting Network Services...");
    rtsp_server_init(g_config.rtsp_port);
    http_server_init(g_config.http_port);
    ha_mqtt_init();

    pthread_t th_rtsp, th_http, th_mqtt, th_wifi;
    pthread_create(&th_wifi, NULL, wifi_mgr_thread, NULL);
    pthread_create(&th_rtsp, NULL, rtsp_server_thread, NULL);
    pthread_create(&th_http, NULL, http_server_thread, NULL);
    if (g_config.mqtt_enabled) {
        pthread_create(&th_mqtt, NULL, ha_mqtt_thread, NULL);
    }

    LOG_INFO("Open-IPCam is running!");
    LOG_INFO("Web Dashboard: http://0.0.0.0:%d", g_config.http_port);
    LOG_INFO("RTSP Stream:   rtsp://0.0.0.0:%d%s", g_config.rtsp_port, g_config.rtsp_main_path);

    while (g_running) {
        sleep(1);
    }

    LOG_INFO("Stopping services...");
    wifi_mgr_stop();
    rtsp_server_stop();
    http_server_stop();
    ha_mqtt_stop();

    pthread_join(th_wifi, NULL);
    pthread_join(th_rtsp, NULL);
    pthread_join(th_http, NULL);
    if (g_config.mqtt_enabled) {
        pthread_join(th_mqtt, NULL);
    }

    motor_cleanup();
    gpio_cleanup_all();

    LOG_INFO("Open-IPCam exited cleanly.");
    return 0;
}

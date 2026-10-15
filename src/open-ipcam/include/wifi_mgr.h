#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include "common.h"

#define WIFI_AP_DEFAULT_SSID "OpenIPCam-Setup"
#define WIFI_USERDATA_CONF   "/userdata/Config/wifi.conf"
#define WIFI_SDCARD_CONF     "/media/FastIoT_factoryTest/wifi.conf"
#define WPA_CONF_FILE        "/userdata/Config/wpa_supplicant.conf"

typedef enum {
    WIFI_STATE_UNCONFIGURED = 0,
    WIFI_STATE_AP_MODE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED_STA
} wifi_state_t;

typedef struct {
    char ssid[64];
    int signal;
    char encryption[32];
} wifi_scan_result_t;

int wifi_mgr_init(void);
void wifi_mgr_stop(void);
void *wifi_mgr_thread(void *arg);

wifi_state_t wifi_mgr_get_state(void);
int wifi_mgr_get_ip(char *out_ip, size_t max_len);
int wifi_mgr_get_ssid(char *out_ssid, size_t max_len);

// Scanning available networks for Web UI dropdown
int wifi_mgr_scan(wifi_scan_result_t *results, int max_results);

// Connect to user's home Wi-Fi (shuts down AP, connects STA)
int wifi_mgr_configure_and_connect(const char *ssid, const char *password);

// Fallback: reset to AP mode (OpenIPCam-Setup)
int wifi_mgr_start_ap_mode(void);

#endif // WIFI_MGR_H

#include "wifi_mgr.h"
#include "gpio_ctrl.h"
#include <net/if.h>

static wifi_state_t g_wifi_state = WIFI_STATE_UNCONFIGURED;
static char g_current_ip[32] = "0.0.0.0";
static char g_current_ssid[64] = "";
static pthread_mutex_t g_wifi_mutex = PTHREAD_MUTEX_INITIALIZER;

static int run_command_silent(const char *cmd) {
    LOG_INFO("Exec: %s", cmd);
    return system(cmd);
}

wifi_state_t wifi_mgr_get_state(void) {
    pthread_mutex_lock(&g_wifi_mutex);
    wifi_state_t s = g_wifi_state;
    pthread_mutex_unlock(&g_wifi_mutex);
    return s;
}

int wifi_mgr_get_ip(char *out_ip, size_t max_len) {
    pthread_mutex_lock(&g_wifi_mutex);
    strncpy(out_ip, g_current_ip, max_len - 1);
    out_ip[max_len - 1] = '\0';
    pthread_mutex_unlock(&g_wifi_mutex);
    return 0;
}

int wifi_mgr_get_ssid(char *out_ssid, size_t max_len) {
    pthread_mutex_lock(&g_wifi_mutex);
    strncpy(out_ssid, g_current_ssid, max_len - 1);
    out_ssid[max_len - 1] = '\0';
    pthread_mutex_unlock(&g_wifi_mutex);
    return 0;
}

static void update_current_ip(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;

    struct ifreq ifr;
    strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(fd, SIOCGIFADDR, &ifr) >= 0) {
        struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
        char ip_buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ip_buf, sizeof(ip_buf));

        pthread_mutex_lock(&g_wifi_mutex);
        strncpy(g_current_ip, ip_buf, sizeof(g_current_ip) - 1);
        pthread_mutex_unlock(&g_wifi_mutex);
    }
    close(fd);
}

int wifi_mgr_start_ap_mode(void) {
    pthread_mutex_lock(&g_wifi_mutex);
    g_wifi_state = WIFI_STATE_AP_MODE;
    strncpy(g_current_ssid, WIFI_AP_DEFAULT_SSID, sizeof(g_current_ssid) - 1);
    strncpy(g_current_ip, "192.168.4.1", sizeof(g_current_ip) - 1);
    pthread_mutex_unlock(&g_wifi_mutex);

    LOG_INFO(">>> ACTIVATING AP PROVISIONING MODE (SSID: %s) <<<", WIFI_AP_DEFAULT_SSID);

    // 1. Tear down any active station connections
    run_command_silent("killall -9 udhcpc wpa_supplicant 2>/dev/null");
    run_command_silent("wifi -f sta 2>/dev/null");
    sleep(1);

    // 2. Start AP mode using Allwinner wifimanager
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "wifi -o ap %s", WIFI_AP_DEFAULT_SSID);
    run_command_silent(cmd);

    // 3. Configure static IP 192.168.4.1 on wlan0
    run_command_silent("ifconfig wlan0 192.168.4.1 netmask 255.255.255.0 up");

    // 4. Create transient udhcpd config if missing
    FILE *f = fopen("/tmp/udhcpd_ap.conf", "w");
    if (f) {
        fprintf(f,
            "start 192.168.4.100\n"
            "end 192.168.4.200\n"
            "interface wlan0\n"
            "opt subnet 255.255.255.0\n"
            "opt router 192.168.4.1\n"
            "opt dns 192.168.4.1\n"
            "opt lease 86400\n");
        fclose(f);
        run_command_silent("udhcpd /tmp/udhcpd_ap.conf &");
    }

    LOG_INFO("AP Mode active: connect to '%s' and open http://192.168.4.1:8080", WIFI_AP_DEFAULT_SSID);
    return 0;
}

int wifi_mgr_configure_and_connect(const char *ssid, const char *password) {
    if (!ssid || strlen(ssid) == 0) return -1;

    LOG_INFO("Configuring Wi-Fi for SSID: '%s'...", ssid);

    pthread_mutex_lock(&g_wifi_mutex);
    g_wifi_state = WIFI_STATE_CONNECTING;
    strncpy(g_current_ssid, ssid, sizeof(g_current_ssid) - 1);
    pthread_mutex_unlock(&g_wifi_mutex);

    // 1. Ensure persistent config folder exists on NOR flash (/userdata is JFFS2)
    run_command_silent("mkdir -p /userdata/Config 2>/dev/null");

    // 2. Save credentials to permanent flash storage (/userdata/Config/wifi.conf)
    FILE *f = fopen(WIFI_USERDATA_CONF, "w");
    if (f) {
        fprintf(f, "ssid=%s\npassword=%s\n", ssid, password ? password : "");
        fclose(f);
        LOG_INFO("Saved Wi-Fi configuration to permanent flash: %s", WIFI_USERDATA_CONF);
    } else {
        LOG_ERROR("Failed to write to %s: %s", WIFI_USERDATA_CONF, strerror(errno));
    }

    // Also mirror to MicroSD if available
    f = fopen(WIFI_SDCARD_CONF, "w");
    if (f) {
        fprintf(f, "ssid=%s\npassword=%s\n", ssid, password ? password : "");
        fclose(f);
    }

    // 3. Write standard wpa_supplicant.conf
    f = fopen(WPA_CONF_FILE, "w");
    if (f) {
        fprintf(f,
            "ctrl_interface=/var/run/wpa_supplicant\n"
            "update_config=1\n\n"
            "network={\n"
            "    ssid=\"%s\"\n", ssid);
        if (password && strlen(password) > 0) {
            fprintf(f, "    psk=\"%s\"\n", password);
        } else {
            fprintf(f, "    key_mgmt=NONE\n");
        }
        fprintf(f, "}\n");
        fclose(f);
    }

    // Flush all filesystem write buffers to physical NOR Flash immediately!
    sync();

    // 4. Extinguish AP mode completely (kill udhcpd and close AP)
    LOG_INFO("Closing AP mode network '%s'...", WIFI_AP_DEFAULT_SSID);
    run_command_silent("killall -9 udhcpd 2>/dev/null");
    run_command_silent("wifi -f ap 2>/dev/null");
    run_command_silent("ifconfig wlan0 0.0.0.0");
    sleep(1);

    // 4. Start Station mode and connect
    LOG_INFO("Connecting as Station to '%s'...", ssid);
    run_command_silent("wifi -o sta");
    sleep(1);

    char connect_cmd[256];
    if (password && strlen(password) > 0) {
        snprintf(connect_cmd, sizeof(connect_cmd), "wifi -c \"%s\" \"%s\"", ssid, password);
    } else {
        snprintf(connect_cmd, sizeof(connect_cmd), "wifi -c \"%s\"", ssid);
    }
    run_command_silent(connect_cmd);

    // 5. Run udhcpc to obtain IP from home router
    run_command_silent("udhcpc -i wlan0 -b -s /usr/share/udhcpc/default.script &");

    // Poll for IP address up to 30 seconds
    bool got_ip = false;
    for (int i = 0; i < 30; i++) {
        sleep(1);
        update_current_ip();
        pthread_mutex_lock(&g_wifi_mutex);
        if (strcmp(g_current_ip, "0.0.0.0") != 0 && strncmp(g_current_ip, "192.168.4.", 10) != 0) {
            got_ip = true;
            g_wifi_state = WIFI_STATE_CONNECTED_STA;
            pthread_mutex_unlock(&g_wifi_mutex);
            break;
        }
        pthread_mutex_unlock(&g_wifi_mutex);
    }

    if (got_ip) {
        LOG_INFO(">>> Successfully connected to '%s'! IP: %s (AP mode disabled) <<<", ssid, g_current_ip);
        return 0;
    } else {
        LOG_WARN("Could not connect to '%s' or obtain IP. Falling back to AP mode...", ssid);
        wifi_mgr_start_ap_mode();
        return -1;
    }
}

int wifi_mgr_scan(wifi_scan_result_t *results, int max_results) {
    if (!results || max_results <= 0) return 0;

    // Trigger scan via wpa_cli or wifi -s
    run_command_silent("wpa_cli -i wlan0 scan 2>/dev/null");
    usleep(800000); // 800ms wait for scan completion

    FILE *pipe = popen("wpa_cli -i wlan0 scan_results 2>/dev/null", "r");
    if (!pipe) {
        // Fallback simulation/default list for testing
        strncpy(results[0].ssid, "MinhaRede_Casa", sizeof(results[0].ssid));
        results[0].signal = -55;
        strcpy(results[0].encryption, "WPA2-PSK");
        return 1;
    }

    char line[256];
    int count = 0;
    // Skip header line (bssid / frequency / signal level / flags / ssid)
    if (fgets(line, sizeof(line), pipe)) {
        while (fgets(line, sizeof(line), pipe) && count < max_results) {
            char bssid[32], flags[64], ssid[64];
            int freq, signal;
            if (sscanf(line, "%31s %d %d %63s %63[^\n\r]", bssid, &freq, &signal, flags, ssid) == 5) {
                // Ignore hidden SSIDs
                if (strlen(ssid) == 0 || strcmp(ssid, "\\x00") == 0) continue;
                snprintf(results[count].ssid, sizeof(results[count].ssid), "%s", ssid);
                results[count].signal = signal;
                if (strstr(flags, "WPA2")) strcpy(results[count].encryption, "WPA2");
                else if (strstr(flags, "WPA")) strcpy(results[count].encryption, "WPA");
                else strcpy(results[count].encryption, "OPEN");
                count++;
            }
        }
    }
    pclose(pipe);
    return count;
}

int wifi_mgr_init(void) {
    // Check if wifi.conf exists in permanent flash first (/userdata/Config/wifi.conf)
    FILE *f = fopen(WIFI_USERDATA_CONF, "r");
    if (!f) {
        // Fallback to SD card
        f = fopen(WIFI_SDCARD_CONF, "r");
    }

    if (f) {
        char line[256];
        char ssid[64] = "", pass[64] = "";
        while (fgets(line, sizeof(line), f)) {
            sscanf(line, "ssid=%63[^\n\r]", ssid);
            sscanf(line, "password=%63[^\n\r]", pass);
        }
        fclose(f);

        if (strlen(ssid) > 0) {
            LOG_INFO("Saved Wi-Fi configuration found: SSID='%s'. Connecting...", ssid);
            return wifi_mgr_configure_and_connect(ssid, pass);
        }
    }

    // No configuration found -> start in AP Provisioning Mode
    LOG_INFO("No saved Wi-Fi found. Starting initial setup AP mode...");
    return wifi_mgr_start_ap_mode();
}

void wifi_mgr_stop(void) {
    LOG_INFO("Stopping WiFi Manager...");
}

void *wifi_mgr_thread(void *arg) {
    (void)arg;
    int rst_press_seconds = 0;

    while (g_running) {
        sleep(1);

        // 1. Periodic IP check in station mode
        if (wifi_mgr_get_state() == WIFI_STATE_CONNECTED_STA) {
            update_current_ip();
        }

        // 2. Monitor Reset button (GPIO 102). Active LOW or HIGH depending on pull
        int rst_val = gpio_get_value(GPIO_RST_BTN);
        if (rst_val == 0) { // Pressed (pulled to ground)
            rst_press_seconds++;
            if (rst_press_seconds >= 3) {
                LOG_WARN("Reset button held for 3 seconds! Resetting to AP Provisioning Mode...");
                unlink(WIFI_USERDATA_CONF);
                unlink(WIFI_SDCARD_CONF);
                unlink(WPA_CONF_FILE);
                sync();
                wifi_mgr_start_ap_mode();
                rst_press_seconds = 0;
            }
        } else {
            rst_press_seconds = 0;
        }
    }
    return NULL;
}

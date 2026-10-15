#include "http_server.h"
#include "motor_ctrl.h"
#include "gpio_ctrl.h"
#include "wifi_mgr.h"

#define HTTP_BUF_SIZE 4096

static int g_http_server_fd = -1;

static const char HTML_DASHBOARD[] =
"<!DOCTYPE html>\n"
"<html lang=\"pt-BR\">\n"
"<head>\n"
"    <meta charset=\"utf-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>Open-IPCam Control Center</title>\n"
"    <style>\n"
"        :root { --bg: #121214; --card: #202024; --accent: #04d361; --text: #e1e1e6; --sub: #8d8d99; --btn: #29292e; --btn-hover: #323238; --danger: #e83f5b; --warn: #ff9000; }\n"
"        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; display: flex; justify-content: center; }\n"
"        .container { max-width: 540px; width: 100%; display: flex; flex-direction: column; gap: 16px; }\n"
"        .card { background: var(--card); border-radius: 12px; padding: 20px; box-shadow: 0 8px 24px rgba(0,0,0,0.5); }\n"
"        h1 { margin: 0 0 4px 0; font-size: 20px; color: var(--accent); display: flex; align-items: center; justify-content: space-between; }\n"
"        .version { font-size: 11px; color: var(--sub); font-weight: normal; }\n"
"        .badge { background: rgba(4,211,97,0.15); color: var(--accent); padding: 4px 10px; border-radius: 20px; font-size: 12px; font-weight: 600; }\n"
"        .badge-warn { background: rgba(255,144,0,0.15); color: var(--warn); }\n"
"        .alert-box { padding: 12px; border-radius: 8px; margin-top: 10px; font-size: 13px; line-height: 1.4; }\n"
"        .alert-ap { background: rgba(255,144,0,0.1); border: 1px solid var(--warn); color: #ffc069; }\n"
"        .alert-sta { background: rgba(4,211,97,0.1); border: 1px solid var(--accent); color: #85e89d; }\n"
"        .status-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; margin-top: 12px; font-size: 13px; }\n"
"        .stat-item { background: var(--bg); padding: 10px; border-radius: 8px; }\n"
"        .stat-label { color: var(--sub); font-size: 11px; text-transform: uppercase; margin-bottom: 2px; }\n"
"        .stat-val { font-weight: bold; font-family: monospace; font-size: 14px; }\n"
"        .dpad { display: grid; grid-template-columns: repeat(3, 72px); gap: 10px; justify-content: center; margin: 15px auto; }\n"
"        button { background: var(--btn); border: 1px solid var(--btn-hover); color: #fff; font-size: 22px; height: 60px; border-radius: 10px; cursor: pointer; transition: 0.15s; display: flex; align-items: center; justify-content: center; user-select: none; }\n"
"        button:hover { background: var(--accent); color: #000; border-color: var(--accent); transform: translateY(-1px); }\n"
"        button:active { transform: scale(0.95); }\n"
"        .btn-ctl { height: 42px; font-size: 14px; font-weight: 500; border-radius: 8px; padding: 0 14px; }\n"
"        .btn-primary { background: var(--accent); color: #000; font-weight: bold; border-color: var(--accent); }\n"
"        .btn-primary:hover { background: #03b252; }\n"
"        .btn-danger { background: rgba(232,63,91,0.15); color: var(--danger); border-color: var(--danger); }\n"
"        .btn-danger:hover { background: var(--danger); color: #fff; }\n"
"        .ctl-group { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 10px; }\n"
"        .blank { visibility: hidden; }\n"
"        .section-title { font-size: 14px; font-weight: 600; color: var(--text); margin-top: 10px; margin-bottom: 6px; }\n"
"        .stream-info { background: var(--bg); padding: 12px; border-radius: 8px; font-size: 12px; word-break: break-all; margin-top: 10px; }\n"
"        .stream-info code { color: #996dff; font-family: monospace; }\n"
"        input, select { background: var(--bg); border: 1px solid var(--btn-hover); color: #fff; padding: 10px; border-radius: 8px; width: 100%; box-sizing: border-box; font-size: 14px; margin-top: 4px; margin-bottom: 10px; }\n"
"        input:focus, select:focus { border-color: var(--accent); outline: none; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <div class=\"card\">\n"
"            <h1>Open-IPCam <span id=\"net-badge\" class=\"badge\">100% Local</span></h1>\n"
"            <div id=\"net-alert\" class=\"alert-box alert-ap\">\n"
"                Verificando status de rede...\n"
"            </div>\n"
"            <div class=\"status-grid\">\n"
"                <div class=\"stat-item\"><div class=\"stat-label\">IP da Camera</div><div id=\"st-ip\" class=\"stat-val\">192.168.4.1</div></div>\n"
"                <div class=\"stat-item\"><div class=\"stat-label\">Rede Wi-Fi</div><div id=\"st-ssid\" class=\"stat-val\">OpenIPCam-Setup</div></div>\n"
"                <div class=\"stat-item\"><div class=\"stat-label\">Posicao PTZ</div><div id=\"st-pos\" class=\"stat-val\">X: 0 | Y: 0</div></div>\n"
"                <div class=\"stat-item\"><div class=\"stat-label\">Uptime</div><div id=\"st-uptime\" class=\"stat-val\">0s</div></div>\n"
"            </div>\n"
"        </div>\n"
"\n"
"        <!-- Configuracao de Wi-Fi -->\n"
"        <div class=\"card\">\n"
"            <div class=\"section-title\">Configurar Rede Wi-Fi</div>\n"
"            <div style=\"display:flex; gap:8px; margin-top:6px; margin-bottom:4px;\">\n"
"                <button class=\"btn-ctl\" style=\"flex:1;\" onclick=\"scanWifi()\">&#x1F504; Buscar Redes Wi-Fi</button>\n"
"            </div>\n"
"            <label style=\"font-size:12px; color:var(--sub);\">Selecione a Rede (SSID):</label>\n"
"            <select id=\"wifi-select\" onchange=\"onSelectNetwork()\">\n"
"                <option value=\"\">-- Clique em Buscar Redes ou digite abaixo --</option>\n"
"            </select>\n"
"            <label style=\"font-size:12px; color:var(--sub);\">Ou digite o nome da rede:</label>\n"
"            <input type=\"text\" id=\"wifi-ssid\" placeholder=\"Ex: MinhaCasa_2.4G\">\n"
"            <label style=\"font-size:12px; color:var(--sub);\">Senha do Wi-Fi:</label>\n"
"            <input type=\"password\" id=\"wifi-pass\" placeholder=\"Senha da sua rede\">\n"
"            <div class=\"ctl-group\">\n"
"                <button id=\"btn-connect\" class=\"btn-ctl btn-primary\" style=\"flex:2;\" onclick=\"connectWifi()\">&#x1F4BE; Salvar e Conectar</button>\n"
"                <button class=\"btn-ctl btn-danger\" style=\"flex:1;\" onclick=\"resetWifi()\">&#x26A0;&#xFE0F; Reset AP</button>\n"
"            </div>\n"
"            <p style=\"font-size:11px; color:var(--sub); margin-top:8px; margin-bottom:0;\">\n"
"                * Ao conectar com sucesso, a rede <code>OpenIPCam-Setup</code> sera desligada automaticamente e a camera passara a ser acessivel exclusivamente pelo IP atribuido pelo seu roteador.\n"
"            </p>\n"
"        </div>\n"
"\n"
"        <!-- Controle PTZ -->\n"
"        <div class=\"card\" style=\"text-align: center;\">\n"
"            <div class=\"section-title\">Controle de Movimento (PTZ)</div>\n"
"            <div class=\"dpad\">\n"
"                <button class=\"blank\"></button>\n"
"                <button onclick=\"ptz('u')\" title=\"Cima\">&#x2B06;&#xFE0F;</button>\n"
"                <button class=\"blank\"></button>\n"
"\n"
"                <button onclick=\"ptz('l')\" title=\"Esquerda\">&#x2B05;&#xFE0F;</button>\n"
"                <button onclick=\"ptz('s')\" title=\"Parar\" style=\"font-size:16px;\">&#x23F9;&#xFE0F;</button>\n"
"                <button onclick=\"ptz('r')\" title=\"Direita\">&#x27A1;&#xFE0F;</button>\n"
"\n"
"                <button class=\"blank\"></button>\n"
"                <button onclick=\"ptz('d')\" title=\"Baixo\">&#x2B07;&#xFE0F;</button>\n"
"                <button class=\"blank\"></button>\n"
"            </div>\n"
"            <div class=\"ctl-group\" style=\"justify-content: center;\">\n"
"                <button class=\"btn-ctl\" onclick=\"ptz('h')\">&#x1F3E0; Centralizar (Home)</button>\n"
"            </div>\n"
"        </div>\n"
"\n"
"        <!-- Iluminacao -->\n"
"        <div class=\"card\">\n"
"            <div class=\"section-title\">Iluminacao & Visao Noturna</div>\n"
"            <div class=\"ctl-group\">\n"
"                <button class=\"btn-ctl\" onclick=\"setNight('day')\">&#x2600;&#xFE0F; Dia (IRCUT)</button>\n"
"                <button class=\"btn-ctl\" onclick=\"setNight('ir')\">&#x1F319; Noite IR</button>\n"
"                <button class=\"btn-ctl\" onclick=\"setNight('color')\">&#x1F4A1; Holofote</button>\n"
"            </div>\n"
"            <div class=\"ctl-group\" style=\"margin-top: 10px;\">\n"
"                <button class=\"btn-ctl\" onclick=\"setSiren()\">&#x1F6A8; Sirene / Alarme</button>\n"
"            </div>\n"
"        </div>\n"
"\n"
"        <!-- Streams -->\n"
"        <div class=\"card\">\n"
"            <div class=\"section-title\">Integracao Home Assistant / RTSP</div>\n"
"            <div class=\"stream-info\">\n"
"                <strong>Stream Principal (1080P):</strong><br>\n"
"                <code id=\"urlMain\">rtsp://admin:Ztac@2021@IP:554/live/camid_0/main</code><br><br>\n"
"                <strong>Stream Secundario (360P):</strong><br>\n"
"                <code id=\"urlSub\">rtsp://admin:Ztac@2021@IP:554/live/camid_0/sub</code>\n"
"            </div>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <script>\n"
"        const host = window.location.hostname;\n"
"        document.getElementById('urlMain').innerText = 'rtsp://admin:Ztac@2021@' + host + ':554/live/camid_0/main';\n"
"        document.getElementById('urlSub').innerText = 'rtsp://admin:Ztac@2021@' + host + ':554/live/camid_0/sub';\n"
"\n"
"        function scanWifi() {\n"
"            const sel = document.getElementById('wifi-select');\n"
"            sel.innerHTML = '<option>Buscando redes...</option>';\n"
"            fetch('/api/wifi/scan')\n"
"                .then(r => r.json())\n"
"                .then(nets => {\n"
"                    sel.innerHTML = '<option value=\"\">-- Selecione uma rede encontrada --</option>';\n"
"                    nets.forEach(n => {\n"
"                        const opt = document.createElement('option');\n"
"                        opt.value = n.ssid;\n"
"                        opt.innerText = n.ssid + ' (' + n.signal + 'dBm ' + n.encryption + ')';\n"
"                        sel.appendChild(opt);\n"
"                    });\n"
"                })\n"
"                .catch(e => { sel.innerHTML = '<option>Erro ao buscar redes</option>'; });\n"
"        }\n"
"\n"
"        function onSelectNetwork() {\n"
"            const sel = document.getElementById('wifi-select');\n"
"            if (sel.value) document.getElementById('wifi-ssid').value = sel.value;\n"
"        }\n"
"\n"
"        function connectWifi() {\n"
"            const ssid = document.getElementById('wifi-ssid').value.trim();\n"
"            const pass = document.getElementById('wifi-pass').value;\n"
"            if (!ssid) { alert('Digite ou selecione o SSID da rede!'); return; }\n"
"            const btn = document.getElementById('btn-connect');\n"
"            btn.disabled = true;\n"
"            btn.innerText = 'Conectando ao Wi-Fi...';\n"
"            fetch('/api/wifi/setup?ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(pass), {method:'POST'})\n"
"                .then(r => r.json())\n"
"                .then(d => {\n"
"                    alert('Configuracao enviada! A camera agora esta se conectando a ' + ssid + '. A rede OpenIPCam-Setup sera desligada e ela estara acessivel no IP do seu roteador.');\n"
"                })\n"
"                .catch(e => alert('Enviado! Conectando...'));\n"
"        }\n"
"\n"
"        function resetWifi() {\n"
"            if (!confirm('Deseja resetar a camera para o modo de configuracao (OpenIPCam-Setup)?')) return;\n"
"            fetch('/api/wifi/reset', {method:'POST'}).then(() => alert('Camera resetada para o modo AP!'));\n"
"        }\n"
"\n"
"        function ptz(dir) {\n"
"            fetch('/api/ptz?dir=' + dir, {method:'POST'}).then(updateStatus);\n"
"        }\n"
"        function setNight(mode) {\n"
"            fetch('/api/nightvision?mode=' + mode, {method:'POST'}).then(updateStatus);\n"
"        }\n"
"        let sirenActive = false;\n"
"        function setSiren() {\n"
"            sirenActive = !sirenActive;\n"
"            fetch('/api/siren?state=' + (sirenActive ? 'on':'off'), {method:'POST'}).then(updateStatus);\n"
"        }\n"
"        function updateStatus() {\n"
"            fetch('/api/status').then(r=>r.json()).then(d=>{\n"
"                document.getElementById('st-pos').innerText = 'X: ' + d.pos_x + ' | Y: ' + d.pos_y;\n"
"                document.getElementById('st-uptime').innerText = d.uptime_sec + 's';\n"
"                document.getElementById('st-ip').innerText = d.ip;\n"
"                document.getElementById('st-ssid').innerText = d.ssid || 'Nao conectada';\n"
"                const alert = document.getElementById('net-alert');\n"
"                const badge = document.getElementById('net-badge');\n"
"                if (d.wifi_state === 'AP_MODE') {\n"
"                    alert.className = 'alert-box alert-ap';\n"
"                    alert.innerHTML = '&#x26A0;&#xFE0F; <strong>Modo de Configuracao Ativo</strong>: Conectado a rede própria <code>OpenIPCam-Setup</code>. Configure sua rede abaixo para que ela entre na sua LAN e desligue o Wi-Fi próprio.';\n"
"                    badge.className = 'badge badge-warn';\n"
"                    badge.innerText = 'Modo Setup AP';\n"
"                } else {\n"
"                    alert.className = 'alert-box alert-sta';\n"
"                    alert.innerHTML = '&#x2705; <strong>Conectada na sua rede</strong>: ' + d.ssid + ' (IP: <code>' + d.ip + '</code>). O Wi-Fi próprio foi desligado.';\n"
"                    badge.className = 'badge';\n"
"                    badge.innerText = 'Conectada';\n"
"                }\n"
"            }).catch(e=>{});\n"
"        }\n"
"        setInterval(updateStatus, 3000);\n"
"        updateStatus();\n"
"    </script>\n"
"</body>\n"
"</html>\n";

static void send_http_response(int fd, int code, const char *content_type, const char *body, size_t body_len) {
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Server: open-ipcam/%s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        code, OPEN_IPCAM_VERSION, content_type, body_len);

    send(fd, header, hlen, 0);
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}

static void handle_api_status(int fd) {
    pthread_mutex_lock(&g_state_mutex);
    int px = g_state.pos_x;
    int py = g_state.pos_y;
    bool moving = g_state.is_moving;
    night_mode_t nm = g_state.night_mode;
    bool ircut = g_state.ircut_state;
    bool ir_led = g_state.ir_led_state;
    int wled = g_state.white_led_brightness;
    bool siren = g_state.siren_state;
    uint32_t clients = g_state.rtsp_clients_count;
    time_t uptime = time(NULL) - g_state.start_time;
    pthread_mutex_unlock(&g_state_mutex);

    char ip[32] = "0.0.0.0", ssid[64] = "";
    wifi_mgr_get_ip(ip, sizeof(ip));
    wifi_mgr_get_ssid(ssid, sizeof(ssid));
    wifi_state_t ws = wifi_mgr_get_state();

    const char *ws_str = "DISCONNECTED";
    if (ws == WIFI_STATE_AP_MODE) ws_str = "AP_MODE";
    else if (ws == WIFI_STATE_CONNECTING) ws_str = "CONNECTING";
    else if (ws == WIFI_STATE_CONNECTED_STA) ws_str = "CONNECTED_STA";

    const char *nm_str = "AUTO";
    if (nm == NIGHT_MODE_DAY) nm_str = "DAY";
    else if (nm == NIGHT_MODE_IR) nm_str = "NIGHT_IR";
    else if (nm == NIGHT_MODE_COLOR) nm_str = "NIGHT_COLOR";

    char json[1024];
    int len = snprintf(json, sizeof(json),
        "{\n"
        "  \"pos_x\": %d,\n"
        "  \"pos_y\": %d,\n"
        "  \"is_moving\": %s,\n"
        "  \"night_mode\": \"%s\",\n"
        "  \"ircut_day_filter\": %s,\n"
        "  \"ir_led\": %s,\n"
        "  \"white_led_pct\": %d,\n"
        "  \"siren\": %s,\n"
        "  \"rtsp_clients\": %u,\n"
        "  \"wifi_state\": \"%s\",\n"
        "  \"ip\": \"%s\",\n"
        "  \"ssid\": \"%s\",\n"
        "  \"uptime_sec\": %ld,\n"
        "  \"version\": \"%s\"\n"
        "}\n",
        px, py, moving ? "true" : "false", nm_str,
        ircut ? "true" : "false", ir_led ? "true" : "false",
        wled, siren ? "true" : "false", clients,
        ws_str, ip, ssid, (long)uptime, OPEN_IPCAM_VERSION);

    send_http_response(fd, 200, "application/json", json, len);
}

static void handle_api_wifi_scan(int fd) {
    wifi_scan_result_t results[20];
    int count = wifi_mgr_scan(results, 20);

    char json[2048] = "[";
    size_t off = 1;
    for (int i = 0; i < count; i++) {
        char item[128];
        int ilen = snprintf(item, sizeof(item),
            "%s{\"ssid\":\"%s\",\"signal\":%d,\"encryption\":\"%s\"}",
            i > 0 ? "," : "", results[i].ssid, results[i].signal, results[i].encryption);
        if (off + ilen < sizeof(json) - 2) {
            strcat(json, item);
            off += ilen;
        }
    }
    strcat(json, "]\n");
    send_http_response(fd, 200, "application/json", json, strlen(json));
}

static void handle_api_wifi_setup(int fd, const char *query) {
    char ssid[64] = "", pass[64] = "";
    const char *s = strstr(query, "ssid=");
    if (s) {
        s += 5;
        int i = 0;
        while (*s && *s != '&' && i < 63) {
            if (*s == '%' && *(s+1) && *(s+2)) {
                // simple url decode
                int val = 0;
                sscanf(s + 1, "%2x", &val);
                ssid[i++] = (char)val;
                s += 3;
            } else if (*s == '+') {
                ssid[i++] = ' ';
                s++;
            } else {
                ssid[i++] = *s++;
            }
        }
        ssid[i] = '\0';
    }

    const char *p = strstr(query, "password=");
    if (p) {
        p += 9;
        int i = 0;
        while (*p && *p != '&' && i < 63) {
            if (*p == '%' && *(p+1) && *(p+2)) {
                int val = 0;
                sscanf(p + 1, "%2x", &val);
                pass[i++] = (char)val;
                p += 3;
            } else if (*p == '+') {
                pass[i++] = ' ';
                p++;
            } else {
                pass[i++] = *p++;
            }
        }
        pass[i] = '\0';
    }

    const char resp[] = "{\"status\":\"connecting\"}\n";
    send_http_response(fd, 200, "application/json", resp, strlen(resp));

    if (strlen(ssid) > 0) {
        wifi_mgr_configure_and_connect(ssid, pass);
    }
}

static void handle_api_wifi_reset(int fd) {
    unlink(WIFI_USERDATA_CONF);
    unlink(WIFI_SDCARD_CONF);
    unlink(WPA_CONF_FILE);
    sync();
    wifi_mgr_start_ap_mode();
    const char resp[] = "{\"status\":\"reset_to_ap\"}\n";
    send_http_response(fd, 200, "application/json", resp, strlen(resp));
}

static void handle_api_ptz(int fd, const char *query) {
    char dir = 's';
    int steps = g_config.motor_step_size > 0 ? g_config.motor_step_size : 100;

    const char *d = strstr(query, "dir=");
    if (d) dir = d[4];

    const char *st = strstr(query, "steps=");
    if (st) steps = atoi(st + 6);

    motor_move((motor_direction_t)dir, steps);

    const char resp[] = "{\"status\":\"ok\"}\n";
    send_http_response(fd, 200, "application/json", resp, strlen(resp));
}

static void handle_api_nightvision(int fd, const char *query) {
    if (strstr(query, "mode=day")) {
        set_night_mode(NIGHT_MODE_DAY);
    } else if (strstr(query, "mode=ir")) {
        set_night_mode(NIGHT_MODE_IR);
    } else if (strstr(query, "mode=color")) {
        set_night_mode(NIGHT_MODE_COLOR);
    } else {
        set_night_mode(NIGHT_MODE_AUTO);
    }

    const char resp[] = "{\"status\":\"ok\"}\n";
    send_http_response(fd, 200, "application/json", resp, strlen(resp));
}

static void handle_api_siren(int fd, const char *query) {
    bool enable = strstr(query, "state=on") != NULL;
    set_siren(enable);

    const char resp[] = "{\"status\":\"ok\"}\n";
    send_http_response(fd, 200, "application/json", resp, strlen(resp));
}

static void handle_http_client(int client_fd) {
    char req[HTTP_BUF_SIZE];
    ssize_t n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    req[n] = '\0';

    char method[16], uri[256], proto[16];
    if (sscanf(req, "%15s %255s %15s", method, uri, proto) < 3) {
        close(client_fd);
        return;
    }

    // Routing
    if (strcmp(uri, "/") == 0 || strcmp(uri, "/index.html") == 0) {
        send_http_response(client_fd, 200, "text/html", HTML_DASHBOARD, sizeof(HTML_DASHBOARD) - 1);
    } else if (strncmp(uri, "/api/status", 11) == 0) {
        handle_api_status(client_fd);
    } else if (strncmp(uri, "/api/wifi/scan", 14) == 0) {
        handle_api_wifi_scan(client_fd);
    } else if (strncmp(uri, "/api/wifi/setup", 15) == 0) {
        handle_api_wifi_setup(client_fd, uri);
    } else if (strncmp(uri, "/api/wifi/reset", 15) == 0) {
        handle_api_wifi_reset(client_fd);
    } else if (strncmp(uri, "/api/ptz", 8) == 0 || strncmp(uri, "/cmd", 4) == 0) {
        handle_api_ptz(client_fd, uri);
    } else if (strncmp(uri, "/api/nightvision", 16) == 0) {
        handle_api_nightvision(client_fd, uri);
    } else if (strncmp(uri, "/api/siren", 10) == 0) {
        handle_api_siren(client_fd, uri);
    } else {
        const char not_found[] = "404 Not Found\n";
        send_http_response(client_fd, 404, "text/plain", not_found, strlen(not_found));
    }

    close(client_fd);
}

int http_server_init(int port) {
    g_http_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_http_server_fd < 0) {
        LOG_ERROR("Failed to create HTTP socket: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(g_http_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port > 0 ? port : 8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_http_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind HTTP to port %d: %s", port, strerror(errno));
        close(g_http_server_fd);
        g_http_server_fd = -1;
        return -1;
    }

    if (listen(g_http_server_fd, 10) < 0) {
        LOG_ERROR("Failed to listen on HTTP socket: %s", strerror(errno));
        close(g_http_server_fd);
        g_http_server_fd = -1;
        return -1;
    }

    LOG_INFO("HTTP Server listening on port %d", port > 0 ? port : 8080);
    return 0;
}

void http_server_stop(void) {
    if (g_http_server_fd >= 0) {
        close(g_http_server_fd);
        g_http_server_fd = -1;
    }
}

void *http_server_thread(void *arg) {
    (void)arg;
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(g_http_server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd >= 0) {
            handle_http_client(client_fd);
        }
    }
    return NULL;
}

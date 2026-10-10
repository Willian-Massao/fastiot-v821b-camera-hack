#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <pthread.h>

#define HTTP_PORT 8080
#define TELNET_PORT 2323
#define PTZ_UDP_PORT 8888

static const char HTML_PAGE[] =
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <meta charset=\"utf-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>Painel FastIoT - Controle Camera</title>\n"
"    <style>\n"
"        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #121214; color: #e1e1e6; margin: 0; padding: 20px; display: flex; justify-content: center; }\n"
"        .card { background: #202024; border-radius: 12px; padding: 24px; max-width: 480px; width: 100%; box-shadow: 0 8px 24px rgba(0,0,0,0.5); text-align: center; }\n"
"        h1 { margin-top: 0; font-size: 22px; color: #04d361; }\n"
"        .status { background: #121214; padding: 12px; border-radius: 8px; margin-bottom: 20px; font-family: monospace; font-size: 13px; color: #a8a8b3; text-align: left; }\n"
"        .pad { display: grid; grid-template-columns: repeat(3, 70px); gap: 12px; justify-content: center; margin: 20px auto; }\n"
"        button { background: #29292e; border: 1px solid #323238; color: #fff; font-size: 24px; height: 60px; border-radius: 10px; cursor: pointer; transition: 0.1s; display: flex; align-items: center; justify-content: center; user-select: none; }\n"
"        button:hover { background: #04d361; color: #000; border-color: #04d361; }\n"
"        button:active { transform: scale(0.95); }\n"
"        .blank { visibility: hidden; }\n"
"        .stream-box { margin-top: 25px; padding: 15px; background: #121214; border-radius: 8px; text-align: left; font-size: 13px; }\n"
"        .stream-box code { color: #8257e5; word-break: break-all; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"card\">\n"
"        <h1>Controle Camera IP</h1>\n"
"        <div id=\"log\" class=\"status\">Status: Pronto</div>\n"
"\n"
"        <div class=\"pad\">\n"
"            <button class=\"blank\"></button>\n"
"            <button onclick=\"move('u')\">&#x2B06;&#xFE0F;</button>\n"
"            <button class=\"blank\"></button>\n"
"\n"
"            <button onclick=\"move('l')\">&#x2B05;&#xFE0F;</button>\n"
"            <button onclick=\"move('s')\" style=\"font-size:16px;\">&#x23F9;&#xFE0F;</button>\n"
"            <button onclick=\"move('r')\">&#x27A1;&#xFE0F;</button>\n"
"\n"
"            <button class=\"blank\"></button>\n"
"            <button onclick=\"move('d')\">&#x2B07;&#xFE0F;</button>\n"
"            <button class=\"blank\"></button>\n"
"        </div>\n"
"\n"
"        <div class=\"stream-box\">\n"
"            <strong>Transmissao Audio + Video (VLC / go2rtc):</strong><br>\n"
"            <code id=\"rtspUrl\">rtsp://admin:Ztac@2021@IP:554/live/camid_0/main</code><br><br>\n"
"            <strong>Terminal Root Telnet:</strong> <code>porta 23</code>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <script>\n"
"        document.getElementById('rtspUrl').innerText = 'rtsp://admin:Ztac@2021@' + window.location.hostname + ':554/live/camid_0/main';\n"
"        function move(dir) {\n"
"            var nomes = {'u':'CIMA', 'd':'BAIXO', 'l':'ESQUERDA', 'r':'DIREITA', 's':'PARAR'};\n"
"            document.getElementById('log').innerText = 'Movendo: ' + (nomes[dir] || dir) + '...';\n"
"            fetch('/cmd?dir=' + dir)\n"
"                .then(r => r.text())\n"
"                .then(t => document.getElementById('log').innerText = t)\n"
"                .catch(e => document.getElementById('log').innerText = 'Erro: ' + e);\n"
"        }\n"
"    </script>\n"
"</body>\n"
"</html>\n";

static void send_ptz_udp(int type) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PTZ_UDP_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", type);
    sendto(fd, buf, strlen(buf), 0, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
}

static void *telnet_daemon_thread(void *arg) {
    (void)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TELNET_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        return NULL;
    }
    listen(server_fd, 5);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);
            dup2(client_fd, 0);
            dup2(client_fd, 1);
            dup2(client_fd, 2);
            close(client_fd);
            char *sh_args[] = {"/bin/ash", NULL};
            execv("/bin/ash", sh_args);
            execv("/bin/sh", sh_args);
            exit(0);
        }
        close(client_fd);
        waitpid(-1, NULL, WNOHANG);
    }
    return NULL;
}

static void handle_http_client(int client_fd) {
    char req[2048];
    int n = read(client_fd, req, sizeof(req) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    req[n] = '\0';

    if (strncmp(req, "GET /cmd?", 9) == 0) {
        char *p_dir = strstr(req, "dir=");
        char dir = 's';
        if (p_dir) dir = *(p_dir + 4);

        int type = 0;
        const char *desc = "PARADO";
        if (dir == 'u') { type = 4; desc = "CIMA (Up)"; }
        else if (dir == 'd') { type = 3; desc = "BAIXO (Down)"; }
        else if (dir == 'l') { type = 1; desc = "ESQUERDA (Left)"; }
        else if (dir == 'r') { type = 2; desc = "DIREITA (Right)"; }
        else if (dir == 's') { type = 0; desc = "PARAR"; }

        send_ptz_udp(type);

        char res[256];
        snprintf(res, sizeof(res), "Comando enviado: %s (codigo %d)", desc, type);

        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n", strlen(res));
        write(client_fd, header, hlen);
        write(client_fd, res, strlen(res));
    } else {
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n", strlen(HTML_PAGE));
        write(client_fd, header, hlen);
        write(client_fd, HTML_PAGE, strlen(HTML_PAGE));
    }
    close(client_fd);
}

int main() {
    signal(SIGCHLD, SIG_IGN);

    pthread_t th;
    pthread_create(&th, NULL, telnet_daemon_thread, NULL);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(HTTP_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind http");
        return 1;
    }
    listen(server_fd, 10);
    printf("CamServer iniciado! HTTP: %d, Telnet: %d\n", HTTP_PORT, TELNET_PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        handle_http_client(client_fd);
    }
    return 0;
}

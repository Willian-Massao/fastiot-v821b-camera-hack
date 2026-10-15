#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <netdb.h>
#include <errno.h>
#include <stdbool.h>

// Function pointer to FastIot_PTZControl:
// Entry point at 0x3aa0e contains the __riscv_save_3 / __riscv_restore_3 frame
typedef int (*p_ptz_func)(int ptzid, int type);

// Hook connect() to block WAN traffic from ipcam immediately
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    static int (*real_connect)(int, const struct sockaddr *, socklen_t) = NULL;
    if (!real_connect) {
        real_connect = (int (*)(int, const struct sockaddr *, socklen_t))dlsym(RTLD_NEXT, "connect");
    }

    if (addr && addr->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
        uint32_t ip = ntohl(in->sin_addr.s_addr);

        // Allowed IP ranges (LAN only):
        // 127.0.0.0/8 (Loopback)
        // 10.0.0.0/8 (Private A)
        // 172.16.0.0/12 (Private B)
        // 192.168.0.0/16 (Private C)
        // 224.0.0.0/4 (Multicast)
        // 255.255.255.255 (Broadcast)
        bool is_local = (ip == 0) ||
                        ((ip >> 24) == 127) ||
                        ((ip >> 24) == 10) ||
                        (((ip >> 20) & 0xFFF) == (172 << 4 | 1)) ||
                        ((ip >> 16) == (192 << 8 | 168)) ||
                        ((ip >> 28) == 14) ||
                        (ip == 0xFFFFFFFF);

        if (!is_local) {
            // Block outbound cloud connections immediately without timeouts
            errno = ENETUNREACH;
            return -1;
        }
    }

    return real_connect ? real_connect(sockfd, addr, addrlen) : -1;
}

// Hook gethostbyname to prevent slow DNS lookups to cloud services
struct hostent *gethostbyname(const char *name) {
    if (name && (strcmp(name, "localhost") != 0 && strcmp(name, "127.0.0.1") != 0)) {
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }
    static struct hostent *(*real_gethostbyname)(const char *) = NULL;
    if (!real_gethostbyname) {
        real_gethostbyname = (struct hostent *(*)(const char *))dlsym(RTLD_NEXT, "gethostbyname");
    }
    return real_gethostbyname ? real_gethostbyname(name) : NULL;
}

static void *ptz_bridge_thread(void *arg) {
    (void)arg;
    // Wait a couple seconds for ipcam initialization (g_HALIONodeAdaptationHandle)
    sleep(3);

    p_ptz_func ptz_control = (p_ptz_func)0x3aa0e;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    fprintf(stderr, "[PTZ_BRIDGE] Pronto! Ouvindo na porta UDP 8888 (FastIot_PTZControl @ 0x3aa0e)\n");

    char buf[64];
    while (1) {
        int n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) continue;
        buf[n] = '\0';

        int type = atoi(buf);
        fprintf(stderr, "[PTZ_BRIDGE] Comando PTZ recebido: type=%d\n", type);
        if (type > 0) {
            ptz_control(1, type);
            usleep(350000); // 350ms
            ptz_control(1, 0);
        } else {
            ptz_control(1, 0);
        }
    }
    return NULL;
}

__attribute__((constructor)) static void init_ptz_bridge() {
    pthread_t th;
    pthread_create(&th, NULL, ptz_bridge_thread, NULL);
}

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Function pointer to FastIot_PTZControl:
// Entry point at 0x3aa0e contains the __riscv_save_3 / __riscv_restore_3 frame
typedef int (*p_ptz_func)(int ptzid, int type);

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

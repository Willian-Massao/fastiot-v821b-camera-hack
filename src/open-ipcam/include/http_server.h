#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "common.h"

int http_server_init(int port);
void http_server_stop(void);
void *http_server_thread(void *arg);

#endif // HTTP_SERVER_H

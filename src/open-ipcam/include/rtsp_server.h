#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include "common.h"

int rtsp_server_init(int port);
void rtsp_server_stop(void);
void *rtsp_server_thread(void *arg);

// Broadcast an H.264 NALU to all connected RTSP clients
int rtsp_broadcast_h264_nalu(int stream_id, const uint8_t *nalu_data, size_t nalu_len, uint32_t timestamp);

#endif // RTSP_SERVER_H

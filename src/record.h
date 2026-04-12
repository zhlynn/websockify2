/* SPDX-License-Identifier: MIT */
#ifndef WS_RECORD_H
#define WS_RECORD_H

#include "platform.h"

typedef struct {
    FILE    *fp;
    uint64_t start_time;
} ws_record_t;

/* Open recording file. Returns 0 on success */
int  ws_record_open(ws_record_t *rec, const char *path);
void ws_record_close(ws_record_t *rec);

/* Record a frame. direction: '<' = from client, '>' = to client */
int  ws_record_frame(ws_record_t *rec, char direction, const void *data, int len);

#endif /* WS_RECORD_H */

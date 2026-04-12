/* SPDX-License-Identifier: MIT */
#ifndef WS_WS_H
#define WS_WS_H

#include "platform.h"
#include "buf.h"

/* WebSocket opcodes */
#define WS_OP_CONT   0x0
#define WS_OP_TEXT   0x1
#define WS_OP_BIN    0x2
#define WS_OP_CLOSE  0x8
#define WS_OP_PING   0x9
#define WS_OP_PONG   0xA

/* WebSocket close codes */
#define WS_CLOSE_NORMAL    1000
#define WS_CLOSE_GOING     1001
#define WS_CLOSE_PROTOCOL  1002
#define WS_CLOSE_BADDATA   1003
#define WS_CLOSE_ABNORMAL  1006
#define WS_CLOSE_TOOBIG    1009

/* Max frame header is 14 bytes (2 base + 8 extended length + 4 mask) */
#define WS_MAX_FRAME_HEADER 14

typedef struct {
    uint8_t  fin;
    uint8_t  opcode;
    uint8_t  masked;
    uint8_t  mask[4];
    uint64_t payload_len;
    int      header_len;   /* total header bytes consumed */
} ws_frame_header_t;

/* Parse frame header from buffer.
 * Returns:
 *   > 0: header parsed, value is header length
 *     0: need more data
 *    -1: protocol error */
int ws_frame_parse_header(const uint8_t *data, int len, ws_frame_header_t *hdr);

/* Apply/remove mask in-place. Optimized for 4/8 byte chunks */
void ws_frame_apply_mask(uint8_t *data, uint64_t len, const uint8_t mask[4]);

/* Encode frame header into buf. Returns header length.
 * buf must be at least WS_MAX_FRAME_HEADER bytes */
int ws_frame_encode_header(uint8_t *buf, uint8_t opcode, uint64_t payload_len, int fin);

/* Build and encode a complete close frame. Returns total frame length.
 * buf must be at least WS_MAX_FRAME_HEADER + 2 + reason_len bytes */
int ws_frame_encode_close(uint8_t *buf, uint16_t code, const char *reason);

/* Build pong response frame from ping payload */
int ws_frame_encode_pong(uint8_t *buf, const uint8_t *payload, int payload_len);

#endif /* WS_WS_H */

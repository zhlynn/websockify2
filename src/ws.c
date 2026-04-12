/* SPDX-License-Identifier: MIT */
#include "ws.h"

int ws_frame_parse_header(const uint8_t *data, int len, ws_frame_header_t *hdr) {
    if (len < 2) return 0;

    hdr->fin    = (data[0] >> 7) & 0x01;
    hdr->opcode = data[0] & 0x0F;
    hdr->masked = (data[1] >> 7) & 0x01;

    uint64_t plen = data[1] & 0x7F;
    int offset = 2;

    if (plen == 126) {
        if (len < 4) return 0;
        plen = ((uint64_t)data[2] << 8) | (uint64_t)data[3];
        offset = 4;
    } else if (plen == 127) {
        if (len < 10) return 0;
        plen = 0;
        for (int i = 0; i < 8; i++)
            plen = (plen << 8) | (uint64_t)data[2 + i];
        offset = 10;
    }

    if (hdr->masked) {
        if (len < offset + 4) return 0;
        memcpy(hdr->mask, data + offset, 4);
        offset += 4;
    }

    hdr->payload_len = plen;
    hdr->header_len = offset;
    return offset;
}

void ws_frame_apply_mask(uint8_t *data, uint64_t len, const uint8_t mask[4]) {
    uint64_t i = 0;

    /* Process 8 bytes at a time */
    if (len >= 8) {
        uint64_t mask64;
        uint8_t *m8 = (uint8_t *)&mask64;
        m8[0] = mask[0]; m8[1] = mask[1]; m8[2] = mask[2]; m8[3] = mask[3];
        m8[4] = mask[0]; m8[5] = mask[1]; m8[6] = mask[2]; m8[7] = mask[3];

        for (; i + 8 <= len; i += 8) {
            uint64_t *p = (uint64_t *)(data + i);
            *p ^= mask64;
        }
    }

    /* Remaining bytes */
    for (; i < len; i++)
        data[i] ^= mask[i & 3];
}

int ws_frame_encode_header(uint8_t *buf, uint8_t opcode, uint64_t payload_len, int fin) {
    int offset = 0;
    buf[0] = (fin ? 0x80 : 0x00) | (opcode & 0x0F);

    if (payload_len < 126) {
        buf[1] = (uint8_t)payload_len;
        offset = 2;
    } else if (payload_len <= 0xFFFF) {
        buf[1] = 126;
        buf[2] = (uint8_t)(payload_len >> 8);
        buf[3] = (uint8_t)(payload_len);
        offset = 4;
    } else {
        buf[1] = 127;
        for (int i = 0; i < 8; i++)
            buf[2 + i] = (uint8_t)(payload_len >> (56 - i * 8));
        offset = 10;
    }
    return offset;
}

int ws_frame_encode_close(uint8_t *buf, uint16_t code, const char *reason) {
    int reason_len = reason ? (int)strlen(reason) : 0;
    int payload_len = 2 + reason_len;

    int hdr_len = ws_frame_encode_header(buf, WS_OP_CLOSE, (uint64_t)payload_len, 1);

    /* Close code in network byte order */
    buf[hdr_len] = (uint8_t)(code >> 8);
    buf[hdr_len + 1] = (uint8_t)(code);

    if (reason_len > 0)
        memcpy(buf + hdr_len + 2, reason, (size_t)reason_len);

    return hdr_len + payload_len;
}

int ws_frame_encode_pong(uint8_t *buf, const uint8_t *payload, int payload_len) {
    int hdr_len = ws_frame_encode_header(buf, WS_OP_PONG, (uint64_t)payload_len, 1);
    if (payload_len > 0)
        memcpy(buf + hdr_len, payload, (size_t)payload_len);
    return hdr_len + payload_len;
}

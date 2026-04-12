/* SPDX-License-Identifier: MIT */
#ifndef WS_CRYPTO_H
#define WS_CRYPTO_H

#include "platform.h"

/*
 * SHA-1 implementation for WebSocket handshake (Sec-WebSocket-Accept).
 * NOT for security purposes — SHA-1 is broken for crypto, but required by RFC 6455.
 */

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buffer[64];
} ws_sha1_ctx_t;

void ws_sha1_init(ws_sha1_ctx_t *ctx);
void ws_sha1_update(ws_sha1_ctx_t *ctx, const void *data, size_t len);
void ws_sha1_final(ws_sha1_ctx_t *ctx, uint8_t digest[20]);

/* One-shot SHA-1 */
void ws_sha1(const void *data, size_t len, uint8_t digest[20]);

/*
 * Base64 encode/decode
 */

/* Returns number of bytes written (including NUL), or 0 on error.
 * out_size must be >= ((len + 2) / 3) * 4 + 1 */
int ws_base64_encode(const void *data, size_t len, char *out, size_t out_size);

/* Returns number of decoded bytes, or -1 on error */
int ws_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_size);

/*
 * Generate WebSocket accept key from client key.
 * out must be at least 29 bytes.
 */
int ws_websocket_accept_key(const char *client_key, char *out, size_t out_size);

#endif /* WS_CRYPTO_H */

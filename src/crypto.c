/* SPDX-License-Identifier: MIT */
#include "crypto.h"

/* ---- SHA-1 (RFC 3174) ---- */

#define SHA1_ROL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    uint32_t a, b, c, d, e;

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24)
             | ((uint32_t)block[i*4+1] << 16)
             | ((uint32_t)block[i*4+2] << 8)
             | ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = SHA1_ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    a = state[0]; b = state[1]; c = state[2];
    d = state[3]; e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t = SHA1_ROL(a, 5) + f + e + k + w[i];
        e = d; d = c;
        c = SHA1_ROL(b, 30);
        b = a; a = t;
    }

    state[0] += a; state[1] += b; state[2] += c;
    state[3] += d; state[4] += e;
}

void ws_sha1_init(ws_sha1_ctx_t *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

void ws_sha1_update(ws_sha1_ctx_t *ctx, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    size_t offset = (size_t)(ctx->count & 63);
    ctx->count += len;

    /* Fill partial block */
    if (offset) {
        size_t avail = 64 - offset;
        if (len < avail) {
            memcpy(ctx->buffer + offset, p, len);
            return;
        }
        memcpy(ctx->buffer + offset, p, avail);
        sha1_transform(ctx->state, ctx->buffer);
        p += avail;
        len -= avail;
    }

    /* Process full blocks */
    while (len >= 64) {
        sha1_transform(ctx->state, p);
        p += 64;
        len -= 64;
    }

    /* Save remainder */
    if (len)
        memcpy(ctx->buffer, p, len);
}

void ws_sha1_final(ws_sha1_ctx_t *ctx, uint8_t digest[20]) {
    uint64_t bits = ctx->count * 8;
    uint8_t pad = 0x80;
    ws_sha1_update(ctx, &pad, 1);

    pad = 0;
    while ((ctx->count & 63) != 56)
        ws_sha1_update(ctx, &pad, 1);

    uint8_t bits_be[8];
    for (int i = 0; i < 8; i++)
        bits_be[i] = (uint8_t)(bits >> (56 - i * 8));
    ws_sha1_update(ctx, bits_be, 8);

    for (int i = 0; i < 5; i++) {
        digest[i*4+0] = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}

void ws_sha1(const void *data, size_t len, uint8_t digest[20]) {
    ws_sha1_ctx_t ctx;
    ws_sha1_init(&ctx);
    ws_sha1_update(&ctx, data, len);
    ws_sha1_final(&ctx, digest);
}

/* ---- Base64 ---- */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int ws_base64_encode(const void *data, size_t len, char *out, size_t out_size) {
    size_t needed = ((len + 2) / 3) * 4 + 1;
    if (out_size < needed) return 0;

    const uint8_t *p = (const uint8_t *)data;
    char *o = out;

    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (uint32_t)p[i] << 16;
        if (i + 1 < len) val |= (uint32_t)p[i+1] << 8;
        if (i + 2 < len) val |= (uint32_t)p[i+2];

        *o++ = b64_table[(val >> 18) & 0x3F];
        *o++ = b64_table[(val >> 12) & 0x3F];
        *o++ = (i + 1 < len) ? b64_table[(val >> 6) & 0x3F] : '=';
        *o++ = (i + 2 < len) ? b64_table[val & 0x3F] : '=';
    }
    *o = '\0';
    return (int)(o - out + 1);
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int ws_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_size) {
    if (in_len % 4 != 0) return -1;

    size_t out_len = (in_len / 4) * 3;
    if (in_len > 0 && in[in_len - 1] == '=') out_len--;
    if (in_len > 1 && in[in_len - 2] == '=') out_len--;
    if (out_size < out_len) return -1;

    uint8_t *o = out;
    for (size_t i = 0; i < in_len; i += 4) {
        int a = b64_decode_char(in[i]);
        int b = b64_decode_char(in[i+1]);
        int c = (in[i+2] != '=') ? b64_decode_char(in[i+2]) : 0;
        int d = (in[i+3] != '=') ? b64_decode_char(in[i+3]) : 0;

        if (a < 0 || b < 0 || (in[i+2] != '=' && c < 0) || (in[i+3] != '=' && d < 0))
            return -1;

        uint32_t val = ((uint32_t)a << 18) | ((uint32_t)b << 12)
                     | ((uint32_t)c << 6) | (uint32_t)d;

        *o++ = (uint8_t)(val >> 16);
        if (in[i+2] != '=') *o++ = (uint8_t)(val >> 8);
        if (in[i+3] != '=') *o++ = (uint8_t)(val);
    }
    return (int)(o - out);
}

/* ---- WebSocket Accept Key ---- */

static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

int ws_websocket_accept_key(const char *client_key, char *out, size_t out_size) {
    /* Concatenate client_key + magic GUID */
    char concat[256];
    int n = snprintf(concat, sizeof(concat), "%s%s", client_key, WS_MAGIC);
    if (n < 0 || (size_t)n >= sizeof(concat)) return -1;

    /* SHA-1 hash */
    uint8_t digest[20];
    ws_sha1(concat, (size_t)n, digest);

    /* Base64 encode */
    return ws_base64_encode(digest, 20, out, out_size) > 0 ? 0 : -1;
}

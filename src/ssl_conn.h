/* SPDX-License-Identifier: MIT */
#ifndef WS_SSL_CONN_H
#define WS_SSL_CONN_H

#include "platform.h"

/* Forward declare to avoid exposing OpenSSL headers everywhere */
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

typedef struct {
    SSL_CTX *ctx;
    int      ssl_only;
} ws_ssl_ctx_t;

typedef enum {
    WS_SSL_OK = 0,
    WS_SSL_WANT_READ,
    WS_SSL_WANT_WRITE,
    WS_SSL_ERROR,
    WS_SSL_CLOSED
} ws_ssl_status_t;

/* Initialize OpenSSL library (call once) */
int ws_ssl_init_library(void);

/* Create SSL context. cert/key required. Returns NULL on error */
ws_ssl_ctx_t *ws_ssl_ctx_create(const char *cert, const char *key,
                                 const char *ca_cert, const char *ciphers,
                                 int verify_client, int ssl_only);
void ws_ssl_ctx_destroy(ws_ssl_ctx_t *ctx);

/* Create SSL connection from accepted socket */
SSL *ws_ssl_new(ws_ssl_ctx_t *ctx, ws_socket_t fd);
void ws_ssl_free(SSL *ssl);

/* Non-blocking handshake. Returns status */
ws_ssl_status_t ws_ssl_handshake(SSL *ssl);

/* Read/write through SSL. Returns bytes transferred, or sets status */
int ws_ssl_read(SSL *ssl, void *buf, int len, ws_ssl_status_t *status);
int ws_ssl_write(SSL *ssl, const void *buf, int len, ws_ssl_status_t *status);

/* Shutdown SSL connection */
ws_ssl_status_t ws_ssl_shutdown(SSL *ssl);

/* Detect if first byte looks like SSL (peek at socket) */
int ws_ssl_detect(ws_socket_t fd);

#endif /* WS_SSL_CONN_H */

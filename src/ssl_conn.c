/* SPDX-License-Identifier: MIT */
#include "ssl_conn.h"
#include "log.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>

int ws_ssl_init_library(void) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    return 0;
}

ws_ssl_ctx_t *ws_ssl_ctx_create(const char *cert, const char *key,
                                 const char *ca_cert, const char *ciphers,
                                 int verify_client, int ssl_only) {
    ws_ssl_ctx_t *ctx = (ws_ssl_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->ssl_only = ssl_only;
    ctx->ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx->ctx) {
        ws_log_error("SSL_CTX_new failed");
        free(ctx);
        return NULL;
    }

    /* Disable old protocols */
    SSL_CTX_set_min_proto_version(ctx->ctx, TLS1_2_VERSION);

    /* Load certificate */
    if (SSL_CTX_use_certificate_chain_file(ctx->ctx, cert) != 1) {
        ws_log_error("Failed to load certificate: %s", cert);
        goto err;
    }

    /* Load private key */
    if (SSL_CTX_use_PrivateKey_file(ctx->ctx, key, SSL_FILETYPE_PEM) != 1) {
        ws_log_error("Failed to load private key: %s", key);
        goto err;
    }

    if (SSL_CTX_check_private_key(ctx->ctx) != 1) {
        ws_log_error("Certificate/key mismatch");
        goto err;
    }

    /* Ciphers */
    if (ciphers && ciphers[0]) {
        if (SSL_CTX_set_cipher_list(ctx->ctx, ciphers) != 1) {
            ws_log_error("Invalid cipher list: %s", ciphers);
            goto err;
        }
    }

    /* Client verification */
    if (verify_client) {
        if (ca_cert && ca_cert[0]) {
            if (SSL_CTX_load_verify_locations(ctx->ctx, ca_cert, NULL) != 1) {
                ws_log_error("Failed to load CA cert: %s", ca_cert);
                goto err;
            }
        }
        SSL_CTX_set_verify(ctx->ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    }

    /* Performance: enable session caching */
    SSL_CTX_set_session_cache_mode(ctx->ctx, SSL_SESS_CACHE_SERVER);
    SSL_CTX_sess_set_cache_size(ctx->ctx, 1024);

    return ctx;

err:
    ERR_clear_error();
    SSL_CTX_free(ctx->ctx);
    free(ctx);
    return NULL;
}

void ws_ssl_ctx_destroy(ws_ssl_ctx_t *ctx) {
    if (!ctx) return;
    SSL_CTX_free(ctx->ctx);
    free(ctx);
}

SSL *ws_ssl_new(ws_ssl_ctx_t *ctx, ws_socket_t fd) {
    SSL *ssl = SSL_new(ctx->ctx);
    if (!ssl) return NULL;
    SSL_set_fd(ssl, (int)fd);
    SSL_set_accept_state(ssl);
    return ssl;
}

void ws_ssl_free(SSL *ssl) {
    if (ssl) SSL_free(ssl);
}

ws_ssl_status_t ws_ssl_handshake(SSL *ssl) {
    int ret = SSL_do_handshake(ssl);
    if (ret == 1) return WS_SSL_OK;

    int err = SSL_get_error(ssl, ret);
    switch (err) {
    case SSL_ERROR_WANT_READ:  return WS_SSL_WANT_READ;
    case SSL_ERROR_WANT_WRITE: return WS_SSL_WANT_WRITE;
    default:                   return WS_SSL_ERROR;
    }
}

int ws_ssl_read(SSL *ssl, void *buf, int len, ws_ssl_status_t *status) {
    int n = SSL_read(ssl, buf, len);
    if (n > 0) {
        *status = WS_SSL_OK;
        return n;
    }

    int err = SSL_get_error(ssl, n);
    switch (err) {
    case SSL_ERROR_WANT_READ:  *status = WS_SSL_WANT_READ;  return 0;
    case SSL_ERROR_WANT_WRITE: *status = WS_SSL_WANT_WRITE; return 0;
    case SSL_ERROR_ZERO_RETURN: *status = WS_SSL_CLOSED;    return 0;
    default:                    *status = WS_SSL_ERROR;      return -1;
    }
}

int ws_ssl_write(SSL *ssl, const void *buf, int len, ws_ssl_status_t *status) {
    int n = SSL_write(ssl, buf, len);
    if (n > 0) {
        *status = WS_SSL_OK;
        return n;
    }

    int err = SSL_get_error(ssl, n);
    switch (err) {
    case SSL_ERROR_WANT_READ:  *status = WS_SSL_WANT_READ;  return 0;
    case SSL_ERROR_WANT_WRITE: *status = WS_SSL_WANT_WRITE; return 0;
    default:                    *status = WS_SSL_ERROR;      return -1;
    }
}

ws_ssl_status_t ws_ssl_shutdown(SSL *ssl) {
    int ret = SSL_shutdown(ssl);
    if (ret == 1) return WS_SSL_OK;
    if (ret == 0) return WS_SSL_WANT_READ;  /* need to call again */

    int err = SSL_get_error(ssl, ret);
    switch (err) {
    case SSL_ERROR_WANT_READ:  return WS_SSL_WANT_READ;
    case SSL_ERROR_WANT_WRITE: return WS_SSL_WANT_WRITE;
    default:                   return WS_SSL_ERROR;
    }
}

int ws_ssl_detect(ws_socket_t fd) {
    uint8_t byte;
    int n = (int)recv(fd, (char *)&byte, 1, MSG_PEEK);
    if (n != 1) return 0;
    /* 0x16 = TLS handshake, 0x80 = SSLv2 (legacy) */
    return (byte == 0x16 || byte == 0x80);
}

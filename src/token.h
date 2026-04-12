/* SPDX-License-Identifier: MIT */
#ifndef WS_TOKEN_H
#define WS_TOKEN_H

#include "platform.h"

typedef struct {
    char host[256];
    int  port;
} ws_target_t;

typedef struct {
    char path[4096];
    int  is_dir;  /* path is a directory of token files */
} ws_token_ctx_t;

/* Initialize token context. path can be a single file or a directory */
int  ws_token_init(ws_token_ctx_t *ctx, const char *path);
void ws_token_free(ws_token_ctx_t *ctx);

/* Look up token. Returns 0 on success, -1 if not found.
 * Token file format: token: host:port (one per line) */
int  ws_token_lookup(const ws_token_ctx_t *ctx, const char *token, ws_target_t *target);

#endif /* WS_TOKEN_H */

/* SPDX-License-Identifier: MIT */
#include "token.h"
#include "log.h"
#include <dirent.h>
#include <ctype.h>

int ws_token_init(ws_token_ctx_t *ctx, const char *path) {
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->path, sizeof(ctx->path), "%s", path);

    struct stat st;
    if (stat(path, &st) < 0) {
        ws_log_error("token path not found: %s", path);
        return -1;
    }
    ctx->is_dir = S_ISDIR(st.st_mode);
    return 0;
}

void ws_token_free(ws_token_ctx_t *ctx) {
    (void)ctx;
}

static int parse_token_line(const char *line, const char *token, ws_target_t *target) {
    /* Format: "token: host:port" or "token = host:port" */
    const char *p = line;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '#' || *p == '\0') return 0;  /* comment or empty */

    /* Find token separator (: or =) */
    const char *sep = strpbrk(p, ":=");
    if (!sep) return 0;

    /* Extract token name, trim trailing spaces */
    size_t tlen = (size_t)(sep - p);
    while (tlen > 0 && isspace((unsigned char)p[tlen - 1])) tlen--;

    if (strlen(token) != tlen || strncmp(p, token, tlen) != 0)
        return 0;

    /* Extract value */
    const char *val = sep + 1;
    while (isspace((unsigned char)*val)) val++;

    /* Parse host:port */
    const char *colon = strrchr(val, ':');
    if (!colon) return 0;

    size_t hlen = (size_t)(colon - val);
    if (hlen >= sizeof(target->host)) return 0;

    memcpy(target->host, val, hlen);
    target->host[hlen] = '\0';

    /* Trim trailing whitespace from host */
    while (hlen > 0 && isspace((unsigned char)target->host[hlen - 1]))
        target->host[--hlen] = '\0';

    target->port = atoi(colon + 1);
    return target->port > 0 ? 1 : 0;
}

static int search_file(const char *path, const char *token, ws_target_t *target) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len-1] == '\r') line[--len] = '\0';

        if (parse_token_line(line, token, target) == 1) {
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return -1;
}

int ws_token_lookup(const ws_token_ctx_t *ctx, const char *token, ws_target_t *target) {
    if (!ctx->is_dir) {
        return search_file(ctx->path, token, target);
    }

    /* Search all files in directory */
    DIR *d = opendir(ctx->path);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char filepath[4096];
        snprintf(filepath, sizeof(filepath), "%s/%s", ctx->path, ent->d_name);

        if (search_file(filepath, token, target) == 0) {
            closedir(d);
            return 0;
        }
    }

    closedir(d);
    return -1;
}

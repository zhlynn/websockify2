/* SPDX-License-Identifier: MIT */
#include "token.h"
#include "log.h"
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#ifndef WS_PLATFORM_WINDOWS
#include <fcntl.h>
#include <unistd.h>
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

int ws_token_init(ws_token_ctx_t *ctx, const char *path) {
    if (!ctx || !path) return -1;
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->path, sizeof(ctx->path), "%s", path);

    struct stat st;
#ifdef WS_PLATFORM_WINDOWS
    if (stat(path, &st) < 0) {
#else
    /* lstat: do not follow a symlink as the token root itself */
    if (lstat(path, &st) < 0) {
#endif
        ws_log_error("token path not found: %s", path);
        return -1;
    }
#ifndef WS_PLATFORM_WINDOWS
    if (S_ISLNK(st.st_mode)) {
        ws_log_error("token path is a symlink, refusing: %s", path);
        return -1;
    }
#endif
    ctx->is_dir = S_ISDIR(st.st_mode);
    return 0;
}

/* Reject anything that isn't a plain token filename. Defends against
 * malicious filenames like "..", "x/y", or embedded slashes. */
static int valid_token_filename(const char *name) {
    if (!name || !name[0]) return 0;
    for (const char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '/' || c == '\\') return 0;
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return 0;
    }
    /* Forbid "." and ".." explicitly */
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
        return 0;
    return 1;
}

/* Reject tokens that would be dangerous to log or compare against files.
 * Legitimate tokens are short ASCII identifiers. */
static int valid_token_string(const char *token) {
    if (!token || !token[0]) return 0;
    size_t n = strlen(token);
    if (n > 128) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)token[i];
        if (c <= 0x20 || c == 0x7f) return 0;
    }
    return 1;
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
#ifndef WS_PLATFORM_WINDOWS
    /* Open with O_NOFOLLOW so symlinks inside the token directory cannot
     * be used to redirect us to arbitrary files (e.g. /etc/passwd). */
    int fd = open(path, O_RDONLY | O_NOFOLLOW
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
                  );
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) { close(fd); return -1; }
    FILE *f = fdopen(fd, "r");
    if (!f) { close(fd); return -1; }
#else
    FILE *f = fopen(path, "r");
    if (!f) return -1;
#endif

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
    if (!ctx || !target || !valid_token_string(token)) return -1;

    if (!ctx->is_dir) {
        return search_file(ctx->path, token, target);
    }

    /* Search all files in directory */
    DIR *d = opendir(ctx->path);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!valid_token_filename(ent->d_name)) continue;

        char filepath[4096];
        int n = snprintf(filepath, sizeof(filepath), "%s/%s", ctx->path, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(filepath)) continue;

        if (search_file(filepath, token, target) == 0) {
            closedir(d);
            return 0;
        }
    }

    closedir(d);
    return -1;
}

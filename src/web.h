/* SPDX-License-Identifier: MIT */
#ifndef WS_WEB_H
#define WS_WEB_H

#include "platform.h"
#include "http.h"

/* Get MIME type from file extension */
const char *ws_mime_type(const char *path);

/* Build HTTP response for a static file request.
 * Returns response length written to resp_buf, or -1 on error.
 * Sets *file_fd to the opened file descriptor for sendfile (caller closes).
 * Sets *file_size to the file size. */
int ws_web_serve_file(const char *web_dir, const ws_http_request_t *req,
                      char *resp_buf, int resp_buf_size,
                      int *file_fd, size_t *file_size);

#endif /* WS_WEB_H */

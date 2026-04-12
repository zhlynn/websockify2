/* SPDX-License-Identifier: MIT */
#ifndef WS_SERVER_H
#define WS_SERVER_H

#include "platform.h"
#include "config.h"

/* Run the server event loop. Returns 0 on normal shutdown, -1 on error. */
int ws_server_run(ws_config_t *config);

#endif /* WS_SERVER_H */

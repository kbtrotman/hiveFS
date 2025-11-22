/**
 * Lightweight TCP control-plane helpers used by hi_command to bridge the
 * kernel command ring to remote listeners.
 */
#pragma once

#include "hi_command.h"

int hicmd_tcp_init(const char *host, const char *port, int comm_fd);
void hicmd_tcp_poll(int timeout_ms);
void hicmd_tcp_broadcast_cmd(const struct hifs_cmds *cmd);
void hicmd_tcp_shutdown(void);

/*
 * This file is part of the sigrok-cli project.
 *
 * Copyright (C) 2026 Probes contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef SIGROK_CLI_MIRROR_H
#define SIGROK_CLI_MIRROR_H

#include <libsigrok/libsigrok.h>

/*
 * Optional live mirror. When a Probes deck broker advertises an endpoint
 * (PROBES_SIGROK_IPC, or <user-data>/Probes/sigrok/ipc/broker.json), a data
 * operation attaches a second datafeed callback that re-emits the same
 * binary-v1 frames to the broker and honors a "stop" command from it. When no
 * broker is present everything is a no-op and the CLI behaves exactly as
 * before.
 */
int mirror_attach(struct sr_session *session);
void mirror_detach(void);

#endif
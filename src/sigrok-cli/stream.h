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

#ifndef SIGROK_CLI_STREAM_H
#define SIGROK_CLI_STREAM_H

#include <libsigrok/libsigrok.h>

int stream_ensure_binary_stdout(void);
void stream_set_session(struct sr_session *session);
void stream_set_sample_limit(guint64 limit);
void stream_datafeed_in(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data);
int stream_control_attach(struct sr_session *session, gboolean stop_on_eof);
void stream_control_detach(void);
void stream_session_end(const char *reason);
void stream_set_source_file(void);

#endif

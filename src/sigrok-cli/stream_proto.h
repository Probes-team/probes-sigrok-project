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

#ifndef SIGROK_CLI_STREAM_PROTO_H
#define SIGROK_CLI_STREAM_PROTO_H

#include <glib.h>
#include <libsigrok/libsigrok.h>

/*
 * Shared encoder for the "binary-v1" frame protocol. Both the stdout stream
 * (stream.c) and the live mirror (mirror.c) emit byte-identical frames so the
 * panel can parse either source with one decoder.
 */
#define STREAM_MAGIC "PRBS"
#define STREAM_VERSION 1

enum stream_frame_type {
	STREAM_FRAME_META = 1,
	STREAM_FRAME_LOGIC = 2,
	STREAM_FRAME_TRIGGER = 3,
	STREAM_FRAME_FRAME_BEGIN = 4,
	STREAM_FRAME_FRAME_END = 5,
	STREAM_FRAME_STATS = 6,
	STREAM_FRAME_ERROR = 7,
	STREAM_FRAME_SESSION_END = 8,
};

void stream_json_escape(GString *json, const char *text);

/* Append one encoded frame (16-byte header + payload) to @out. */
void stream_proto_append_frame(GByteArray *out, guint8 type, guint32 sequence,
		const void *payload, guint32 length);

/* Build the META JSON payload for a device; @source is "live" or "file". */
GString *stream_build_meta(const struct sr_dev_inst *sdi, guint16 unitsize,
		const char *source);

#endif
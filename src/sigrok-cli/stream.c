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

#include <config.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "sigrok-cli.h"
#include "stream.h"
#include "stream_proto.h"

static guint32 stream_sequence;
static guint64 stream_total_samples;
static guint64 stream_frame_index;
static guint16 stream_unitsize;
static gboolean stream_triggered;
static gboolean stream_meta_sent;
static gboolean stream_end_sent;
static gboolean stream_stop_requested;
static gboolean stream_limit_reached;
static gboolean stream_stop_on_eof;
static guint64 stream_sample_limit;
static const char *stream_source = "live";
static struct sr_session *stream_session;
static GIOChannel *stream_control_channel;
static guint stream_control_source_id;

static void stream_write_frame(guint8 type, const void *payload, guint32 length)
{
	GByteArray *frame = g_byte_array_new();

	stream_proto_append_frame(frame, type, stream_sequence++, payload, length);
	fwrite(frame->data, 1, frame->len, stdout);
	fflush(stdout);
	g_byte_array_free(frame, TRUE);
}

static void stream_send_meta(const struct sr_dev_inst *sdi, guint16 unitsize)
{
	GString *json = stream_build_meta(sdi, unitsize, stream_source);
	stream_write_frame(STREAM_FRAME_META, json->str, json->len);
	g_string_free(json, TRUE);
	stream_meta_sent = TRUE;
}

static void byte_array_append_u16(GByteArray *array, guint16 value)
{
	guint8 bytes[2] = { value & 0xff, (value >> 8) & 0xff };
	g_byte_array_append(array, bytes, sizeof(bytes));
}

static void byte_array_append_u64(GByteArray *array, guint64 value)
{
	guint8 bytes[8];
	unsigned int i;

	for (i = 0; i < 8; i++)
		bytes[i] = (value >> (8 * i)) & 0xff;
	g_byte_array_append(array, bytes, sizeof(bytes));
}

static void stream_send_logic(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_logic *logic)
{
	GByteArray *payload;
	guint64 sample_count;
	guint64 remaining;
	guint32 length;

	if (!stream_meta_sent || stream_unitsize != logic->unitsize) {
		stream_unitsize = logic->unitsize;
		stream_send_meta(sdi, stream_unitsize);
	}

	sample_count = logic->length / logic->unitsize;
	length = logic->length;
	if (stream_sample_limit) {
		remaining = stream_total_samples < stream_sample_limit
			? stream_sample_limit - stream_total_samples : 0;
		if (sample_count > remaining) {
			sample_count = remaining;
			length = (guint32)(sample_count * logic->unitsize);
		}
	}
	if (sample_count == 0)
		return;
	payload = g_byte_array_sized_new(20 + length);
	byte_array_append_u64(payload, stream_total_samples);
	byte_array_append_u64(payload, sample_count);
	byte_array_append_u16(payload, logic->unitsize);
	byte_array_append_u16(payload, 0);
	g_byte_array_append(payload, logic->data, length);
	stream_write_frame(STREAM_FRAME_LOGIC, payload->data, payload->len);
	g_byte_array_free(payload, TRUE);
	stream_total_samples += sample_count;
}

static gboolean stream_stop_idle(gpointer data)
{
	(void)data;
	if (stream_session)
		sr_session_stop(stream_session);
	return G_SOURCE_REMOVE;
}

static gboolean stream_control_in(GIOChannel *source, GIOCondition condition,
		gpointer data)
{
	char buffer[256];
	gsize bytes_read = 0;
	GIOStatus status;
	gboolean requested = FALSE;

	(void)data;

	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		/*
		 * An explicit stop command always ends the stream. EOF only ends
		 * an unbounded one: a bounded acquisition finishes on its own, so
		 * a closed stdin (for example /dev/null) must not abort it.
		 */
		stream_control_source_id = 0;
		if (stream_stop_on_eof)
			g_idle_add(stream_stop_idle, NULL);
		return G_SOURCE_REMOVE;
	}

	status = g_io_channel_read_chars(source, buffer, sizeof(buffer) - 1,
		&bytes_read, NULL);
	if (bytes_read > 0) {
		buffer[bytes_read] = '\0';
		if (strstr(buffer, "\"stop\"") || strstr(buffer, "stop"))
			requested = TRUE;
	}
	if (requested)
		g_idle_add(stream_stop_idle, NULL);
	if (status == G_IO_STATUS_EOF) {
		stream_control_source_id = 0;
		if (stream_stop_on_eof)
			g_idle_add(stream_stop_idle, NULL);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

int stream_ensure_binary_stdout(void)
{
#ifdef G_OS_WIN32
	if (_setmode(_fileno(stdout), _O_BINARY) == -1)
		return SR_ERR;
#endif
	return SR_OK;
}

void stream_set_session(struct sr_session *session)
{
	stream_session = session;
}

void stream_set_sample_limit(guint64 limit)
{
	stream_sample_limit = limit;
}

int stream_control_attach(struct sr_session *session, gboolean stop_on_eof)
{
	stream_session = session;
	stream_stop_on_eof = stop_on_eof;
#ifdef G_OS_WIN32
	stream_control_channel = g_io_channel_win32_new_fd(0);
#else
	stream_control_channel = g_io_channel_unix_new(0);
#endif
	if (!stream_control_channel)
		return SR_ERR;
	g_io_channel_set_encoding(stream_control_channel, NULL, NULL);
	g_io_channel_set_buffered(stream_control_channel, FALSE);
	stream_control_source_id = g_io_add_watch(stream_control_channel,
		G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL, stream_control_in, NULL);
	return SR_OK;
}

void stream_control_detach(void)
{
	/*
	 * Remove the stdin watch so the event loop is not kept alive by a
	 * blocking reader (a lingering watch would keep the process running
	 * after a bounded acquisition already finished).
	 */
	if (stream_control_source_id) {
		g_source_remove(stream_control_source_id);
		stream_control_source_id = 0;
	}
	if (stream_control_channel) {
		g_io_channel_unref(stream_control_channel);
		stream_control_channel = NULL;
	}
	stream_session = NULL;
}

void stream_set_source_file(void)
{
	stream_source = "file";
}

void stream_session_end(const char *reason)
{
	GString *json;
	GString *stats;

	if (stream_end_sent)
		return;
	stream_end_sent = TRUE;

	stats = g_string_new("{\"totalSamples\":");
	g_string_append_printf(stats, "%" G_GUINT64_FORMAT ",\"frames\":%" G_GUINT64_FORMAT "}",
		stream_total_samples, stream_frame_index);
	stream_write_frame(STREAM_FRAME_STATS, stats->str, stats->len);
	g_string_free(stats, TRUE);

	json = g_string_new("{\"reason\":\"");
	stream_json_escape(json, reason ? reason : "stopped");
	g_string_append_printf(json, "\",\"totalSamples\":%" G_GUINT64_FORMAT ",\"triggered\":%s}",
		stream_total_samples, stream_triggered ? "true" : "false");
	stream_write_frame(STREAM_FRAME_SESSION_END, json->str, json->len);
	g_string_free(json, TRUE);
	fflush(stdout);
}

void stream_datafeed_in(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data)
{
	struct df_arg_desc *df_arg = cb_data;
	const struct sr_datafeed_logic *logic;
	GString *json;

	if (!packet)
		return;

	switch (packet->type) {
	case SR_DF_HEADER:
		stream_unitsize = 0;
		stream_meta_sent = FALSE;
		stream_stop_requested = FALSE;
		stream_limit_reached = FALSE;
		stream_send_meta(sdi, 0);
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		if (logic && logic->length > 0 && logic->unitsize > 0) {
			if (stream_sample_limit && stream_total_samples >= stream_sample_limit)
				break;
			stream_send_logic(sdi, logic);
			/*
			 * The CLI owns the sample limit so a driver that ignores
			 * SR_CONF_LIMIT_SAMPLES while streaming still ends on time
			 * instead of running until the user aborts.
			 */
			if (stream_sample_limit && stream_total_samples >= stream_sample_limit) {
				stream_limit_reached = TRUE;
				/*
				 * Defer the session stop to the event loop: stopping from
				 * inside the libusb datafeed callback makes the driver the
				 * cancel/drain its own transfers re-entrantly, which fails
				 * and leaves the FPGA running (DSView stops on a quiet pipe).
				 */
				g_idle_add(stream_stop_idle, NULL);
			}
		}
		break;
	case SR_DF_TRIGGER:
		stream_triggered = TRUE;
		{
			guint64 sample_index = stream_total_samples;
			stream_write_frame(STREAM_FRAME_TRIGGER, &sample_index,
				sizeof(sample_index));
		}
		break;
	case SR_DF_FRAME_BEGIN:
	case SR_DF_FRAME_END:
		stream_write_frame(packet->type == SR_DF_FRAME_BEGIN ?
			STREAM_FRAME_FRAME_BEGIN : STREAM_FRAME_FRAME_END,
			&stream_frame_index, sizeof(stream_frame_index));
		if (packet->type == SR_DF_FRAME_END)
			stream_frame_index++;
		break;
	case SR_DF_ANALOG:
		if (stream_stop_requested)
			break;
		stream_stop_requested = TRUE;
		json = g_string_new("{\"code\":\"analog_not_supported\",\"message\":\"");
		stream_json_escape(json, "v1 stream mode supports logic data only");
		g_string_append(json, "\"}");
		stream_write_frame(STREAM_FRAME_ERROR, json->str, json->len);
		g_string_free(json, TRUE);
		if (df_arg && df_arg->session)
			g_idle_add(stream_stop_idle, NULL);
		break;
	case SR_DF_END:
		stream_session_end(stream_limit_reached ? "samples-reached" : "device");
		break;
	default:
		break;
	}
}

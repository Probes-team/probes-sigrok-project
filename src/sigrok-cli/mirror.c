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
#include <gio/gio.h>
#include <string.h>

#include "sigrok-cli.h"
#include "mirror.h"
#include "stream_proto.h"

#define MIRROR_DESCRIPTOR_ENV "PROBES_SIGROK_IPC"
#define MIRROR_CONTROL_BYTES 256
/* Upper bound on bytes queued for a slow consumer. Beyond this the oldest
 * droppable (LOGIC) frames are discarded so a stalled reader can never stall
 * the capture itself. */
#define MIRROR_MAX_PENDING_BYTES (32 * 1024 * 1024)

typedef struct {
	GByteArray *bytes;
	gboolean droppable;
} MirrorFrame;

static GSocket *mirror_socket;
static GSource *mirror_source;
static GSource *mirror_out_source;
static GQueue *mirror_out;
static gsize mirror_out_bytes;
static struct sr_session *mirror_session;
static gboolean mirror_meta_sent;
static gboolean mirror_failed;
static guint16 mirror_unitsize;
static guint64 mirror_total_samples;
static guint32 mirror_sequence;

static gchar *mirror_descriptor_path(void)
{
	const gchar *override = g_getenv(MIRROR_DESCRIPTOR_ENV);

	if (override && *override)
		return g_strdup(override);
	return g_build_filename(g_get_user_data_dir(), "Probes", "sigrok", "ipc",
		"broker.json", NULL);
}

/* Minimal JSON scanning: we only read our own small descriptor, so a full
 * parser would be dead weight. Keys are matched as "key": value. */
static gboolean mirror_json_int(const gchar *json, const gchar *key, gint *out)
{
	gchar *needle = g_strdup_printf("\"%s\"", key);
	const gchar *p = strstr(json, needle);
	gboolean ok = FALSE;

	g_free(needle);
	if (!p)
		return FALSE;
	p = strchr(p, ':');
	if (!p)
		return FALSE;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '\0')
		return FALSE;
	*out = (gint)g_ascii_strtoll(p, NULL, 10);
	ok = TRUE;
	return ok;
}

static gchar *mirror_json_string(const gchar *json, const gchar *key)
{
	gchar *needle = g_strdup_printf("\"%s\"", key);
	const gchar *p = strstr(json, needle);
	const gchar *start, *end;
	gchar *value = NULL;

	g_free(needle);
	if (!p)
		return NULL;
	p = strchr(p, ':');
	if (!p)
		return NULL;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p != '"')
		return NULL;
	start = p + 1;
	end = strchr(start, '"');
	if (!end)
		return NULL;
	value = g_strndup(start, (gsize)(end - start));
	return value;
}

static void mirror_flush(void);

/* Write-ready handler: resume a buffered send after a transient stall. */
static gboolean mirror_out_ready(GSocket *socket, GIOCondition condition, gpointer data)
{
	GSource *source = mirror_out_source;

	(void)socket;
	(void)data;
	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		mirror_failed = TRUE;
		mirror_out_source = NULL;
		if (source)
			g_source_unref(source);
		return G_SOURCE_REMOVE;
	}
	mirror_flush();
	if (mirror_failed || !mirror_out || mirror_out->length == 0) {
		mirror_out_source = NULL;
		if (source)
			g_source_unref(source);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void mirror_ensure_out_source(void)
{
	if (mirror_out_source || mirror_failed || !mirror_socket)
		return;
	mirror_out_source = g_socket_create_source(mirror_socket, G_IO_OUT, NULL);
	g_source_set_callback(mirror_out_source, (GSourceFunc)mirror_out_ready, NULL, NULL);
	g_source_attach(mirror_out_source, NULL);
}

/*
 * Sends as much of the queued frames as the socket accepts. A non-blocking
 * socket reports a full send buffer as G_IO_ERROR_WOULD_BLOCK; that is not a
 * failure, so the frame stays queued and the write-ready watch flushes it
 * later. Only a real socket error permanently disables the mirror.
 */
static void mirror_flush(void)
{
	if (mirror_failed || !mirror_socket || !mirror_out)
		return;
	while (mirror_out->length > 0) {
		MirrorFrame *frame = mirror_out->head->data;
		gsize offset = 0;

		while (offset < frame->bytes->len) {
			GError *error = NULL;
			gssize n = g_socket_send(mirror_socket,
				(const gchar *)frame->bytes->data + offset,
				frame->bytes->len - offset, NULL, &error);

			if (n > 0) {
				offset += (gsize)n;
				continue;
			}
			if (n == 0 || (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))) {
				g_clear_error(&error);
				return;
			}
			if (error)
				g_error_free(error);
			mirror_failed = TRUE;
			return;
		}
		mirror_out_bytes -= frame->bytes->len;
		g_byte_array_free(frame->bytes, TRUE);
		g_free(frame);
		g_queue_pop_head(mirror_out);
	}
}

/* Keeps the queue bounded: the oldest droppable (LOGIC) frames go first, so a
 * stalled consumer loses resolution instead of stalling the capture. */
static void mirror_enforce_cap(void)
{
	GList *link = mirror_out->head;

	while (link && mirror_out_bytes > MIRROR_MAX_PENDING_BYTES) {
		MirrorFrame *frame = link->data;
		GList *next = link->next;

		if (frame->droppable) {
			mirror_out_bytes -= frame->bytes->len;
			g_byte_array_free(frame->bytes, TRUE);
			g_free(frame);
			g_queue_delete_link(mirror_out, link);
		}
		link = next;
	}
}

static void mirror_enqueue(GByteArray *bytes, gboolean droppable)
{
	MirrorFrame *frame = g_new0(MirrorFrame, 1);

	frame->bytes = bytes;
	frame->droppable = droppable;
	g_queue_push_tail(mirror_out, frame);
	mirror_out_bytes += bytes->len;
	mirror_enforce_cap();
	mirror_flush();
	if (mirror_out->length > 0 && !mirror_failed)
		mirror_ensure_out_source();
}

static void mirror_send_frame(guint8 type, const void *payload, guint32 length)
{
	GByteArray *frame;

	if (mirror_failed || !mirror_socket || !mirror_out)
		return;
	frame = g_byte_array_new();
	stream_proto_append_frame(frame, type, mirror_sequence++, payload, length);
	mirror_enqueue(frame, type == STREAM_FRAME_LOGIC);
}

static void mirror_send_meta(const struct sr_dev_inst *sdi, guint16 unitsize)
{
	GString *json = stream_build_meta(sdi, unitsize, "live");

	mirror_send_frame(STREAM_FRAME_META, json->str, json->len);
	g_string_free(json, TRUE);
	mirror_meta_sent = TRUE;
}

static void mirror_datafeed_in(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data)
{
	guint64 sample_count;
	guint32 length;

	(void)cb_data;
	if (mirror_failed)
		return;
	if (packet->type != SR_DF_LOGIC)
		return;

	{
		const struct sr_datafeed_logic *logic = packet->payload;

		if (!mirror_meta_sent || mirror_unitsize != logic->unitsize) {
			mirror_unitsize = logic->unitsize ? logic->unitsize : 1;
			mirror_send_meta(sdi, mirror_unitsize);
		}
		sample_count = logic->unitsize ? logic->length / logic->unitsize : 0;
		if (sample_count == 0)
			return;
		length = (guint32)(sample_count * logic->unitsize);
		{
			GByteArray *payload = g_byte_array_sized_new(20 + length);
			guint8 header[20];
			int i;

			for (i = 0; i < 8; i++)
				header[i] = (mirror_total_samples >> (8 * i)) & 0xff;
			for (i = 0; i < 8; i++)
				header[8 + i] = (sample_count >> (8 * i)) & 0xff;
			header[16] = logic->unitsize & 0xff;
			header[17] = (logic->unitsize >> 8) & 0xff;
			header[18] = 0;
			header[19] = 0;
			g_byte_array_append(payload, header, sizeof(header));
			g_byte_array_append(payload, logic->data, length);
			mirror_send_frame(STREAM_FRAME_LOGIC, payload->data, payload->len);
			g_byte_array_free(payload, TRUE);
		}
		mirror_total_samples += sample_count;
	}
}

static gboolean mirror_control_in(GSocket *socket, GIOCondition condition,
		gpointer data)
{
	gchar buffer[MIRROR_CONTROL_BYTES];
	gssize n;

	(void)data;
	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		mirror_failed = TRUE;
		return G_SOURCE_REMOVE;
	}
	n = g_socket_receive(socket, buffer, sizeof(buffer) - 1, NULL, NULL);
	if (n > 0) {
		buffer[n] = '\0';
		if (strstr(buffer, "stop") && mirror_session)
			sr_session_stop(mirror_session);
	} else if (n == 0) {
		mirror_failed = TRUE;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

int mirror_attach(struct sr_session *session)
{
	gchar *path, *json, *host, *token;
	gint port = 0;
	GSocketClient *client;
	GSocketConnection *connection;
	GError *error = NULL;

	if (mirror_socket)
		return 0;

	path = mirror_descriptor_path();
	json = NULL;
	g_file_get_contents(path, &json, NULL, NULL);
	g_free(path);
	if (!json)
		return 0;
	if (!mirror_json_int(json, "port", &port) || port <= 0) {
		g_free(json);
		return 0;
	}
	host = mirror_json_string(json, "host");
	token = mirror_json_string(json, "token");
	g_free(json);

	client = g_socket_client_new();
	connection = g_socket_client_connect_to_host(client,
		host ? host : "127.0.0.1", (guint16)port, NULL, &error);
	g_object_unref(client);
	g_free(host);
	if (!connection) {
		if (error)
			g_error_free(error);
		g_free(token);
		return 0;
	}
	mirror_socket = g_socket_connection_get_socket(connection);
	g_object_ref(mirror_socket);
	g_object_unref(connection);
	g_socket_set_blocking(mirror_socket, FALSE);
	mirror_out = g_queue_new();
	mirror_out_bytes = 0;
	mirror_failed = FALSE;

	if (token) {
		gchar *hello = g_strdup_printf("{\"token\":\"%s\"}\n", token);
		GByteArray *bytes = g_byte_array_sized_new(strlen(hello));

		g_byte_array_append(bytes, (const guint8 *)hello, strlen(hello));
		g_free(hello);
		mirror_enqueue(bytes, FALSE);
	}
	g_free(token);

	mirror_session = session;
	mirror_source = g_socket_create_source(mirror_socket,
		G_IO_IN | G_IO_HUP | G_IO_ERR, NULL);
	g_source_set_callback(mirror_source, (GSourceFunc)mirror_control_in, NULL, NULL);
	g_source_attach(mirror_source, NULL);

	sr_session_datafeed_callback_add(session, mirror_datafeed_in, NULL);
	return 0;
}

void mirror_detach(void)
{
	if (mirror_socket && mirror_out) {
		const gchar *reason = "{\"reason\":\"stopped\"}";
		int i;

		mirror_send_frame(STREAM_FRAME_SESSION_END, reason, (guint32)strlen(reason));
		/* Bounded chance for a briefly-stalled consumer to take the tail. */
		for (i = 0; i < 200 && !mirror_failed && mirror_out->length > 0; i++) {
			mirror_flush();
			if (mirror_out->length == 0)
				break;
			g_usleep(1000);
		}
	}
	if (mirror_source) {
		g_source_destroy(mirror_source);
		g_source_unref(mirror_source);
		mirror_source = NULL;
	}
	if (mirror_out_source) {
		g_source_destroy(mirror_out_source);
		g_source_unref(mirror_out_source);
		mirror_out_source = NULL;
	}
	if (mirror_socket) {
		g_socket_close(mirror_socket, NULL);
		g_object_unref(mirror_socket);
		mirror_socket = NULL;
	}
	if (mirror_out) {
		MirrorFrame *frame;

		while ((frame = g_queue_pop_head(mirror_out)) != NULL) {
			g_byte_array_free(frame->bytes, TRUE);
			g_free(frame);
		}
		g_queue_free(mirror_out);
		mirror_out = NULL;
	}
	mirror_out_bytes = 0;
	mirror_session = NULL;
	mirror_meta_sent = FALSE;
	mirror_failed = FALSE;
	mirror_unitsize = 0;
	mirror_total_samples = 0;
	mirror_sequence = 0;
}

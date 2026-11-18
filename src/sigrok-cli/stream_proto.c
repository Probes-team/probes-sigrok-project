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
#include <string.h>

#include "sigrok-cli.h"
#include "stream_proto.h"

void stream_json_escape(GString *json, const char *text)
{
	const unsigned char *p = (const unsigned char *)(text ? text : "");

	for (; *p; p++) {
		switch (*p) {
		case '"': g_string_append(json, "\\\""); break;
		case '\\': g_string_append(json, "\\\\"); break;
		case '\n': g_string_append(json, "\\n"); break;
		case '\r': g_string_append(json, "\\r"); break;
		case '\t': g_string_append(json, "\\t"); break;
		default:
			if (*p < 0x20)
				g_string_append_printf(json, "\\u%04x", *p);
			else
				g_string_append_c(json, (gchar)*p);
		}
	}
}

static const char *stream_channel_type_name(int type)
{
	return type == SR_CHANNEL_LOGIC ? "logic" : "other";
}

GString *stream_build_meta(const struct sr_dev_inst *sdi, guint16 unitsize,
		const char *source)
{
	struct sr_dev_driver *driver = sr_dev_inst_driver_get(sdi);
	GSList *channels = sr_dev_inst_channels_get(sdi), *l;
	GVariant *gvar = NULL;
	GString *json = g_string_new("{");
	guint64 samplerate = 0;
	gchar *device_mode = NULL;
	gboolean continuous = FALSE;
	gdouble threshold_low = 0.0, threshold_high = 0.0;
	gboolean has_threshold = FALSE;
	gboolean first = TRUE;

	if (maybe_config_get(driver, sdi, NULL, SR_CONF_SAMPLERATE, &gvar) == SR_OK) {
		samplerate = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
	}
	if (maybe_config_get(driver, sdi, NULL, SR_CONF_CONTINUOUS, &gvar) == SR_OK) {
		continuous = g_variant_get_boolean(gvar);
		g_variant_unref(gvar);
	}
	if (maybe_config_get(driver, sdi, NULL, SR_CONF_DEVICE_MODE, &gvar) == SR_OK) {
		device_mode = g_strdup(g_variant_get_string(gvar, NULL));
		g_variant_unref(gvar);
	}
	if (maybe_config_get(driver, sdi, NULL, SR_CONF_VOLTAGE_THRESHOLD, &gvar) == SR_OK) {
		if (g_variant_is_of_type(gvar, G_VARIANT_TYPE("(dd)"))) {
			g_variant_get(gvar, "(dd)", &threshold_low, &threshold_high);
			has_threshold = TRUE;
		}
		g_variant_unref(gvar);
	}

	g_string_append(json, "\"driver\":\"");
	stream_json_escape(json, driver->name);
	g_string_append(json, "\",\"mode\":\"");
	stream_json_escape(json, source ? source : "live");
	g_string_append(json, "\",");
	g_string_append_printf(json, "\"samplerate\":%" G_GUINT64_FORMAT ",", samplerate);
	g_string_append_printf(json, "\"unitsize\":%u,", unitsize);
	if (has_threshold)
		g_string_append_printf(json, "\"voltageThreshold\":{\"min\":%g,\"max\":%g},",
			threshold_low, threshold_high);
	g_string_append(json, "\"channels\":[");
	for (l = channels; l; l = l->next) {
		struct sr_channel *channel = l->data;
		if (!channel->enabled)
			continue;
		if (!first)
			g_string_append_c(json, ',');
		first = FALSE;
		g_string_append_printf(json, "{\"index\":%d,\"name\":\"", channel->index);
		stream_json_escape(json, channel->name);
		g_string_append_printf(json, "\",\"type\":\"%s\",\"enabled\":true}",
			stream_channel_type_name(channel->type));
	}
	g_string_append(json, "],\"capabilities\":{");
	g_string_append_printf(json, "\"continuous\":%s,", continuous ? "true" : "false");
	g_string_append(json, "\"stream\":true");
	if (device_mode) {
		g_string_append(json, ",\"deviceMode\":\"");
		stream_json_escape(json, device_mode);
		g_string_append_c(json, '"');
		g_free(device_mode);
	}
	g_string_append(json, "},\"streamProtocol\":1}");
	return json;
}

void stream_proto_append_frame(GByteArray *out, guint8 type, guint32 sequence,
		const void *payload, guint32 length)
{
	guint8 header[16];
	int i;

	memcpy(header, STREAM_MAGIC, 4);
	header[4] = STREAM_VERSION;
	header[5] = type;
	header[6] = 0;
	header[7] = 0;
	for (i = 0; i < 4; i++)
		header[8 + i] = (sequence >> (8 * i)) & 0xff;
	for (i = 0; i < 4; i++)
		header[12 + i] = (length >> (8 * i)) & 0xff;
	g_byte_array_append(out, header, sizeof(header));
	if (length && payload)
		g_byte_array_append(out, payload, length);
}
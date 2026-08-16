/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Q2H2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <config.h>
#include <string.h>

#include "protocol.h"

static const struct ch32h417_profile supported_devices[] = {
	{ USB_VENDOR_ID, USB_PRODUCT_ID, "WCH", "CH32H417 Logic Analyzer" },
	{ 0, 0, NULL, NULL },
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const char *const pattern_modes[] = { "logic" };

static const struct ch32h417_profile *find_profile(uint16_t vid, uint16_t pid)
{
	int index;

	for (index = 0; supported_devices[index].vid; index++) {
		if (supported_devices[index].vid == vid &&
				supported_devices[index].pid == pid)
			return &supported_devices[index];
	}
	return NULL;
}

static gboolean connection_requested(const char *requested, int index)
{
	char connection_id[32];

	if (!requested || !*requested)
		return TRUE;
	g_snprintf(connection_id, sizeof(connection_id), "ch375/%d", index);
	return !g_strcmp0(requested, connection_id);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *requested_connection = NULL;
	GSList *devices = NULL;
	GSList *entry;
	struct sr_config *config;
	int index;

	for (entry = options; entry; entry = entry->next) {
		config = entry->data;
		if (config->key == SR_CONF_CONN)
			requested_connection = g_variant_get_string(config->data, NULL);
	}
	if (ch375_init() != SR_OK) {
		sr_err("Packaged CH375DLL64.DLL is unavailable");
		return std_scan_complete(di, devices);
	}

	for (index = 0; index < CH375_MAX_NUMBER; index++) {
		void *handle;
		unsigned long usb_id;
		uint16_t vid;
		uint16_t pid;
		const struct ch32h417_profile *profile;
		struct sr_dev_inst *sdi;
		struct dev_context *devc;
		struct sr_channel_group *group;
		int channel_index;
		char connection_id[32];
		char channel_name[8];

		if (!connection_requested(requested_connection, index))
			continue;
		handle = ch375_open_device(index);
		if (!handle || handle == CH375_INVALID_HANDLE)
			continue;
		usb_id = ch375_get_usb_id(index);
		vid = usb_id & 0xffff;
		pid = usb_id >> 16;
		profile = find_profile(vid, pid);
		if (!profile) {
			ch375_close_device(index);
			continue;
		}

		sdi = g_malloc0(sizeof(*sdi));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(profile->vendor);
		sdi->model = g_strdup(profile->model);
		sdi->inst_type = SR_INST_USB;
		g_snprintf(connection_id, sizeof(connection_id), "ch375/%d", index);
		sdi->connection_id = g_strdup(connection_id);
		group = sr_channel_group_new(sdi, "Logic", NULL);
		for (channel_index = 0; channel_index < 16; channel_index++) {
			g_snprintf(channel_name, sizeof(channel_name), "D%d", channel_index);
			group->channels = g_slist_append(group->channels,
				sr_channel_new(sdi, channel_index, SR_CHANNEL_LOGIC, TRUE,
					channel_name));
		}

		devc = ch32h417_dev_new();
		devc->profile = profile;
		devc->device_index = index;
		devc->device_handle = handle;
		sdi->priv = devc;
		ch375_set_io_mode(index, 0);
		ch375_set_timeout_ex(index, USB_TIMEOUT, USB_TIMEOUT, USB_TIMEOUT, USB_TIMEOUT);
		devc->is_usb2 = ch32h417_detect_usb2(sdi);
		ch32h417_update_samplerates(devc);
		devices = g_slist_append(devices, sdi);
	}

	return std_scan_complete(di, devices);
}

static void clear_helper(void *priv)
{
	struct dev_context *devc = priv;

	if (!devc)
		return;
	if (devc->read_thread) {
		g_mutex_lock(&devc->lock);
		devc->acq_aborted = TRUE;
		g_mutex_unlock(&devc->lock);
		ch375_abort_endpoint_read(devc->device_index, PIPE_LOGIC_DATA);
		g_thread_join(devc->read_thread);
		devc->read_thread = NULL;
	}
	if (devc->device_handle && devc->device_handle != CH375_INVALID_HANDLE)
		ch375_close_device(devc->device_index);
	/* std_dev_clear_with_callback() releases devc after this callback. */
	ch32h417_dev_clear(devc);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	return ch32h417_open(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return ch32h417_close(sdi);
}

static int cleanup(const struct sr_dev_driver *di)
{
	int ret = std_cleanup(di);

	ch375_cleanup();
	return ret;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	const struct dev_context *devc;

	(void)cg;
	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	switch (key) {
	case SR_CONF_CONN:
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->limit_frames);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_NUM_LOGIC_CHANNELS:
		*data = g_variant_new_uint64(devc->num_channels);
		break;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_string(pattern_modes[0]);
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t value;
	int index;

	(void)cg;
	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
		devc->limit_frames = g_variant_get_uint64(data);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		return SR_OK;
	case SR_CONF_CAPTURE_RATIO:
		value = g_variant_get_uint64(data);
		if (value > 100)
			return SR_ERR_ARG;
		devc->capture_ratio = value;
		return SR_OK;
	case SR_CONF_SAMPLERATE:
		value = g_variant_get_uint64(data);
		for (index = 0; index < devc->num_samplerates; index++) {
			if (devc->samplerates[index] == value) {
				devc->cur_samplerate = value;
				return SR_OK;
			}
		}
		return SR_ERR_ARG;
	case SR_CONF_NUM_LOGIC_CHANNELS:
		value = g_variant_get_uint64(data);
		if (value != 8 && value != 16)
			return SR_ERR_ARG;
		devc->num_channels = value;
		devc->sample_width = value == 8 ? SAMPLE_WIDTH_8BIT : SAMPLE_WIDTH_16BIT;
		ch32h417_update_samplerates(devc);
		return SR_OK;
	case SR_CONF_PATTERN_MODE:
		return !g_strcmp0(g_variant_get_string(data, NULL), pattern_modes[0])
			? SR_OK : SR_ERR_ARG;
	default:
		return SR_ERR_NA;
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	const struct dev_context *devc = sdi ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		if (!devc)
			return SR_ERR_NA;
		*data = std_gvar_samplerates(devc->samplerates, devc->num_samplerates);
		return SR_OK;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		return SR_OK;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_strv(pattern_modes, ARRAY_SIZE(pattern_modes));
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static struct sr_dev_driver ch32h417_driver_info = {
	.name = "wch-ch32h417",
	.longname = "WCH CH32H417 Logic Analyzer",
	.api_version = 1,
	.init = std_init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = ch32h417_acquisition_start,
	.dev_acquisition_stop = ch32h417_acquisition_stop,
};

SR_REGISTER_DEV_DRIVER(ch32h417_driver_info);

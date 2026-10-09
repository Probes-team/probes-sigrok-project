/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <math.h>
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_DEVICE_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CONTINUOUS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_RLE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FILTER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_EXTERNAL_CLOCK | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CLOCK_EDGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const char *signal_edges[] = {
	[DS_EDGE_RISING] = "rising",
	[DS_EDGE_FALLING] = "falling",
};

static const char *operation_mode_names[] = {
	[DSLOGIC_OPERATION_BUFFER] = "buffer",
	[DSLOGIC_OPERATION_STREAM] = "stream",
	[DSLOGIC_OPERATION_INTERNAL_TEST] = "internal-test",
};

static const double thresholds[][2] = {
	{ 0.7, 1.4 },
	{ 1.4, 3.6 },
};

static unsigned int enabled_channel_count(const struct sr_dev_inst *sdi)
{
	const GSList *channels;
	unsigned int count = 0;

	for (channels = sdi->channels; channels; channels = channels->next) {
		const struct sr_channel *channel = channels->data;
		if (channel->enabled)
			count++;
	}

	return count;
}

static gboolean samplerate_is_available(const struct sr_dev_inst *sdi,
		uint64_t samplerate, gboolean stream)
{
	const struct dev_context *devc = sdi->priv;

	return devc && dslogic_profile_samplerate_is_available(devc->profile,
		enabled_channel_count(sdi), samplerate, stream);
}

static int operation_mode_from_string(const char *name,
		enum dslogic_operation_mode *operation_mode)
{
	enum dslogic_operation_mode candidate;

	if (!name || !operation_mode)
		return SR_ERR_ARG;
	for (candidate = DSLOGIC_OPERATION_BUFFER;
			candidate <= DSLOGIC_OPERATION_INTERNAL_TEST; candidate++) {
		if (!strcmp(name, operation_mode_names[candidate])) {
			*operation_mode = candidate;
			return SR_OK;
		}
	}

	return SR_ERR_ARG;
}

static int dslogic_apply_operation_mode(struct sr_dev_inst *sdi,
		enum dslogic_operation_mode operation_mode)
{
	struct dev_context *devc;
	const struct dslogic_channel_mode *mode;
	const GSList *channels;
	gboolean stream;
	GArray *samplerates;
	int ret;

	if (!sdi || !(devc = sdi->priv) || !devc->profile ||
			!dslogic_profile_operation_supported(devc->profile, operation_mode))
		return SR_ERR_NA;

	mode = dslogic_profile_mode_for_operation(devc->profile, operation_mode);
	if (!mode || mode->channel_count == 0 ||
			mode->channel_count > devc->profile->logic_channel_count)
		return SR_ERR_ARG;
	if (g_slist_length(sdi->channels) < mode->channel_count)
		return SR_ERR_ARG;

	/* DSView's mode transition owns the active probe count. */
	for (channels = sdi->channels; channels; channels = channels->next) {
		struct sr_channel *channel = channels->data;

		if ((ret = sr_dev_channel_enable(channel,
				channel->index < (int)mode->channel_count)) != SR_OK)
			return ret;
	}

	stream = dslogic_profile_operation_is_stream(devc->profile,
		operation_mode);
	if (operation_mode == DSLOGIC_OPERATION_INTERNAL_TEST) {
		/* Exact DSView derivation: buffered tests run at 100 MHz. */
		devc->cur_samplerate = stream ? mode->max_samplerate / 10 : SR_MHZ(100);
		devc->limit_samples = stream ? devc->cur_samplerate * 3 :
			devc->profile->mem_depth / enabled_channel_count(sdi);
	} else if (!devc->cur_samplerate || !samplerate_is_available(sdi,
			devc->cur_samplerate, stream)) {
		samplerates = dslogic_profile_samplerates_for_operation(devc->profile,
			enabled_channel_count(sdi), stream);
		if (!samplerates)
			return SR_ERR_MALLOC;
		if (samplerates->len == 0) {
			g_array_unref(samplerates);
			return SR_ERR_ARG;
		}
		devc->cur_samplerate = g_array_index(samplerates, uint64_t, 0);
		g_array_unref(samplerates);
	}

	devc->operation_mode = operation_mode;
	devc->selected_mode = mode;

	return SR_OK;
}

static const struct dslogic_profile *profile_for_device(
		const struct libusb_device_descriptor *des, enum libusb_speed usb_speed)
{
	return dslogic_profile_find(des->idVendor, des->idProduct, usb_speed);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct sr_config *src;
	const struct dslogic_profile *prof;
	GSList *l, *devices, *conn_devices;
	gboolean has_firmware;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	struct libusb_device_handle *hdl;
	int ret, i, j;
	int64_t firmware_updated;
	const char *conn;
	char manufacturer[64], product[64], serial_num[64], connection_id[64];
	char channel_name[16];

	drvc = di->context;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	/* Find all DSLogic compatible devices and upload firmware to them. */
	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		firmware_updated = 0;
		if (conn) {
			usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i])
					&& usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		libusb_get_device_descriptor(devlist[i], &des);

		prof = profile_for_device(&des, libusb_get_device_speed(devlist[i]));
		if (!prof)
			continue;

		if ((ret = libusb_open(devlist[i], &hdl)) < 0) {
			sr_warn("Failed to open potential device with "
				"VID:PID %04x:%04x: %s.", des.idVendor,
				des.idProduct, libusb_error_name(ret));
			continue;
		}

		if (des.iManufacturer == 0) {
			manufacturer[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iManufacturer, (unsigned char *) manufacturer,
				sizeof(manufacturer))) < 0) {
			sr_warn("Failed to get manufacturer string descriptor: %s.",
				libusb_error_name(ret));
			libusb_close(hdl);
			continue;
		}

		if (des.iProduct == 0) {
			product[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iProduct, (unsigned char *) product,
				sizeof(product))) < 0) {
			sr_warn("Failed to get product string descriptor: %s.",
				libusb_error_name(ret));
			libusb_close(hdl);
			continue;
		}

		if (des.iSerialNumber == 0) {
			serial_num[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iSerialNumber, (unsigned char *) serial_num,
				sizeof(serial_num))) < 0) {
			sr_warn("Failed to get serial number string descriptor: %s.",
				libusb_error_name(ret));
			libusb_close(hdl);
			continue;
		}

		libusb_close(hdl);

		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		/* DSView v1.3.2's DSL/dslogic.c identifies this exact descriptor
		 * pair as an already-loaded FX2 device. */
		has_firmware = usb_match_manuf_prod(devlist[i], "DreamSourceLab",
			"USB-based DSL Instrument v2");
		if (!has_firmware) {
			ret = dslogic_upload_fx2_if_needed(drvc->sr_ctx, devlist[i], prof,
				FALSE, &firmware_updated);
			if (ret != SR_OK) {
				sr_warn("Skipping DSLogic device %d.%d (%s): cold boot is unavailable (%s).",
					libusb_get_bus_number(devlist[i]),
					libusb_get_device_address(devlist[i]), prof->model,
					ret == SR_ERR_NA ? "no verified FX2 firmware" : "upload failed");
				continue;
			}
		}

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup(prof->vendor);
		sdi->model = g_strdup(prof->model);
		sdi->version = g_strdup(prof->model_version);
		sdi->serial_num = g_strdup(serial_num);
		sdi->connection_id = g_strdup(connection_id);

		/* Logic channels, all in one channel group. */
		cg = sr_channel_group_new(sdi, "Logic", NULL);
		for (j = 0; j < (int)prof->logic_channel_count; j++) {
			sprintf(channel_name, "%d", j);
			ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC,
						TRUE, channel_name);
			cg->channels = g_slist_append(cg->channels, ch);
		}

		devc = dslogic_dev_new();
		devc->profile = prof;
		sdi->priv = devc;
		if ((ret = dslogic_apply_operation_mode(sdi,
				prof->default_operation_mode)) != SR_OK) {
			sr_err("Failed to initialize %s operation mode.", prof->model);
			sdi->priv = NULL;
			g_free(devc);
			sr_dev_inst_free(sdi);
			continue;
		}
		devices = g_slist_append(devices, sdi);

		if (has_firmware) {
			/* Already has the firmware, so fix the new address. */
			sr_dbg("Found a DSLogic device.");
			sdi->status = SR_ST_INACTIVE;
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
					libusb_get_device_address(devlist[i]), NULL);
		} else {
			devc->fw_updated = firmware_updated;
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
					0xff, NULL);
		}
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct dev_context *devc;
	int ret;
	int64_t timediff_us, timediff_ms;

	devc = sdi->priv;

	/*
	 * If the firmware was recently uploaded, wait up to MAX_RENUM_DELAY_MS
	 * milliseconds for the FX2 to renumerate.
	 */
	ret = SR_ERR;
	if (devc->fw_updated > 0) {
		sr_info("Waiting for device to reset.");
		/* Takes >= 300ms for the FX2 to be gone from the USB bus. */
		g_usleep(300 * 1000);
		timediff_ms = 0;
		while (timediff_ms < MAX_RENUM_DELAY_MS) {
			if ((ret = dslogic_open_and_claim(sdi, di)) == SR_OK)
				break;
			g_usleep(100 * 1000);

			timediff_us = g_get_monotonic_time() - devc->fw_updated;
			timediff_ms = timediff_us / 1000;
			sr_spew("Waited %" PRIi64 "ms.", timediff_ms);
		}
		if (ret != SR_OK) {
			sr_err("Device failed to renumerate.");
			return SR_ERR;
		}
		sr_info("Device came back after %" PRIi64 "ms.", timediff_ms);
	} else {
		sr_info("Firmware upload was not needed.");
		ret = dslogic_open_and_claim(sdi, di);
	}

	if (ret != SR_OK) {
		sr_err("Unable to open device.");
		return SR_ERR;
	}

	if (devc->operation_mode == DSLOGIC_OPERATION_UNSET &&
			(ret = dslogic_apply_operation_mode(sdi,
				devc->profile->default_operation_mode)) != SR_OK)
		return ret;

	if (devc->cur_threshold == 0.0)
		devc->cur_threshold = thresholds[1][0];

	/* Read the device state before ever resetting PROG_B. */
	if ((ret = dslogic_prepare_fpga(sdi)) != SR_OK) {
		dslogic_close_and_release(sdi);
		return ret;
	}
	if ((ret = dslogic_set_voltage_threshold(sdi, devc->cur_threshold)) != SR_OK) {
		dslogic_close_and_release(sdi);
		return ret;
	}
	devc->threshold_needs_apply = FALSE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return dslogic_close_and_release(sdi);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int idx;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		if (usb->address == 255)
			/* Device still needs to re-enumerate after firmware
			 * upload, so we don't know its (future) address. */
			return SR_ERR;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		if (!(devc->profile->capabilities & DSLOGIC_CAP_VOLTAGE_THRESHOLD))
			return SR_ERR_NA;
		if (!strcmp(devc->profile->model, "DSLogic")) {
			if ((idx = std_double_tuple_idx_d0(devc->cur_threshold,
					ARRAY_AND_SIZE(thresholds))) < 0)
				return SR_ERR_BUG;
			*data = std_gvar_tuple_double(thresholds[idx][0],
					thresholds[idx][1]);
		} else {
			*data = std_gvar_tuple_double(devc->cur_threshold, devc->cur_threshold);
		}
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
	case SR_CONF_EXTERNAL_CLOCK:
		*data = g_variant_new_boolean(devc->external_clock);
		break;
	case SR_CONF_DEVICE_MODE:
		if (!dslogic_profile_operation_supported(devc->profile,
				devc->operation_mode))
			return SR_ERR_NA;
		*data = g_variant_new_string(operation_mode_names[devc->operation_mode]);
		break;
	case SR_CONF_CONTINUOUS:
		*data = g_variant_new_boolean(
			dslogic_operation_mode_is_continuous(devc->operation_mode));
		break;
	case SR_CONF_RLE:
		if (!(devc->profile->capabilities & DSLOGIC_CAP_RLE))
			return SR_ERR_NA;
		*data = g_variant_new_boolean(devc->rle_mode);
		break;
	case SR_CONF_FILTER:
		if (!(devc->profile->capabilities & DSLOGIC_CAP_FILTER))
			return SR_ERR_NA;
		*data = g_variant_new_boolean(devc->filter_mode);
		break;
	case SR_CONF_CLOCK_EDGE:
		idx = devc->clock_edge;
		if (idx >= (int)ARRAY_SIZE(signal_edges))
			return SR_ERR_BUG;
		*data = g_variant_new_string(signal_edges[idx]);
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
	enum dslogic_operation_mode operation_mode;
	int idx, ret;
	gdouble low, high;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		if (devc->operation_mode == DSLOGIC_OPERATION_INTERNAL_TEST)
			return SR_ERR_NA;
		if (!samplerate_is_available(sdi, g_variant_get_uint64(data),
				dslogic_profile_operation_is_stream(devc->profile,
					devc->operation_mode)))
			return SR_ERR_ARG;
		devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (devc->operation_mode == DSLOGIC_OPERATION_INTERNAL_TEST)
			return SR_ERR_NA;
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_CAPTURE_RATIO:
		if (g_variant_get_uint64(data) > 100)
			return SR_ERR_ARG;
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		if (!(devc->profile->capabilities & DSLOGIC_CAP_VOLTAGE_THRESHOLD))
			return SR_ERR_NA;
		if (!strcmp(devc->profile->model, "DSLogic")) {
			if ((idx = std_double_tuple_idx(data, ARRAY_AND_SIZE(thresholds))) < 0)
				return SR_ERR_ARG;
			devc->cur_threshold = thresholds[idx][0];
			devc->fpga_needs_upload = TRUE;
		} else {
			g_variant_get(data, "(dd)", &low, &high);
			if (low > high || low < 0.0 || high > 5.0)
				return SR_ERR_ARG;
			devc->cur_threshold = (low + high) / 2.0;
		}
		/* A config setter is state-only; apply it in the acquisition path. */
		devc->threshold_needs_apply = TRUE;
		break;
	case SR_CONF_EXTERNAL_CLOCK:
		devc->external_clock = g_variant_get_boolean(data);
		break;
	case SR_CONF_DEVICE_MODE:
		if ((ret = operation_mode_from_string(g_variant_get_string(data, NULL),
				&operation_mode)) != SR_OK)
			return ret;
		return dslogic_apply_operation_mode((struct sr_dev_inst *)sdi,
			operation_mode);
	case SR_CONF_CONTINUOUS:
		if (g_variant_get_boolean(data))
			return dslogic_apply_operation_mode((struct sr_dev_inst *)sdi,
				DSLOGIC_OPERATION_STREAM);
		if (!dslogic_profile_operation_supported(devc->profile,
				DSLOGIC_OPERATION_BUFFER))
			return SR_ERR_ARG;
		return dslogic_apply_operation_mode((struct sr_dev_inst *)sdi,
			DSLOGIC_OPERATION_BUFFER);
	case SR_CONF_RLE:
		if (!(devc->profile->capabilities & DSLOGIC_CAP_RLE))
			return SR_ERR_NA;
		devc->rle_mode = g_variant_get_boolean(data);
		break;
	case SR_CONF_FILTER:
		if (!(devc->profile->capabilities & DSLOGIC_CAP_FILTER))
			return SR_ERR_NA;
		devc->filter_mode = g_variant_get_boolean(data);
		break;
	case SR_CONF_CLOCK_EDGE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(signal_edges))) < 0)
			return SR_ERR_ARG;
		devc->clock_edge = idx;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_VOLTAGE_THRESHOLD:
		if (!devc || !devc->profile)
			return SR_ERR_ARG;
		if (!(devc->profile->capabilities & DSLOGIC_CAP_VOLTAGE_THRESHOLD))
			return SR_ERR_NA;
		if (!strcmp(devc->profile->model, "DSLogic"))
			*data = std_gvar_thresholds(ARRAY_AND_SIZE(thresholds));
		else
			*data = std_gvar_min_max_step_thresholds(0.0, 5.0, 0.1);
		break;
	case SR_CONF_SAMPLERATE:
	{
		GArray *samplerates;

		if (!devc)
			return SR_ERR_ARG;
		if (devc->operation_mode == DSLOGIC_OPERATION_INTERNAL_TEST) {
			*data = std_gvar_samplerates(&devc->cur_samplerate, 1);
			break;
		}
		samplerates = dslogic_profile_samplerates_for_operation(devc->profile,
			enabled_channel_count(sdi), dslogic_profile_operation_is_stream(
				devc->profile, devc->operation_mode));
		if (!samplerates)
			return SR_ERR_MALLOC;
		*data = std_gvar_samplerates((const uint64_t *)samplerates->data,
			samplerates->len);
		g_array_unref(samplerates);
		break;
	}
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_CLOCK_EDGE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(signal_edges));
		break;
	case SR_CONF_RLE:
	case SR_CONF_FILTER:
		if (!devc || !devc->profile)
			return SR_ERR_ARG;
	if (key == SR_CONF_RLE &&
				!(devc->profile->capabilities & DSLOGIC_CAP_RLE))
			return SR_ERR_NA;
		if (key == SR_CONF_FILTER &&
				!(devc->profile->capabilities & DSLOGIC_CAP_FILTER))
			return SR_ERR_NA;
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_DEVICE_MODE:
	{
		const gchar *modes[4];
		size_t count = 0;
		enum dslogic_operation_mode operation_mode;

		if (!devc || !devc->profile)
			return SR_ERR_ARG;
		for (operation_mode = DSLOGIC_OPERATION_BUFFER;
				operation_mode <= DSLOGIC_OPERATION_INTERNAL_TEST;
				operation_mode++) {
			if (dslogic_profile_operation_supported(devc->profile,
					operation_mode))
				modes[count++] = operation_mode_names[operation_mode];
		}
		*data = g_variant_new_strv(modes, count);
		break;
	}
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static struct sr_dev_driver dreamsourcelab_dslogic_driver_info = {
	.name = "dreamsourcelab-dslogic",
	.longname = "DreamSourceLab DSLogic",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dslogic_acquisition_start,
	.dev_acquisition_stop = dslogic_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(dreamsourcelab_dslogic_driver_info);

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

static const uint64_t logic_samplerates_usb3[] = {
	SR_MHZ(200), SR_MHZ(187.5), SR_MHZ(175), SR_MHZ(162.5),
	SR_MHZ(156.25), SR_MHZ(150), SR_MHZ(137.5), SR_MHZ(125),
	SR_MHZ(112.5), SR_MHZ(100), SR_MHZ(93.75), SR_MHZ(87.5),
	SR_MHZ(81.5), SR_MHZ(75.125), SR_MHZ(75), SR_MHZ(68.75),
};

static const uint64_t logic_samplerates_usb2_16ch[] = { SR_MHZ(20) };
static const uint64_t logic_samplerates_usb2_8ch[] = { SR_MHZ(40) };

static void ch375_init_comm_params(const struct dev_context *devc)
{
	if (!ch375_set_io_mode(devc->device_index, 0))
		sr_warn("Unable to set synchronous CH375 I/O mode");
	if (!ch375_set_timeout_ex(devc->device_index, USB_TIMEOUT, USB_TIMEOUT,
			USB_TIMEOUT, USB_TIMEOUT))
		sr_warn("Unable to set CH375 timeouts");
}

SR_PRIV void ch32h417_update_samplerates(struct dev_context *devc)
{
	if (!devc->is_usb2) {
		devc->samplerates = logic_samplerates_usb3;
		devc->num_samplerates = ARRAY_SIZE(logic_samplerates_usb3);
	} else if (devc->num_channels == 8) {
		devc->samplerates = logic_samplerates_usb2_8ch;
		devc->num_samplerates = ARRAY_SIZE(logic_samplerates_usb2_8ch);
	} else {
		devc->samplerates = logic_samplerates_usb2_16ch;
		devc->num_samplerates = ARRAY_SIZE(logic_samplerates_usb2_16ch);
	}
	devc->cur_samplerate = devc->samplerates[0];
}

SR_PRIV struct dev_context *ch32h417_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(*devc));
	devc->num_channels = 16;
	devc->sample_width = SAMPLE_WIDTH_16BIT;
	devc->limit_frames = 1;
	devc->device_index = -1;
	devc->device_handle = CH375_INVALID_HANDLE;
	devc->buffer_size = BUFFER_UPLOAD_SIZE;
	devc->samplerates = logic_samplerates_usb3;
	devc->num_samplerates = ARRAY_SIZE(logic_samplerates_usb3);
	devc->cur_samplerate = logic_samplerates_usb3[0];
	g_mutex_init(&devc->lock);

	return devc;
}

SR_PRIV void ch32h417_dev_clear(struct dev_context *devc)
{
	if (!devc)
		return;
	if (devc->stl)
		soft_trigger_logic_free(devc->stl);
	g_mutex_clear(&devc->lock);
}

SR_PRIV gboolean ch32h417_detect_usb2(const struct sr_dev_inst *sdi)
{
	const struct dev_context *devc = sdi->priv;
	uint8_t descriptor[18];
	unsigned long length = sizeof(descriptor);
	uint16_t bcd_usb;

	if (!ch375_get_device_descr(devc->device_index, descriptor, &length) || length < 4) {
		sr_warn("Unable to read USB descriptor, assuming USB 3 mode");
		return FALSE;
	}
	bcd_usb = descriptor[2] | (descriptor[3] << 8);
	return bcd_usb < 0x0300;
}

SR_PRIV int ch32h417_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (!ch375_is_loaded() && ch375_init() != SR_OK)
		return SR_ERR;
	if (devc->device_handle == CH375_INVALID_HANDLE || !devc->device_handle) {
		devc->device_handle = ch375_open_device(devc->device_index);
		if (devc->device_handle == CH375_INVALID_HANDLE || !devc->device_handle) {
			sr_err("Unable to open CH375 device index %d", devc->device_index);
			return SR_ERR;
		}
	}

	ch375_init_comm_params(devc);
	devc->is_usb2 = ch32h417_detect_usb2(sdi);
	ch32h417_update_samplerates(devc);
	return SR_OK;
}

SR_PRIV int ch32h417_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->device_handle && devc->device_handle != CH375_INVALID_HANDLE) {
		ch375_close_device(devc->device_index);
		devc->device_handle = CH375_INVALID_HANDLE;
	}
	return SR_OK;
}

SR_PRIV int ch32h417_send_command(struct sr_dev_inst *sdi,
	uint8_t command, uint8_t length, const uint8_t *data)
{
	struct dev_context *devc = sdi->priv;
	uint8_t packet[PACKET_SIZE_OUT] = { 0 };
	unsigned long packet_length = sizeof(packet);

	if (length > sizeof(packet) - 2)
		return SR_ERR_ARG;
	packet[0] = command;
	packet[1] = length;
	if (length)
		memcpy(&packet[2], data, length);
	if (!ch375_write_endpoint(devc->device_index, PIPE_CMD, packet, &packet_length)) {
		sr_err("CH375 command 0x%02x write failed", command);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int ch32h417_receive_response(struct sr_dev_inst *sdi,
	uint8_t command, uint8_t *data, int *length)
{
	struct dev_context *devc = sdi->priv;
	uint8_t packet[PACKET_SIZE_IN] = { 0 };
	unsigned long packet_length = sizeof(packet);
	uint8_t response_length;

	if (!ch375_read_endpoint(devc->device_index, PIPE_CMD, packet, &packet_length) ||
			packet_length < 3) {
		sr_err("CH375 command response read failed");
		return SR_ERR;
	}
	if (packet[0] != command || packet[2] != 0) {
		sr_err("CH375 command response rejected: command=0x%02x status=0x%02x",
			packet[0], packet[2]);
		return SR_ERR;
	}
	response_length = packet[1];
	if (response_length > packet_length - 2)
		return SR_ERR_DATA;
	if (data && response_length > 1)
		memcpy(data, &packet[3], response_length - 1);
	if (length)
		*length = response_length ? response_length - 1 : 0;
	return SR_OK;
}

SR_PRIV int ch32h417_set_logic_params(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t params[2];
	int rate_index;

	for (rate_index = 0; rate_index < devc->num_samplerates; rate_index++) {
		if (devc->samplerates[rate_index] == devc->cur_samplerate)
			break;
	}
	if (rate_index == devc->num_samplerates)
		return SR_ERR_ARG;
	params[0] = devc->sample_width;
	params[1] = rate_index;
	if (ch32h417_send_command((struct sr_dev_inst *)sdi,
			CMD_SET_LOGIC_PARAMS, sizeof(params), params) != SR_OK)
		return SR_ERR;
	return ch32h417_receive_response((struct sr_dev_inst *)sdi,
		RESP_SET_LOGIC_PARAMS, NULL, NULL);
}

SR_PRIV int ch32h417_start_logic(const struct sr_dev_inst *sdi)
{
	return ch32h417_send_command((struct sr_dev_inst *)sdi,
		CMD_START_LOGIC, 0, NULL);
}

SR_PRIV int ch32h417_stop_logic(const struct sr_dev_inst *sdi)
{
	return ch32h417_send_command((struct sr_dev_inst *)sdi,
		CMD_STOP_LOGIC, 0, NULL);
}

static gboolean acquisition_should_stop(struct dev_context *devc)
{
	gboolean stop;

	g_mutex_lock(&devc->lock);
	stop = devc->acq_aborted || devc->acq_finished;
	g_mutex_unlock(&devc->lock);
	return stop;
}

static void mark_acquisition_finished(struct dev_context *devc)
{
	g_mutex_lock(&devc->lock);
	devc->acq_finished = TRUE;
	g_mutex_unlock(&devc->lock);
}

static gboolean send_logic_data(struct sr_dev_inst *sdi, uint8_t *data,
	size_t sample_count, size_t unitsize)
{
	struct dev_context *devc = sdi->priv;
	size_t offset = 0;

	while (sample_count) {
		size_t emit_count = sample_count;
		struct sr_datafeed_logic logic = {
			.length = emit_count * unitsize,
			.unitsize = unitsize,
			.data = data + offset * unitsize,
		};
		const struct sr_datafeed_packet packet = {
			.type = SR_DF_LOGIC,
			.payload = &logic,
		};

		if (devc->limit_samples) {
			uint64_t remaining = devc->limit_samples - devc->sent_samples;
			if (!remaining)
				remaining = devc->limit_samples;
			emit_count = MIN((uint64_t)emit_count, remaining);
			logic.length = emit_count * unitsize;
		}
		sr_session_send(sdi, &packet);
		offset += emit_count;
		sample_count -= emit_count;
		devc->sent_samples += emit_count;

		if (devc->limit_samples && devc->sent_samples == devc->limit_samples) {
			devc->num_frames++;
			devc->sent_samples = 0;
			if (devc->limit_frames && devc->num_frames >= devc->limit_frames) {
				mark_acquisition_finished(devc);
				return FALSE;
			}
		}
	}
	return TRUE;
}

static gpointer data_read_thread(gpointer user_data)
{
	struct sr_dev_inst *sdi = user_data;
	struct dev_context *devc = sdi->priv;
	uint8_t *buffer;
	size_t unitsize = devc->num_channels == 8 ? 1 : 2;
	unsigned int empty_reads = 0;

	buffer = g_try_malloc(devc->buffer_size);
	if (!buffer) {
		sr_err("Unable to allocate CH32H417 receive buffer");
		mark_acquisition_finished(devc);
		return NULL;
	}

	while (!acquisition_should_stop(devc)) {
		unsigned long transfer_count = 0;
		unsigned long total_data_length = 0;
		unsigned long length = devc->buffer_size;
		size_t sample_count;

		if (!ch375_query_buf_upload_ex(devc->device_index, PIPE_LOGIC_DATA,
				&transfer_count, &total_data_length)) {
			g_usleep(1000);
			continue;
		}
		if (!transfer_count || !total_data_length) {
			if (++empty_reads >= 500) {
				sr_warn("CH32H417 capture timed out waiting for data");
				mark_acquisition_finished(devc);
				break;
			}
			g_usleep(1000);
			continue;
		}
		empty_reads = 0;
		if (!ch375_read_endpoint(devc->device_index, PIPE_LOGIC_DATA,
				buffer, &length)) {
			sr_warn("CH32H417 logic endpoint read failed");
			mark_acquisition_finished(devc);
			break;
		}
		sample_count = length / unitsize;
		if (!sample_count)
			continue;

		if (!devc->trigger_fired && devc->stl) {
			int pre_trigger_samples = 0;
			int trigger_offset = soft_trigger_logic_check(devc->stl, buffer,
				sample_count * unitsize, &pre_trigger_samples);
			if (trigger_offset < 0)
				continue;
			std_session_send_df_trigger(sdi);
			devc->trigger_fired = TRUE;
			if (!send_logic_data(sdi, buffer + trigger_offset * unitsize,
				sample_count - trigger_offset, unitsize))
				break;
		} else if (!send_logic_data(sdi, buffer, sample_count, unitsize)) {
			break;
		}
	}

	g_free(buffer);
	return NULL;
}

static int acquisition_check_cb(int fd, int revents, void *user_data)
{
	struct sr_dev_inst *sdi = user_data;
	struct dev_context *devc = sdi->priv;

	(void)fd;
	(void)revents;
	if (acquisition_should_stop(devc)) {
		sr_dev_acquisition_stop(sdi);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

SR_PRIV int ch32h417_acquisition_start(const struct sr_dev_inst *sdi_const)
{
	struct sr_dev_inst *sdi = (struct sr_dev_inst *)sdi_const;
	struct dev_context *devc = sdi->priv;
	struct sr_trigger *trigger;
	int pre_trigger_samples = 0;

	if (devc->read_thread)
		return SR_ERR;
	devc->sent_samples = 0;
	devc->num_frames = 0;
	devc->end_sent = FALSE;
	devc->acq_aborted = FALSE;
	devc->acq_finished = FALSE;
	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}

	if (ch32h417_stop_logic(sdi) != SR_OK)
		sr_warn("CH32H417 pre-capture stop command failed");
	ch375_clear_buf_upload(devc->device_index, PIPE_LOGIC_DATA);
	if (!ch375_set_buf_upload_ex(devc->device_index, 1, PIPE_LOGIC_DATA,
			devc->buffer_size) || ch32h417_set_logic_params(sdi) != SR_OK ||
			ch32h417_start_logic(sdi) != SR_OK)
		return SR_ERR;

	trigger = sr_session_trigger_get(sdi->session);
	if (trigger) {
		if (devc->limit_samples)
			pre_trigger_samples = devc->capture_ratio * devc->limit_samples / 100;
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre_trigger_samples);
		if (!devc->stl)
			return SR_ERR_MALLOC;
		devc->trigger_fired = FALSE;
	} else {
		devc->trigger_fired = TRUE;
	}

	if (std_session_send_df_header(sdi) != SR_OK)
		return SR_ERR;
	devc->read_thread = g_thread_new("ch32h417-read", data_read_thread, sdi);
	if (!devc->read_thread)
		return SR_ERR_MALLOC;
	if (sr_session_source_add(sdi->session, -1, 0, 5,
			acquisition_check_cb, sdi) != SR_OK) {
		ch32h417_acquisition_stop(sdi);
		return SR_ERR;
	}
	devc->session_source_active = TRUE;
	return SR_OK;
}

SR_PRIV int ch32h417_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	g_mutex_lock(&devc->lock);
	devc->acq_aborted = TRUE;
	g_mutex_unlock(&devc->lock);
	if (devc->session_source_active) {
		sr_session_source_remove(sdi->session, -1);
		devc->session_source_active = FALSE;
	}
	ch375_abort_endpoint_read(devc->device_index, PIPE_LOGIC_DATA);
	if (devc->read_thread) {
		g_thread_join(devc->read_thread);
		devc->read_thread = NULL;
	}
	ch32h417_stop_logic(sdi);
	ch375_clear_buf_upload(devc->device_index, PIPE_LOGIC_DATA);
	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}
	if (!devc->end_sent) {
		std_session_send_df_end(sdi);
		devc->end_sent = TRUE;
	}
	return SR_OK;
}

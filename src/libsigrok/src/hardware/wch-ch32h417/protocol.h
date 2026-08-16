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

#ifndef LIBSIGROK_HARDWARE_WCH_CH32H417_PROTOCOL_H
#define LIBSIGROK_HARDWARE_WCH_CH32H417_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <libsigrok/libsigrok.h>

#include "libsigrok-internal.h"
#include "ch375_wrapper.h"

#define LOG_PREFIX "wch-ch32h417"

#define USB_VENDOR_ID 0x1a86
#define USB_PRODUCT_ID 0x5537
#define USB_TIMEOUT 500

#define PIPE_CMD 1
#define PIPE_LOGIC_DATA 2

#define PACKET_SIZE_OUT 64
#define PACKET_SIZE_IN 32
#define BUFFER_UPLOAD_SIZE (1024 * 1024)

#define CMD_SET_LOGIC_PARAMS 0xa0
#define CMD_START_LOGIC 0xa2
#define CMD_STOP_LOGIC 0xa3
#define CMD_GET_LOGIC_PARAMS 0xa4

#define RESP_SET_LOGIC_PARAMS 0xb0
#define RESP_GET_LOGIC_PARAMS 0xb4

#define SAMPLE_WIDTH_8BIT 0x08
#define SAMPLE_WIDTH_16BIT 0x10

struct ch32h417_profile {
	uint16_t vid;
	uint16_t pid;
	const char *vendor;
	const char *model;
};

struct dev_context {
	const struct ch32h417_profile *profile;
	const uint64_t *samplerates;
	int num_samplerates;
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t limit_frames;
	uint64_t capture_ratio;
	uint64_t sent_samples;
	uint64_t num_frames;
	uint8_t sample_width;
	uint8_t num_channels;
	gboolean is_usb2;
	int device_index;
	void *device_handle;
	size_t buffer_size;
	GThread *read_thread;
	GMutex lock;
	gboolean acq_aborted;
	gboolean acq_finished;
	gboolean session_source_active;
	gboolean end_sent;
	gboolean trigger_fired;
	struct soft_trigger_logic *stl;
};

SR_PRIV struct dev_context *ch32h417_dev_new(void);
SR_PRIV void ch32h417_dev_clear(struct dev_context *devc);
SR_PRIV void ch32h417_update_samplerates(struct dev_context *devc);
SR_PRIV int ch32h417_open(struct sr_dev_inst *sdi);
SR_PRIV int ch32h417_close(struct sr_dev_inst *sdi);
SR_PRIV gboolean ch32h417_detect_usb2(const struct sr_dev_inst *sdi);

SR_PRIV int ch32h417_send_command(struct sr_dev_inst *sdi,
	uint8_t command, uint8_t length, const uint8_t *data);
SR_PRIV int ch32h417_receive_response(struct sr_dev_inst *sdi,
	uint8_t command, uint8_t *data, int *length);
SR_PRIV int ch32h417_set_logic_params(const struct sr_dev_inst *sdi);
SR_PRIV int ch32h417_start_logic(const struct sr_dev_inst *sdi);
SR_PRIV int ch32h417_stop_logic(const struct sr_dev_inst *sdi);
SR_PRIV int ch32h417_acquisition_start(const struct sr_dev_inst *sdi);
SR_PRIV int ch32h417_acquisition_stop(struct sr_dev_inst *sdi);

#endif

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

#ifndef LIBSIGROK_HARDWARE_DREAMSOURCELAB_DSLOGIC_PROTOCOL_H
#define LIBSIGROK_HARDWARE_DREAMSOURCELAB_DSLOGIC_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "profiles.h"

#define LOG_PREFIX "dreamsourcelab-dslogic"

#define USB_INTERFACE		0
#define USB_CONFIGURATION	1

#define MAX_RENUM_DELAY_MS	3000
#define NUM_SIMUL_TRANSFERS	32
#define MAX_EMPTY_TRANSFERS	(NUM_SIMUL_TRANSFERS * 2)

#define NUM_CHANNELS		16
#define NUM_TRIGGER_STAGES	16

/* Matches DSView DSL_REQUIRED_VERSION_MAJOR; minor versions remain compatible. */
#define DSLOGIC_REQUIRED_VERSION_MAJOR	2

/* 6 delay states of up to 256 clock ticks */
#define MAX_SAMPLE_DELAY	(6 * 256)

enum dslogic_edge_modes {
	DS_EDGE_RISING,
	DS_EDGE_FALLING,
};

/*
 * Acquisition state is deliberately private to the driver. Frontends observe
 * only standard libsigrok datafeed packets, never transport implementation
 * details.
 */
enum dslogic_acquisition_state {
	DSLOGIC_ACQ_INIT,
	DSLOGIC_ACQ_START,
	DSLOGIC_ACQ_READY,
	DSLOGIC_ACQ_TRIGGERED,
	DSLOGIC_ACQ_DATA,
	DSLOGIC_ACQ_STOP,
	DSLOGIC_ACQ_FINISH,
	DSLOGIC_ACQ_ABORT,
	DSLOGIC_ACQ_ERROR,
};

enum dslogic_fpga_state {
	DSLOGIC_FPGA_UNKNOWN,
	DSLOGIC_FPGA_UNCONFIGURED,
	DSLOGIC_FPGA_VERIFIED,
	DSLOGIC_FPGA_RECOVERY_REQUIRED,
};

struct dslogic_version {
	uint8_t major;
	uint8_t minor;
};

/* The DSView DSL header starts with this fixed prefix; USB3 pads it to 1 KiB. */
struct dslogic_trigger_header {
	uint32_t check_id;
	uint32_t real_pos;
	uint32_t ram_saddr;
	uint32_t remain_cnt_l;
	uint32_t remain_cnt_h;
	uint32_t status;
	uint8_t first_block[488];
};

/* Driver-private seam for deterministic FPGA upload tests. */
struct dslogic_fpga_upload_ops {
	int (*resource_open)(void *data, struct sr_context *ctx,
			struct sr_resource *resource, const char *name);
	ssize_t (*resource_read)(void *data, struct sr_context *ctx,
			struct sr_resource *resource, void *buffer, size_t length);
	void (*resource_close)(void *data, struct sr_context *ctx,
			struct sr_resource *resource);
	int (*control_write)(void *data, libusb_device_handle *devhdl,
			uint8_t destination, uint16_t offset, const uint8_t *buffer,
			uint8_t length);
	int (*control_read)(void *data, libusb_device_handle *devhdl,
			uint8_t destination, uint16_t offset, uint8_t *buffer,
			uint8_t length);
	int (*wait_hw_status)(void *data, libusb_device_handle *devhdl,
			uint8_t mask, uint8_t *last_status);
	int (*bulk_write)(void *data, libusb_device_handle *devhdl,
			const uint8_t *buffer, size_t length, int *transferred);
};

/* Driver-private boundary for asserting acquisition start ordering. */
struct dslogic_acquisition_start_ops {
	int (*stop)(void *data, const struct sr_dev_inst *sdi);
	int (*upload_fpga)(void *data, const struct sr_dev_inst *sdi);
	int (*apply_threshold)(void *data, const struct sr_dev_inst *sdi,
			double threshold);
	int (*configure_fpga)(void *data, const struct sr_dev_inst *sdi);
	int (*start_transfers)(void *data, const struct sr_dev_inst *sdi);
	int (*add_event_source)(void *data, const struct sr_dev_inst *sdi,
			unsigned int timeout);
	int (*start)(void *data, const struct sr_dev_inst *sdi);
	void (*send_header)(void *data, const struct sr_dev_inst *sdi);
	void (*remove_event_source)(void *data, const struct sr_dev_inst *sdi);
	void (*cancel_and_drain)(void *data, const struct sr_dev_inst *sdi);
	int (*recover)(void *data, const struct sr_dev_inst *sdi);
};

struct dslogic_acquisition_stop_ops {
	int (*recover)(void *data, const struct sr_dev_inst *sdi);
	void (*cancel_and_drain)(void *data, const struct sr_dev_inst *sdi);
};

struct dev_context {
	const struct dslogic_profile *profile;
	/*
	 * Since we can't keep track of a DSLogic device after upgrading
	 * the firmware (it renumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	int64_t fw_updated;

	uint64_t cur_samplerate;
	uint64_t limit_samples;
	/* Hardware-aligned/effective limit, adjusted by the trigger header. */
	uint64_t actual_limit_samples;
	uint64_t capture_ratio;

	gboolean acq_aborted;

	uint64_t sent_samples;
	int submitted_transfers;
	int empty_transfer_count;
	unsigned int empty_poll_count;
	gboolean transfer_completed;
	gboolean overflow_seen;

	unsigned int num_transfers;
	struct libusb_transfer **transfers;
	struct sr_context *ctx;

	uint8_t *packed_buffer;
	size_t packed_buffer_size;
	unsigned int logic_unitsize;

	const struct dslogic_channel_mode *selected_mode;
	uint32_t trigger_pos;
	gboolean external_clock;
	gboolean rle_mode;
	gboolean filter_mode;
	enum dslogic_operation_mode operation_mode;
	int clock_edge;
	double cur_threshold;
	enum dslogic_fpga_state fpga_state;
	uint8_t last_hw_status;
	gboolean fpga_needs_upload;
	gboolean threshold_needs_apply;
	enum dslogic_acquisition_state acquisition_state;
	uint8_t cross_remainder[sizeof(uint64_t) * 32];
	size_t cross_remainder_length;
	gboolean datafeed_end_sent;
	gboolean event_source_registered;
};

SR_PRIV const char *dslogic_profile_fpga_firmware(
		const struct dslogic_profile *profile, double threshold);
SR_PRIV int dslogic_cross_to_packed(const uint8_t *cross, size_t length,
		uint32_t channel_mask, uint8_t *packed, size_t packed_size,
		size_t *sample_count);
/*
 * Append a USB fragment and return newly complete channel-round-robin atoms.
 * The caller owns the returned buffer and must free it with g_free().
 */
SR_PRIV int dslogic_cross_remainder_append(uint8_t *remainder,
		size_t *remainder_length, size_t remainder_capacity,
		const uint8_t *input, size_t input_length, uint32_t channel_mask,
		uint8_t **complete, size_t *complete_length);
/* Return the part of an incoming logic chunk permitted by the sample limit. */
SR_PRIV size_t dslogic_sample_count_until_limit(uint64_t limit_samples,
		uint64_t sent_samples, size_t available_samples);
/* Mark the standard datafeed end packet as consumed exactly once. */
SR_PRIV gboolean dslogic_datafeed_end_once(struct dev_context *devc);
SR_PRIV gboolean dslogic_hw_status_has_overflow(uint8_t status);
/* DSView only accepts buffered trigger headers within the requested capture. */
SR_PRIV gboolean dslogic_trigger_header_starts_data(
		const struct dev_context *devc, uint64_t remain_count);
SR_PRIV size_t dslogic_fpga_config_size(gboolean extension);
SR_PRIV uint16_t dslogic_transform_trigger_word(uint16_t word,
		gboolean adf4360, gboolean half_samplerate,
		gboolean quarter_samplerate);
SR_PRIV int dslogic_build_fpga_config(const struct sr_dev_inst *sdi,
		uint8_t *base, size_t base_size, uint8_t *extension,
		size_t extension_size);
SR_PRIV int dslogic_select_and_upload_fpga_with_ops(
		const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data);
SR_PRIV int dslogic_prepare_fpga_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data);
SR_PRIV int dslogic_prepare_fpga(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_read_hw_status(const struct sr_dev_inst *sdi,
		uint8_t *status);
SR_PRIV int dslogic_verify_loaded_fpga(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_recover_device_state(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_recover_device_state_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data);
SR_PRIV int dslogic_fpga_configure_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_fpga_upload_ops *ops, void *ops_data);
SR_PRIV int dslogic_upload_fx2_if_needed(struct sr_context *ctx,
		libusb_device *device, const struct dslogic_profile *profile,
		gboolean firmware_is_loaded, int64_t *firmware_updated);
SR_PRIV int dslogic_set_voltage_threshold(const struct sr_dev_inst *sdi, double threshold);
SR_PRIV int dslogic_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di);
SR_PRIV int dslogic_open_and_claim(struct sr_dev_inst *sdi, struct sr_dev_driver *di);
SR_PRIV int dslogic_close_and_release(struct sr_dev_inst *sdi);
SR_PRIV struct dev_context *dslogic_dev_new(void);
SR_PRIV int dslogic_acquisition_start(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_acquisition_start_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_acquisition_start_ops *ops, void *ops_data);
SR_PRIV int dslogic_acquisition_stop(struct sr_dev_inst *sdi);
SR_PRIV int dslogic_acquisition_stop_with_ops(const struct sr_dev_inst *sdi,
		const struct dslogic_acquisition_stop_ops *ops, void *ops_data);

#endif

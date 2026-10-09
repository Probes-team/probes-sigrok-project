/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Probes contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <config.h>

#include <check.h>

#include "hardware/dreamsourcelab-dslogic/protocol.h"
#include "hardware/dreamsourcelab-dslogic/profiles.h"
#include "lib.h"

struct test_profile_identity {
	uint16_t pid;
	enum libusb_speed speed;
};

struct fake_fpga_upload {
	const uint8_t *image;
	size_t image_length;
	size_t image_offset;
	int open_result;
	gboolean read_failure;
	int bulk_result;
	int bulk_transferred;
	unsigned int control_write_fail_at;
	unsigned int control_read_fail_at;
	int wait_results[4];
	uint8_t wait_statuses[4];
	unsigned int wait_result_count;
	unsigned int open_count;
	unsigned int read_count;
	unsigned int close_count;
	unsigned int control_count;
	unsigned int control_read_count;
	unsigned int status_count;
	uint8_t control_destinations[96];
	uint16_t control_offsets[96];
	uint8_t control_values[96];
	uint8_t control_read_destinations[96];
	uint16_t control_read_offsets[96];
	uint8_t control_read_values[96];
	uint8_t status_masks[4];
	uint8_t observed_statuses[4];
	uint8_t hw_status;
	uint8_t hdl_version;
	uint8_t security_control;
	char resource_name[64];
};

static int fake_fpga_resource_open(void *data, struct sr_context *ctx,
		struct sr_resource *resource, const char *name)
{
	struct fake_fpga_upload *fake = data;

	(void)ctx;
	fake->open_count++;
	g_strlcpy(fake->resource_name, name, sizeof(fake->resource_name));
	if (fake->open_result != SR_OK)
		return fake->open_result;
	resource->size = fake->image_length;
	resource->handle = fake;
	resource->type = SR_RESOURCE_FIRMWARE;
	return SR_OK;
}

static ssize_t fake_fpga_resource_read(void *data, struct sr_context *ctx,
		struct sr_resource *resource, void *buffer, size_t length)
{
	struct fake_fpga_upload *fake = data;
	size_t available, count;

	(void)ctx;
	ck_assert_ptr_eq(resource->handle, fake);
	fake->read_count++;
	if (fake->read_failure)
		return SR_ERR;
	available = fake->image_length - fake->image_offset;
	count = MIN(available, length);
	memcpy(buffer, fake->image + fake->image_offset, count);
	fake->image_offset += count;
	return count;
}

static void fake_fpga_resource_close(void *data, struct sr_context *ctx,
		struct sr_resource *resource)
{
	struct fake_fpga_upload *fake = data;

	(void)ctx;
	fake->close_count++;
	resource->handle = NULL;
}

static int fake_fpga_control_write(void *data, libusb_device_handle *devhdl,
		uint8_t destination, uint16_t offset, const uint8_t *buffer,
		uint8_t length)
{
	struct fake_fpga_upload *fake = data;

	(void)devhdl;
	ck_assert_uint_lt(fake->control_count,
		G_N_ELEMENTS(fake->control_destinations));
	fake->control_destinations[fake->control_count++] = destination;
	fake->control_offsets[fake->control_count - 1] = offset;
	fake->control_values[fake->control_count - 1] = length ? buffer[0] : 0;
	if (fake->control_write_fail_at == fake->control_count)
		return SR_ERR_IO;
	return SR_OK;
}

static int fake_fpga_control_read(void *data, libusb_device_handle *devhdl,
		uint8_t destination, uint16_t offset, uint8_t *buffer, uint8_t length)
{
	struct fake_fpga_upload *fake = data;

	(void)devhdl;
	ck_assert_uint_lt(fake->control_read_count,
		G_N_ELEMENTS(fake->control_read_destinations));
	fake->control_read_destinations[fake->control_read_count] = destination;
	fake->control_read_offsets[fake->control_read_count++] = offset;
	if (fake->control_read_fail_at == fake->control_read_count)
		return SR_ERR_IO;
	memset(buffer, 0, length);
	if (destination == 2 && length == 1)
		buffer[0] = fake->hw_status;
	else if (destination == 15 && offset == 0 && length >= 5)
		buffer[4] = fake->hdl_version;
	else if (destination == 15 && offset == 0x73 && length == 1)
		buffer[0] = fake->security_control;
	fake->control_read_values[fake->control_read_count - 1] = length ? buffer[0] : 0;
	return SR_OK;
}

static int fake_fpga_wait_hw_status(void *data, libusb_device_handle *devhdl,
		uint8_t mask, uint8_t *last_status)
{
	struct fake_fpga_upload *fake = data;
	unsigned int index;
	uint8_t status;
	int result;

	(void)devhdl;
	ck_assert_uint_lt(fake->status_count, G_N_ELEMENTS(fake->status_masks));
	index = fake->status_count;
	fake->status_masks[index] = mask;
	status = index < fake->wait_result_count ? fake->wait_statuses[index] : mask;
	result = index < fake->wait_result_count ? fake->wait_results[index] : SR_OK;
	fake->observed_statuses[index] = status;
	fake->status_count++;
	if (last_status)
		*last_status = status;
	return result;
}

static int fake_fpga_bulk_write(void *data, libusb_device_handle *devhdl,
		const uint8_t *buffer, size_t length, int *transferred)
{
	struct fake_fpga_upload *fake = data;

	(void)devhdl;
	ck_assert_mem_eq(buffer, fake->image, length);
	*transferred = fake->bulk_transferred >= 0 ? fake->bulk_transferred :
		(int)length;
	return fake->bulk_result;
}

static const struct dslogic_fpga_upload_ops fake_fpga_upload_ops = {
	.resource_open = fake_fpga_resource_open,
	.resource_read = fake_fpga_resource_read,
	.resource_close = fake_fpga_resource_close,
	.control_write = fake_fpga_control_write,
	.control_read = fake_fpga_control_read,
	.wait_hw_status = fake_fpga_wait_hw_status,
	.bulk_write = fake_fpga_bulk_write,
};

static void assert_fake_recovery_suffix(const struct fake_fpga_upload *fake)
{
	static const uint8_t expected_destinations[] = { 14, 9, 6, 7 };
	unsigned int index;

	ck_assert_uint_ge(fake->control_count,
		G_N_ELEMENTS(expected_destinations));
	for (index = 0; index < G_N_ELEMENTS(expected_destinations); index++)
		ck_assert_uint_eq(fake->control_destinations[
			fake->control_count - G_N_ELEMENTS(expected_destinations) + index],
			expected_destinations[index]);
}

enum fake_start_event {
	FAKE_START_STOP,
	FAKE_START_UPLOAD,
	FAKE_START_THRESHOLD,
	FAKE_START_CONFIGURE,
	FAKE_START_TRANSFERS,
	FAKE_START_EVENT_SOURCE,
	FAKE_START_START,
	FAKE_START_HEADER,
	FAKE_START_REMOVE_EVENT_SOURCE,
	FAKE_START_CANCEL_AND_DRAIN,
	FAKE_START_RECOVER,
};

struct fake_acquisition_start {
	enum fake_start_event events[16];
	unsigned int event_count;
	uint64_t limit_at_configure;
	const struct dslogic_channel_mode *mode_at_configure;
	unsigned int event_timeout;
	int transfers_result;
	int event_source_result;
	int start_result;
	int recover_result;
	gboolean aborted_at_recover;
};

static void fake_start_record(struct fake_acquisition_start *fake,
		enum fake_start_event event)
{
	ck_assert_uint_lt(fake->event_count, G_N_ELEMENTS(fake->events));
	fake->events[fake->event_count++] = event;
}

static int fake_start_stop(void *data, const struct sr_dev_inst *sdi)
{
	(void)sdi;
	fake_start_record(data, FAKE_START_STOP);
	return SR_OK;
}

static int fake_start_upload(void *data, const struct sr_dev_inst *sdi)
{
	(void)sdi;
	fake_start_record(data, FAKE_START_UPLOAD);
	return SR_OK;
}

static int fake_start_threshold(void *data, const struct sr_dev_inst *sdi,
		double threshold)
{
	(void)sdi;
	ck_assert_double_eq(threshold, 0.7);
	fake_start_record(data, FAKE_START_THRESHOLD);
	return SR_OK;
}

static int fake_start_configure(void *data, const struct sr_dev_inst *sdi)
{
	struct fake_acquisition_start *fake = data;
	const struct dev_context *devc = sdi->priv;

	fake_start_record(fake, FAKE_START_CONFIGURE);
	fake->limit_at_configure = devc->actual_limit_samples;
	fake->mode_at_configure = devc->selected_mode;
	return SR_OK;
}

static int fake_start_transfers(void *data, const struct sr_dev_inst *sdi)
{
	struct fake_acquisition_start *fake = data;

	(void)sdi;
	fake_start_record(fake, FAKE_START_TRANSFERS);
	return fake->transfers_result;
}

static int fake_start_event_source(void *data, const struct sr_dev_inst *sdi,
		unsigned int timeout)
{
	struct fake_acquisition_start *fake = data;

	(void)sdi;
	fake_start_record(fake, FAKE_START_EVENT_SOURCE);
	fake->event_timeout = timeout;
	return fake->event_source_result;
}

static int fake_start_start(void *data, const struct sr_dev_inst *sdi)
{
	struct fake_acquisition_start *fake = data;

	(void)sdi;
	fake_start_record(fake, FAKE_START_START);
	return fake->start_result;
}

static void fake_start_header(void *data, const struct sr_dev_inst *sdi)
{
	(void)sdi;
	fake_start_record(data, FAKE_START_HEADER);
}

static void fake_start_remove_event_source(void *data,
		const struct sr_dev_inst *sdi)
{
	(void)sdi;
	fake_start_record(data, FAKE_START_REMOVE_EVENT_SOURCE);
}

static void fake_start_cancel_and_drain(void *data,
		const struct sr_dev_inst *sdi)
{
	(void)sdi;
	fake_start_record(data, FAKE_START_CANCEL_AND_DRAIN);
}

static int fake_start_recover(void *data, const struct sr_dev_inst *sdi)
{
	struct fake_acquisition_start *fake = data;

	fake_start_record(fake, FAKE_START_RECOVER);
	fake->aborted_at_recover = ((struct dev_context *)sdi->priv)->acq_aborted;
	if (fake->recover_result != SR_OK)
		return fake->recover_result;
	((struct dev_context *)sdi->priv)->fpga_state = DSLOGIC_FPGA_UNKNOWN;
	return SR_OK;
}

static const struct dslogic_acquisition_start_ops fake_acquisition_start_ops = {
	.stop = fake_start_stop,
	.upload_fpga = fake_start_upload,
	.apply_threshold = fake_start_threshold,
	.configure_fpga = fake_start_configure,
	.start_transfers = fake_start_transfers,
	.add_event_source = fake_start_event_source,
	.start = fake_start_start,
	.send_header = fake_start_header,
	.remove_event_source = fake_start_remove_event_source,
	.cancel_and_drain = fake_start_cancel_and_drain,
	.recover = fake_start_recover,
};

static struct sr_dev_inst *new_fake_fpga_sdi(
		const struct dslogic_profile *profile, struct dev_context **devc_out)
{
	struct sr_dev_driver *driver;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;

	driver = srtest_driver_get("dreamsourcelab-dslogic");
	srtest_driver_init(srtest_ctx, driver);
	sdi = sr_dev_inst_user_new("DreamSourceLab", profile->model, NULL);
	devc = dslogic_dev_new();
	usb = sr_usb_dev_inst_new(0, 0,
		(libusb_device_handle *)g_malloc0(1));
	sdi->driver = driver;
	sdi->conn = usb;
	sdi->priv = devc;
	devc->profile = profile;
	devc->operation_mode = profile->default_operation_mode;
	*devc_out = devc;
	return sdi;
}

static void free_fake_fpga_sdi(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;

	g_free(sdi->priv);
	sdi->priv = NULL;
	g_free(usb->devhdl);
	sr_usb_dev_inst_free(usb);
	sdi->conn = NULL;
	sr_dev_inst_free(sdi);
}

static uint16_t read_le16(const uint8_t *data)
{
	uint16_t value;

	memcpy(&value, data, sizeof(value));
	return GUINT16_FROM_LE(value);
}

static uint32_t read_le32(const uint8_t *data)
{
	uint32_t value;

	memcpy(&value, data, sizeof(value));
	return GUINT32_FROM_LE(value);
}

static uint64_t read_le64(const uint8_t *data)
{
	uint64_t value;

	memcpy(&value, data, sizeof(value));
	return GUINT64_FROM_LE(value);
}

static uint8_t *read_fixture(const char *name, gsize *length)
{
	char *path;
	char *contents;
	GError *error = NULL;

	path = g_build_filename(DSLOGIC_TEST_FIXTURE_DIR, name, NULL);
	ck_assert_msg(g_file_get_contents(path, &contents, length, &error),
		"Unable to read fixture %s: %s", path,
		error ? error->message : "unknown error");
	g_clear_error(&error);
	g_free(path);
	return (uint8_t *)contents;
}

static void assert_cross_matches_packed(const uint8_t *cross, size_t length,
		uint32_t channel_mask, const uint8_t *packed, size_t sample_count,
		unsigned int unitsize)
{
	unsigned int channel_count = 0;
	unsigned int channel, word_index = 0;
	size_t block, bit;

	for (channel = 0; channel < 32; channel++)
		if (channel_mask & (UINT32_C(1) << channel))
			channel_count++;
	ck_assert_uint_eq(sample_count,
		(length / (channel_count * sizeof(uint64_t))) * 64);
	for (block = 0; block < sample_count / 64; block++) {
		word_index = 0;
		for (channel = 0; channel < 32; channel++) {
			uint64_t word;

			if (!(channel_mask & (UINT32_C(1) << channel)))
				continue;
			word = read_le64(cross +
				(block * channel_count + word_index) * sizeof(uint64_t));
			for (bit = 0; bit < 64; bit++)
				ck_assert_uint_eq((packed[(block * 64 + bit) * unitsize +
					channel / 8] >> (channel % 8)) & 1,
					(word >> bit) & 1);
			word_index++;
		}
	}
}

static const struct test_profile_identity test_profiles[] = {
	{ 0x0001, LIBUSB_SPEED_HIGH }, { 0x0003, LIBUSB_SPEED_HIGH },
	{ 0x0020, LIBUSB_SPEED_HIGH }, { 0x0021, LIBUSB_SPEED_HIGH },
	{ 0x0029, LIBUSB_SPEED_HIGH }, { 0x002a, LIBUSB_SPEED_HIGH },
	{ 0x002a, LIBUSB_SPEED_SUPER }, { 0x002c, LIBUSB_SPEED_HIGH },
	{ 0x002c, LIBUSB_SPEED_SUPER }, { 0x002d, LIBUSB_SPEED_HIGH },
	{ 0x0030, LIBUSB_SPEED_HIGH }, { 0x0031, LIBUSB_SPEED_HIGH },
	{ 0x0034, LIBUSB_SPEED_HIGH }, { 0x0035, LIBUSB_SPEED_HIGH },
};

START_TEST(test_profile_identity_is_unique)
{
	size_t index;

	for (index = 0; index < G_N_ELEMENTS(test_profiles); index++) {
		const struct dslogic_profile *profile = dslogic_profile_find(
				0x2a0e, test_profiles[index].pid, test_profiles[index].speed);
		ck_assert_ptr_nonnull(profile);
		ck_assert_uint_gt(profile->logic_channel_count, 0);
		ck_assert_uint_lt(profile->default_mode, profile->mode_count);
	}
}
END_TEST

START_TEST(test_fpga_recovery_uses_force_ready_stop_and_transport_reset)
{
	static const uint8_t expected_destinations[] = { 14, 9, 6, 7 };
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_fpga_upload fake = { 0 };

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(profile);
	sdi = new_fake_fpga_sdi(profile, &devc);
	fake.hw_status = 0x40;
	ck_assert_int_eq(dslogic_recover_device_state_with_ops(sdi,
		&fake_fpga_upload_ops, &fake), SR_OK);
	ck_assert_mem_eq(fake.control_destinations, expected_destinations,
		sizeof(expected_destinations));
	ck_assert_uint_eq(fake.control_offsets[0], 0x70);
	ck_assert_uint_eq(fake.control_values[0], 0x02);
	ck_assert_uint_eq(fake.control_values[2], 0x80);
	ck_assert_uint_eq(fake.control_values[3], 0x01);
	ck_assert_uint_eq(fake.control_read_count, 1);
	ck_assert_uint_eq(fake.control_read_destinations[0], 2);
	ck_assert_uint_eq(devc->last_hw_status, 0x40);
	ck_assert_int_eq(devc->fpga_state, DSLOGIC_FPGA_UNKNOWN);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_fpga_done_prepare_verifies_without_uploading)
{
	static const uint8_t image[] = { 0x11, 0x22, 0x33, 0x44 };
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_fpga_upload fake = {
		.image = image,
		.image_length = sizeof(image),
		.bulk_transferred = -1,
		.hw_status = 0x40,
		.hdl_version = 0x0e,
	};

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(profile);
	sdi = new_fake_fpga_sdi(profile, &devc);
	devc->cur_threshold = 0.7;
	ck_assert_int_eq(dslogic_prepare_fpga_with_ops(sdi,
		&fake_fpga_upload_ops, &fake), SR_OK);
	ck_assert_uint_eq(fake.open_count, 0);
	ck_assert_uint_eq(fake.control_read_count, 2);
	ck_assert_uint_eq(fake.control_read_destinations[0], 2);
	ck_assert_uint_eq(fake.control_read_destinations[1], 15);
	ck_assert_uint_eq(fake.control_count, 1);
	ck_assert_uint_eq(fake.control_destinations[0], 14);
	ck_assert_uint_ne(fake.control_destinations[0], 3);
	ck_assert_int_eq(devc->fpga_state, DSLOGIC_FPGA_VERIFIED);
	ck_assert(!devc->fpga_needs_upload);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_standard_config_matrix)
{
	struct sr_dev_driver *driver;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	GVariant *value;
	unsigned int index;
	int ret;

	driver = srtest_driver_get("dreamsourcelab-dslogic");
	srtest_driver_init(srtest_ctx, driver);
	sdi = sr_dev_inst_user_new("DreamSourceLab", "DSLogic U3Pro32", NULL);
	ck_assert_ptr_nonnull(sdi);
	devc = dslogic_dev_new();
	ck_assert_ptr_nonnull(devc);
	devc->profile = dslogic_profile_find(0x2a0e, 0x002c, LIBUSB_SPEED_SUPER);
	ck_assert_ptr_nonnull(devc->profile);
	devc->cur_samplerate = SR_MHZ(50);
	sdi->driver = driver;
	sdi->priv = devc;
	for (index = 0; index < 32; index++)
		ck_assert_int_eq(sr_dev_inst_channel_add(sdi, index,
			SR_CHANNEL_LOGIC, "D"), SR_OK);

	value = g_variant_new_uint64(SR_MHZ(250));
	ret = driver->config_set(SR_CONF_SAMPLERATE, value, sdi, NULL);
	ck_assert_int_eq(ret, SR_OK);
	ret = driver->config_get(SR_CONF_SAMPLERATE, &value, sdi, NULL);
	ck_assert_int_eq(ret, SR_OK);
	ck_assert_uint_eq(g_variant_get_uint64(value), SR_MHZ(250));

	value = g_variant_new_string("stream");
	ck_assert_int_eq(driver->config_set(SR_CONF_DEVICE_MODE, value, sdi, NULL), SR_OK);
	value = g_variant_new_boolean(TRUE);
	ck_assert_int_eq(driver->config_set(SR_CONF_CONTINUOUS, value, sdi, NULL), SR_OK);
	ret = driver->config_get(SR_CONF_CONTINUOUS, &value, sdi, NULL);
	ck_assert_int_eq(ret, SR_OK);
	ck_assert(g_variant_get_boolean(value));
	value = g_variant_new_boolean(TRUE);
	ck_assert_int_eq(driver->config_set(SR_CONF_RLE, value, sdi, NULL), SR_OK);
	ck_assert_int_eq(driver->config_set(SR_CONF_FILTER, value, sdi, NULL), SR_OK);
	value = g_variant_new_string("internal-test");
	ck_assert_int_eq(driver->config_set(SR_CONF_DEVICE_MODE, value, sdi, NULL), SR_OK);
	ck_assert(devc->rle_mode);
	ck_assert(devc->filter_mode);
	ck_assert_int_eq(devc->operation_mode, DSLOGIC_OPERATION_INTERNAL_TEST);
	ret = driver->config_get(SR_CONF_CONTINUOUS, &value, sdi, NULL);
	ck_assert_int_eq(ret, SR_OK);
	ck_assert(!g_variant_get_boolean(value));
	ck_assert_uint_eq(devc->cur_samplerate, SR_MHZ(100));
	ck_assert_uint_eq(devc->limit_samples,
		UINT64_C(2) * 1024 * 1024 * 1024 / 32);

	ret = driver->config_list(SR_CONF_FILTER, &value, sdi, NULL);
	ck_assert_int_eq(ret, SR_OK);
	ck_assert(g_variant_get_boolean(value));
	ret = driver->config_list(SR_CONF_DEVICE_MODE, &value, sdi, NULL);
	ck_assert_int_eq(ret, SR_OK);
	{
		GVariant *mode = g_variant_get_child_value(value, 2);
		ck_assert_str_eq(g_variant_get_string(mode, NULL), "internal-test");
		g_variant_unref(mode);
	}

	value = g_variant_new_uint64(SR_MHZ(400));
	ck_assert_int_eq(driver->config_set(SR_CONF_SAMPLERATE, value, sdi, NULL), SR_ERR_NA);
	value = g_variant_new_string("buffer");
	ck_assert_int_eq(driver->config_set(SR_CONF_DEVICE_MODE, value, sdi, NULL), SR_OK);
	ret = driver->config_get(SR_CONF_CONTINUOUS, &value, sdi, NULL);
	ck_assert_int_eq(ret, SR_OK);
	ck_assert(!g_variant_get_boolean(value));
	value = g_variant_new_boolean(TRUE);
	ck_assert_int_eq(driver->config_set(SR_CONF_CONTINUOUS, value, sdi, NULL), SR_OK);
	ck_assert_int_eq(devc->operation_mode, DSLOGIC_OPERATION_STREAM);
	value = g_variant_new_boolean(FALSE);
	ck_assert_int_eq(driver->config_set(SR_CONF_CONTINUOUS, value, sdi, NULL), SR_OK);
	ck_assert_int_eq(devc->operation_mode, DSLOGIC_OPERATION_BUFFER);
	value = g_variant_new_uint64(SR_MHZ(400));
	ck_assert_int_eq(driver->config_set(SR_CONF_SAMPLERATE, value, sdi, NULL), SR_ERR_ARG);
	value = g_variant_new_uint64(101);
	ck_assert_int_eq(driver->config_set(SR_CONF_CAPTURE_RATIO, value, sdi, NULL), SR_ERR_ARG);
	value = g_variant_new_uint64(100);
	ck_assert_int_eq(driver->config_set(SR_CONF_CAPTURE_RATIO, value, sdi, NULL), SR_OK);
	value = std_gvar_tuple_double(0.7, 1.4);
	ck_assert_int_eq(driver->config_set(SR_CONF_VOLTAGE_THRESHOLD, value, sdi, NULL), SR_OK);
	ck_assert(devc->fpga_needs_upload);
	ck_assert(devc->threshold_needs_apply);

	g_free(devc);
	sdi->priv = NULL;
}
END_TEST

START_TEST(test_fpga_firmware_selection_comes_from_profile)
{
	const struct dslogic_profile *legacy;
	const struct dslogic_profile *u3;

	legacy = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	u3 = dslogic_profile_find(0x2a0e, 0x002c, LIBUSB_SPEED_SUPER);
	ck_assert_ptr_nonnull(legacy);
	ck_assert_ptr_nonnull(u3);
	ck_assert_str_eq(dslogic_profile_fpga_firmware(legacy, 0.7), "dreamsourcelab-dslogic/DSLogic33.bin");
	ck_assert_str_eq(dslogic_profile_fpga_firmware(legacy, 1.4), "dreamsourcelab-dslogic/DSLogic50.bin");
	ck_assert_str_eq(dslogic_profile_fpga_firmware(u3, 0.7), "dreamsourcelab-dslogic/DSLogicU3Pro32.bin");
	ck_assert_ptr_null(dslogic_profile_fpga_firmware(NULL, 1.4));
}
END_TEST

START_TEST(test_fx2_cold_boot_assets_are_explicit)
{
	const struct dslogic_profile *legacy;
	const struct dslogic_profile *plus;
	const struct dslogic_profile *u2basic;
	const struct dslogic_profile *u3;

	legacy = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	plus = dslogic_profile_find(0x2a0e, 0x0020, LIBUSB_SPEED_HIGH);
	u2basic = dslogic_profile_find(0x2a0e, 0x0029, LIBUSB_SPEED_HIGH);
	u3 = dslogic_profile_find(0x2a0e, 0x002c, LIBUSB_SPEED_SUPER);
	ck_assert_ptr_nonnull(legacy);
	ck_assert_ptr_nonnull(plus);
	ck_assert_ptr_nonnull(u2basic);
	ck_assert_ptr_nonnull(u3);
	ck_assert_str_eq(dslogic_profile_fx2_firmware(legacy),
		"dreamsourcelab-dslogic/DSLogic.fw");
	ck_assert_str_eq(dslogic_profile_fx2_firmware(plus),
		"dreamsourcelab-dslogic/DSLogicPlus.fw");
	ck_assert_ptr_null(dslogic_profile_fx2_firmware(u2basic));
	ck_assert_ptr_null(dslogic_profile_fx2_firmware(u3));
	ck_assert_ptr_null(dslogic_profile_fx2_firmware(NULL));
}
END_TEST

START_TEST(test_fpga_upload_uses_fake_resource_and_usb_ops)
{
	static const uint8_t image[] = { 0x11, 0x22, 0x33, 0x44 };
	static const uint8_t expected_destinations[] = {
		3, 5, 3, 6, 10, 6, 6, 5, 7,
	};
	static const uint8_t expected_status_masks[] = { 0x20, 0x80, 0x40 };
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_fpga_upload fake = {
		.image = image,
		.image_length = sizeof(image),
		.bulk_transferred = -1,
		.hdl_version = 0x0e,
	};

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(profile);
	sdi = new_fake_fpga_sdi(profile, &devc);
	devc->cur_threshold = 0.7;
	ck_assert_int_eq(dslogic_select_and_upload_fpga_with_ops(sdi,
		&fake_fpga_upload_ops, &fake), SR_OK);
	ck_assert_str_eq(fake.resource_name, "dreamsourcelab-dslogic/DSLogic33.bin");
	ck_assert_uint_eq(fake.open_count, 1);
	ck_assert_uint_eq(fake.read_count, 1);
	ck_assert_uint_eq(fake.close_count, 1);
	ck_assert_uint_eq(fake.image_offset, sizeof(image));
	ck_assert_mem_eq(fake.control_destinations, expected_destinations,
		sizeof(expected_destinations));
	ck_assert_mem_eq(fake.status_masks, expected_status_masks,
		sizeof(expected_status_masks));
	ck_assert(!devc->fpga_needs_upload);
	ck_assert_int_eq(devc->fpga_state, DSLOGIC_FPGA_VERIFIED);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_fpga_upload_failure_paths_close_resource)
{
	static const uint8_t image[] = { 0x11, 0x22, 0x33, 0x44 };
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_fpga_upload fake = {
		.image = image,
		.image_length = sizeof(image),
		.bulk_transferred = 2,
	};

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	sdi = new_fake_fpga_sdi(profile, &devc);
	devc->cur_threshold = 1.4;
	ck_assert_int_eq(dslogic_select_and_upload_fpga_with_ops(sdi,
		&fake_fpga_upload_ops, &fake), SR_ERR);
	ck_assert_str_eq(fake.resource_name, "dreamsourcelab-dslogic/DSLogic50.bin");
	ck_assert_uint_eq(fake.close_count, 1);
	ck_assert(devc->fpga_needs_upload);
	free_fake_fpga_sdi(sdi);

	fake = (struct fake_fpga_upload) {
		.image = image,
		.image_length = sizeof(image),
		.read_failure = TRUE,
		.bulk_transferred = -1,
	};
	sdi = new_fake_fpga_sdi(profile, &devc);
	ck_assert_int_eq(dslogic_select_and_upload_fpga_with_ops(sdi,
		&fake_fpga_upload_ops, &fake), SR_ERR_DATA);
	ck_assert_uint_eq(fake.control_count, 0);
	ck_assert_uint_eq(fake.close_count, 1);
	free_fake_fpga_sdi(sdi);

	fake = (struct fake_fpga_upload) {
		.image = image,
		.image_length = sizeof(image),
		.bulk_transferred = -1,
		.hdl_version = 0x0e,
	};
	sdi = new_fake_fpga_sdi(profile, &devc);
	devc->profile = NULL;
	ck_assert_int_eq(dslogic_select_and_upload_fpga_with_ops(sdi,
		&fake_fpga_upload_ops, &fake), SR_ERR);
	ck_assert_uint_eq(fake.open_count, 0);
	ck_assert_uint_eq(fake.close_count, 0);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_fpga_done_prepare_rejects_incompatible_hdl_without_uploading)
{
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_fpga_upload fake = {
		.hw_status = 0x40,
		.hdl_version = 0x0d,
	};

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	sdi = new_fake_fpga_sdi(profile, &devc);
	ck_assert_int_eq(dslogic_prepare_fpga_with_ops(sdi,
		&fake_fpga_upload_ops, &fake), SR_ERR);
	ck_assert_uint_eq(fake.open_count, 0);
	ck_assert_uint_eq(fake.control_count, 1);
	ck_assert_uint_eq(fake.control_destinations[0], 14);
	ck_assert_uint_ne(fake.control_destinations[0], 3);
	ck_assert_int_ne(devc->fpga_state, DSLOGIC_FPGA_VERIFIED);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_fpga_done_prepare_rejects_hdl_read_failure_without_uploading)
{
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_fpga_upload fake = {
		.hw_status = 0x40,
		.control_read_fail_at = 2,
	};
	unsigned int index;

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	sdi = new_fake_fpga_sdi(profile, &devc);
	ck_assert_int_eq(dslogic_prepare_fpga_with_ops(sdi, &fake_fpga_upload_ops,
		&fake), SR_ERR_IO);
	ck_assert_uint_eq(fake.open_count, 0);
	for (index = 0; index < fake.control_count; index++)
		ck_assert_uint_ne(fake.control_destinations[index], 3);
	ck_assert_int_ne(devc->fpga_state, DSLOGIC_FPGA_VERIFIED);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_fpga_done_prepare_runs_security_check_without_uploading)
{
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_fpga_upload fake = {
		.hw_status = 0x40,
		.hdl_version = 0x0e,
		.security_control = 0x08,
	};
	unsigned int index;

	profile = dslogic_profile_find(0x2a0e, 0x002d, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(profile);
	sdi = new_fake_fpga_sdi(profile, &devc);
	ck_assert_int_eq(dslogic_prepare_fpga_with_ops(sdi,
		&fake_fpga_upload_ops, &fake), SR_OK);
	ck_assert_uint_eq(fake.open_count, 0);
	ck_assert_int_eq(devc->fpga_state, DSLOGIC_FPGA_VERIFIED);
	for (index = 0; index < fake.control_count; index++)
		ck_assert_uint_ne(fake.control_destinations[index], 3);
	ck_assert_uint_gt(fake.control_read_count, 2);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_fpga_done_prepare_skips_security_for_nonsecure_profile)
{
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_fpga_upload fake = {
		.hw_status = 0x40,
		.hdl_version = 0x0e,
	};
	unsigned int index;

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	sdi = new_fake_fpga_sdi(profile, &devc);
	ck_assert_int_eq(dslogic_prepare_fpga_with_ops(sdi, &fake_fpga_upload_ops,
		&fake), SR_OK);
	for (index = 0; index < fake.control_read_count; index++)
		ck_assert_uint_ne(fake.control_read_destinations[index], 12);
	ck_assert_uint_eq(fake.open_count, 0);
	ck_assert_int_eq(devc->fpga_state, DSLOGIC_FPGA_VERIFIED);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_fpga_not_done_prepare_uploads_once)
{
	static const uint8_t image[] = { 0x11, 0x22, 0x33, 0x44 };
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_fpga_upload fake = {
		.image = image,
		.image_length = sizeof(image),
		.bulk_transferred = -1,
		.hdl_version = 0x0e,
	};

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	sdi = new_fake_fpga_sdi(profile, &devc);
	devc->cur_threshold = 0.7;
	ck_assert_int_eq(dslogic_prepare_fpga_with_ops(sdi,
		&fake_fpga_upload_ops, &fake), SR_OK);
	ck_assert_uint_eq(fake.open_count, 1);
	ck_assert_int_eq(devc->fpga_state, DSLOGIC_FPGA_VERIFIED);
	ck_assert(!devc->fpga_needs_upload);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_fpga_upload_every_hardware_failure_recovers)
{
	static const uint8_t image[] = { 0x11, 0x22, 0x33, 0x44 };
	const struct dslogic_profile *profile;
	unsigned int failure_case;

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	for (failure_case = 0; failure_case < 7; failure_case++) {
		struct sr_dev_inst *sdi;
		struct dev_context *devc;
		struct fake_fpga_upload fake = {
			.image = image,
			.image_length = sizeof(image),
			.bulk_transferred = -1,
		};

		sdi = new_fake_fpga_sdi(profile, &devc);
		switch (failure_case) {
		case 0:
			fake.control_write_fail_at = 1; /* PROG_B */
			break;
		case 1:
			fake.wait_result_count = 1; /* INIT_B */
			fake.wait_results[0] = SR_ERR_TIMEOUT;
			break;
		case 2:
			fake.bulk_transferred = 2; /* bitstream bulk write */
			break;
		case 3:
			fake.wait_result_count = 2; /* GPIF_DONE */
			fake.wait_results[1] = SR_ERR_TIMEOUT;
			break;
		case 4:
			fake.wait_result_count = 3; /* FPGA_DONE */
			fake.wait_results[2] = SR_ERR_TIMEOUT;
			break;
		case 5:
			fake.control_write_fail_at = 8; /* LED green */
			break;
		case 6:
			fake.control_write_fail_at = 9; /* WORDWIDE */
			break;
		}
		ck_assert_int_ne(dslogic_select_and_upload_fpga_with_ops(sdi,
			&fake_fpga_upload_ops, &fake), SR_OK);
		ck_assert_uint_eq(fake.close_count, 1);
		assert_fake_recovery_suffix(&fake);
		ck_assert_int_eq(devc->fpga_state, DSLOGIC_FPGA_UNKNOWN);
		free_fake_fpga_sdi(sdi);
	}
}
END_TEST

START_TEST(test_acquisition_start_order_is_hardware_safe)
{
	static const enum fake_start_event expected_events[] = {
		FAKE_START_STOP, FAKE_START_UPLOAD, FAKE_START_THRESHOLD,
		FAKE_START_CONFIGURE, FAKE_START_TRANSFERS,
		FAKE_START_EVENT_SOURCE, FAKE_START_START, FAKE_START_HEADER,
	};
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_acquisition_start fake = { 0 };
	unsigned int index;

	profile = dslogic_profile_find(0x2a0e, 0x002c, LIBUSB_SPEED_SUPER);
	sdi = new_fake_fpga_sdi(profile, &devc);
	devc->cur_samplerate = SR_MHZ(50);
	devc->cur_threshold = 0.7;
	devc->limit_samples = 65;
	devc->capture_ratio = 50;
	for (index = 0; index < 32; index++)
		ck_assert_int_eq(sr_dev_inst_channel_add(sdi, index, SR_CHANNEL_LOGIC, "D"), SR_OK);
	ck_assert_int_eq(dslogic_acquisition_start_with_ops(sdi,
		&fake_acquisition_start_ops, &fake), SR_OK);
	ck_assert_mem_eq(fake.events, expected_events, sizeof(expected_events));
	ck_assert_uint_eq(fake.limit_at_configure, 128);
	ck_assert_ptr_nonnull(fake.mode_at_configure);
	ck_assert_str_eq(fake.mode_at_configure->id, "buffer-250x32");
	ck_assert_uint_gt(fake.event_timeout, 0);
	ck_assert_int_eq(devc->acquisition_state, DSLOGIC_ACQ_TRIGGERED);
	ck_assert(devc->event_source_registered);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_acquisition_start_failures_drain_then_recover)
{
	static const enum fake_start_event expected_transfer_failure[] = {
		FAKE_START_STOP, FAKE_START_UPLOAD, FAKE_START_THRESHOLD,
		FAKE_START_CONFIGURE, FAKE_START_TRANSFERS,
		FAKE_START_CANCEL_AND_DRAIN, FAKE_START_RECOVER,
	};
	static const enum fake_start_event expected_event_source_failure[] = {
		FAKE_START_STOP, FAKE_START_UPLOAD, FAKE_START_THRESHOLD,
		FAKE_START_CONFIGURE, FAKE_START_TRANSFERS,
		FAKE_START_EVENT_SOURCE, FAKE_START_CANCEL_AND_DRAIN,
		FAKE_START_RECOVER,
	};
	static const enum fake_start_event expected_start_failure[] = {
		FAKE_START_STOP, FAKE_START_UPLOAD, FAKE_START_THRESHOLD,
		FAKE_START_CONFIGURE, FAKE_START_TRANSFERS,
		FAKE_START_EVENT_SOURCE, FAKE_START_START,
		FAKE_START_REMOVE_EVENT_SOURCE, FAKE_START_CANCEL_AND_DRAIN,
		FAKE_START_RECOVER,
	};
	const struct dslogic_profile *profile;
	const enum fake_start_event *expected_events;
	size_t expected_length;
	unsigned int failure_case, index;

	profile = dslogic_profile_find(0x2a0e, 0x002c, LIBUSB_SPEED_SUPER);
	for (failure_case = 0; failure_case < 3; failure_case++) {
		struct sr_dev_inst *sdi;
		struct dev_context *devc;
		struct fake_acquisition_start fake = { 0 };

		sdi = new_fake_fpga_sdi(profile, &devc);
		devc->cur_samplerate = SR_MHZ(50);
		devc->cur_threshold = 0.7;
		for (index = 0; index < 32; index++)
			ck_assert_int_eq(sr_dev_inst_channel_add(sdi, index,
				SR_CHANNEL_LOGIC, "D"), SR_OK);
		switch (failure_case) {
		case 0:
			fake.transfers_result = SR_ERR_IO;
			expected_events = expected_transfer_failure;
			expected_length = G_N_ELEMENTS(expected_transfer_failure);
			break;
		case 1:
			fake.event_source_result = SR_ERR_IO;
			expected_events = expected_event_source_failure;
			expected_length = G_N_ELEMENTS(expected_event_source_failure);
			break;
		default:
			fake.start_result = SR_ERR_IO;
			expected_events = expected_start_failure;
			expected_length = G_N_ELEMENTS(expected_start_failure);
			break;
		}
		ck_assert_int_eq(dslogic_acquisition_start_with_ops(sdi,
			&fake_acquisition_start_ops, &fake), SR_ERR_IO);
		ck_assert_mem_eq(fake.events, expected_events,
			expected_length * sizeof(*expected_events));
		ck_assert_int_eq(devc->fpga_state, DSLOGIC_FPGA_UNKNOWN);
		free_fake_fpga_sdi(sdi);
	}
}
END_TEST

START_TEST(test_acquisition_stop_drains_before_recover)
{
	static const enum fake_start_event expected_events[] = {
		FAKE_START_CANCEL_AND_DRAIN, FAKE_START_RECOVER,
	};
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct fake_acquisition_start fake = { 0 };
	struct dslogic_acquisition_stop_ops ops = {
		.recover = fake_start_recover,
		.cancel_and_drain = fake_start_cancel_and_drain,
	};

	profile = dslogic_profile_find(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH);
	sdi = new_fake_fpga_sdi(profile, &devc);
	ck_assert_int_eq(dslogic_acquisition_stop_with_ops(sdi, &ops, &fake), SR_OK);
	ck_assert_mem_eq(fake.events, expected_events, sizeof(expected_events));
	ck_assert_int_eq(devc->acquisition_state, DSLOGIC_ACQ_STOP);
	ck_assert(fake.aborted_at_recover);
	fake = (struct fake_acquisition_start) { .recover_result = SR_ERR_IO };
	ck_assert_int_eq(dslogic_acquisition_stop_with_ops(sdi, &ops, &fake), SR_ERR_IO);
	ck_assert_mem_eq(fake.events, expected_events, sizeof(expected_events));
	ck_assert(fake.aborted_at_recover);
	free_fake_fpga_sdi(sdi);
}
END_TEST

START_TEST(test_cross_conversion_packs_3_16_32_channels)
{
	uint64_t words[32];
	uint8_t cross[sizeof(words)];
	uint8_t packed[64 * 4];
	size_t samples;

	memset(words, 0, sizeof(words));
	words[0] = (UINT64_C(1) << 0) | (UINT64_C(1) << 63);
	words[1] = UINT64_C(1) << 1;
	words[2] = UINT64_C(1) << 2;
	memcpy(cross, words, 3 * sizeof(uint64_t));
	ck_assert_int_eq(dslogic_cross_to_packed(cross, 3 * sizeof(uint64_t),
			0x7, packed, sizeof(packed), &samples), SR_OK);
	ck_assert_uint_eq(samples, 64);
	ck_assert_uint_eq(packed[0], 0x01);
	ck_assert_uint_eq(packed[1], 0x02);
	ck_assert_uint_eq(packed[2], 0x04);
	ck_assert_uint_eq(packed[63], 0x01);

	memset(words, 0, sizeof(words));
	words[15] = UINT64_C(1) << 3;
	memcpy(cross, words, 16 * sizeof(uint64_t));
	ck_assert_int_eq(dslogic_cross_to_packed(cross, 16 * sizeof(uint64_t),
			0xffff, packed, sizeof(packed), &samples), SR_OK);
	ck_assert_uint_eq(packed[3 * 2 + 1], 0x80);

	memset(words, 0, sizeof(words));
	words[31] = UINT64_C(1) << 4;
	memcpy(cross, words, sizeof(words));
	ck_assert_int_eq(dslogic_cross_to_packed(cross, sizeof(words),
			UINT32_MAX, packed, sizeof(packed), &samples), SR_OK);
	ck_assert_uint_eq(packed[4 * 4 + 3], 0x80);
	ck_assert_int_eq(dslogic_cross_to_packed(cross, sizeof(words) - 1,
			UINT32_MAX, packed, sizeof(packed), &samples), SR_ERR_ARG);
}
END_TEST

START_TEST(test_cross_fixtures_cover_remainder_and_channel_order)
{
	uint8_t *cross, *complete = NULL, *packed;
	uint8_t remainder[sizeof(uint64_t) * 32] = { 0 };
	size_t length, remainder_length = 0, complete_length, sample_count;

	cross = read_fixture("cross-3ch-two-blocks.bin", &length);
	ck_assert_uint_eq(length, 3 * sizeof(uint64_t) * 3);
	ck_assert_int_eq(dslogic_cross_remainder_append(remainder, &remainder_length,
		sizeof(remainder), cross, 21, 0x7, &complete, &complete_length), SR_OK);
	ck_assert_ptr_null(complete);
	ck_assert_uint_eq(complete_length, 0);
	ck_assert_uint_eq(remainder_length, 21);
	ck_assert_int_eq(dslogic_cross_remainder_append(remainder, &remainder_length,
		sizeof(remainder), cross + 21, length - 21, 0x7,
		&complete, &complete_length), SR_OK);
	ck_assert_uint_eq(remainder_length, 0);
	ck_assert_uint_eq(complete_length, length);
	packed = g_malloc0(256);
	ck_assert_int_eq(dslogic_cross_to_packed(complete, complete_length, 0x7,
		packed, 256, &sample_count), SR_OK);
	ck_assert_uint_eq(sample_count, 192);
	assert_cross_matches_packed(cross, length, 0x7, packed, sample_count, 1);
	g_free(packed);
	g_free(complete);
	g_free(cross);

	cross = read_fixture("cross-16ch-boundary.bin", &length);
	packed = g_malloc0(256);
	ck_assert_int_eq(dslogic_cross_to_packed(cross, length, 0xffff,
		packed, 256, &sample_count), SR_OK);
	ck_assert_uint_eq(sample_count, 128);
	assert_cross_matches_packed(cross, length, 0xffff, packed, sample_count, 2);
	g_free(packed);
	g_free(cross);

	cross = read_fixture("cross-32ch-boundary.bin", &length);
	packed = g_malloc0(512);
	ck_assert_int_eq(dslogic_cross_to_packed(cross, length, UINT32_MAX,
		packed, 512, &sample_count), SR_OK);
	ck_assert_uint_eq(sample_count, 128);
	assert_cross_matches_packed(cross, length, UINT32_MAX, packed, sample_count, 4);
	g_free(packed);
	g_free(cross);
}
END_TEST

START_TEST(test_cross_fixture_rejects_incomplete_tail)
{
	uint8_t *cross, *complete = NULL, packed[64];
	uint8_t remainder[sizeof(uint64_t) * 32] = { 0 };
	size_t length, remainder_length = 0, complete_length, sample_count;

	cross = read_fixture("cross-incomplete-tail.bin", &length);
	ck_assert_int_eq(dslogic_cross_to_packed(cross, length, 0x7,
		packed, sizeof(packed), &sample_count), SR_ERR_ARG);
	ck_assert_int_eq(dslogic_cross_remainder_append(remainder, &remainder_length,
		sizeof(remainder), cross, length, 0x7, &complete, &complete_length), SR_OK);
	ck_assert_uint_eq(complete_length, 3 * sizeof(uint64_t));
	ck_assert_uint_eq(remainder_length, 6);
	g_free(complete);
	g_free(cross);
}
END_TEST

START_TEST(test_cross_fixture_limit_and_end_state_are_bounded)
{
	uint8_t *cross, *packed;
	gsize length;
	size_t sample_count, allowed_samples;
	struct dev_context *devc;

	cross = read_fixture("cross-3ch-two-blocks.bin", &length);
	packed = g_malloc0(256);
	ck_assert_int_eq(dslogic_cross_to_packed(cross, length, 0x7,
		packed, 256, &sample_count), SR_OK);
	ck_assert_uint_eq(sample_count, 192);
	allowed_samples = dslogic_sample_count_until_limit(65, 0, sample_count);
	ck_assert_uint_eq(allowed_samples, 65);
	assert_cross_matches_packed(cross, 3 * sizeof(uint64_t), 0x7,
		packed, 64, 1);
	ck_assert_uint_eq(dslogic_sample_count_until_limit(65, 64,
		sample_count - 64), 1);
	ck_assert_uint_eq(dslogic_sample_count_until_limit(65, 65, 1), 0);

	devc = dslogic_dev_new();
	ck_assert(dslogic_datafeed_end_once(devc));
	ck_assert(!dslogic_datafeed_end_once(devc));
	ck_assert(devc->datafeed_end_sent);
	ck_assert(!dslogic_hw_status_has_overflow(0x00));
	ck_assert(dslogic_hw_status_has_overflow(0x10));
	g_free(devc);
	g_free(packed);
	g_free(cross);
}
END_TEST

START_TEST(test_trigger_header_acceptance_matches_dsview_operation_semantics)
{
	const struct dslogic_profile *profile;
	struct dev_context *devc;

	profile = dslogic_profile_find(0x2a0e, 0x002d, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(profile);
	devc = dslogic_dev_new();
	devc->profile = profile;
	devc->operation_mode = DSLOGIC_OPERATION_BUFFER;
	devc->actual_limit_samples = UINT64_C(1048576);
	ck_assert(dslogic_trigger_header_starts_data(devc, 0));
	ck_assert(dslogic_trigger_header_starts_data(devc, UINT64_C(1048064)));
	ck_assert(!dslogic_trigger_header_starts_data(devc, UINT64_C(1048576)));
	ck_assert(!dslogic_trigger_header_starts_data(devc,
		UINT64_C(0x5555555555555555)));
	devc->actual_limit_samples = 0;
	ck_assert(!dslogic_trigger_header_starts_data(devc, 0));
	devc->operation_mode = DSLOGIC_OPERATION_STREAM;
	ck_assert(dslogic_trigger_header_starts_data(devc,
		UINT64_C(0x5555555555555555)));
	g_free(devc);
}
END_TEST

START_TEST(test_fpga_wire_image_has_tlv_and_stream_bits)
{
	const struct dslogic_profile *profile;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_session *session;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_channel *channel1, *channel31;
	uint8_t *base, *extension;
	unsigned int index;

	profile = dslogic_profile_find(0x2a0e, 0x002c, LIBUSB_SPEED_SUPER);
	ck_assert_ptr_nonnull(profile);
	sdi = sr_dev_inst_user_new("DreamSourceLab", profile->model, NULL);
	devc = dslogic_dev_new();
	sdi->priv = devc;
	devc->profile = profile;
	devc->cur_samplerate = SR_MHZ(250);
	devc->actual_limit_samples = 1024;
	devc->capture_ratio = 50;
	devc->selected_mode = dslogic_profile_select_mode(profile, 32,
		SR_MHZ(250));
	for (index = 0; index < 32; index++)
		ck_assert_int_eq(sr_dev_inst_channel_add(sdi, index, SR_CHANNEL_LOGIC, "D"), SR_OK);
	base = g_malloc0(dslogic_fpga_config_size(FALSE));
	extension = g_malloc0(dslogic_fpga_config_size(TRUE));
	ck_assert_int_eq(dslogic_build_fpga_config(sdi, base,
		dslogic_fpga_config_size(FALSE), extension,
		dslogic_fpga_config_size(TRUE)), SR_OK);
	ck_assert_uint_eq(read_le32(base), UINT32_C(0xf5a5f5a5));
	ck_assert_uint_eq(read_le16(base + 4), 0x0001);
	ck_assert_uint_eq(read_le16(base + 14), 0x0302);
	ck_assert_uint_eq(read_le16(base + 38), 0xffff);
	ck_assert_uint_eq(read_le16(base + 40), 0xffff);
	ck_assert_uint_eq(read_le32(base + dslogic_fpga_config_size(FALSE) - 4),
		UINT32_C(0xfa5afa5a));
	ck_assert_uint_eq(read_le32(extension), UINT32_C(0xf5a5f5a5));
	ck_assert_uint_eq(read_le16(extension + 4), 0x6060);
	ck_assert_uint_eq(read_le16(extension + 198), 0xffff);
	ck_assert_uint_eq(read_le32(extension + dslogic_fpga_config_size(TRUE) - 4),
		UINT32_C(0xfa5afa5a));
	/* DSView writes RLE only from the explicit RLE setting. */
	devc->actual_limit_samples = SR_GHZ(1);
	ck_assert_int_eq(dslogic_build_fpga_config(sdi, base,
		dslogic_fpga_config_size(FALSE), extension,
		dslogic_fpga_config_size(TRUE)), SR_OK);
	ck_assert(!(read_le16(base + 6) & (1 << 3)));
	devc->rle_mode = TRUE;
	ck_assert_int_eq(dslogic_build_fpga_config(sdi, base,
		dslogic_fpga_config_size(FALSE), extension,
		dslogic_fpga_config_size(TRUE)), SR_OK);
	ck_assert(read_le16(base + 6) & (1 << 3));
	devc->rle_mode = FALSE;
	devc->actual_limit_samples = 1024;
	devc->operation_mode = DSLOGIC_OPERATION_STREAM;
	devc->selected_mode = dslogic_profile_select_mode(profile, 32, SR_MHZ(50));
	devc->cur_samplerate = SR_MHZ(50);
	ck_assert_int_eq(dslogic_build_fpga_config(sdi, base,
		dslogic_fpga_config_size(FALSE), extension,
		dslogic_fpga_config_size(TRUE)), SR_OK);
	ck_assert(read_le16(base + 6) & (1 << 12));

	/* Exercise the standard trigger path, including disabled and high channels. */
	ck_assert_int_eq(sr_session_new(srtest_ctx, &session), SR_OK);
	sdi->session = session;
	trigger = sr_trigger_new("DSLogic hardware trigger");
	stage = sr_trigger_stage_add(trigger);
	channel1 = g_slist_nth_data(sdi->channels, 1);
	channel31 = g_slist_nth_data(sdi->channels, 31);
	channel1->enabled = FALSE;
	ck_assert_int_eq(sr_trigger_match_add(stage,
		g_slist_nth_data(sdi->channels, 0), SR_TRIGGER_RISING, 0), SR_OK);
	ck_assert_int_eq(sr_trigger_match_add(stage, channel1, SR_TRIGGER_ONE, 0), SR_OK);
	ck_assert_int_eq(sr_trigger_match_add(stage, channel31, SR_TRIGGER_FALLING, 0), SR_OK);
	ck_assert_int_eq(sr_session_trigger_set(session, trigger), SR_OK);
	devc->operation_mode = DSLOGIC_OPERATION_BUFFER;
	devc->selected_mode = dslogic_profile_select_mode(profile, 31, SR_MHZ(50));
	ck_assert_int_eq(dslogic_build_fpga_config(sdi, base,
		dslogic_fpga_config_size(FALSE), extension,
		dslogic_fpga_config_size(TRUE)), SR_OK);
	ck_assert(!(read_le16(base + 6) & (1 << 12)));
	ck_assert_uint_eq(read_le16(base + 22), 512);
	ck_assert_uint_eq(read_le16(base + 38), 0xfffd);
	ck_assert_uint_eq(read_le16(base + 48), 0xfffe);
	ck_assert_uint_eq(read_le16(base + 80), 0xfffe);
	ck_assert_uint_eq(read_le16(base + 112), 0x0001);
	ck_assert_uint_eq(read_le16(base + 176), 0x0001);
	ck_assert_uint_eq(read_le16(extension + 6), 0x7fff);
	ck_assert_uint_eq(read_le16(extension + 38), 0x7fff);
	ck_assert_uint_eq(read_le16(extension + 134), 0x8000);
	ck_assert_uint_eq(read_le16(extension + 166), 0x8000);
	sr_session_destroy(session);
	sdi->session = NULL;
	g_free(extension);
	g_free(base);
	g_free(devc);
	sdi->priv = NULL;
	sr_dev_inst_free(sdi);
}
END_TEST

START_TEST(test_trigger_word_transforms_match_dsview)
{
	ck_assert_uint_eq(dslogic_transform_trigger_word(0x1234, FALSE, FALSE, FALSE),
		0x1234);
	ck_assert_uint_eq(dslogic_transform_trigger_word(0x12a5, FALSE, TRUE, FALSE),
		0xa5a5);
	ck_assert_uint_eq(dslogic_transform_trigger_word(0x12a5, TRUE, TRUE, FALSE),
		0x12a5);
	ck_assert_uint_eq(dslogic_transform_trigger_word(0x12a5, FALSE, FALSE, TRUE),
		0x5555);
	ck_assert_uint_eq(dslogic_transform_trigger_word(0x12a5, TRUE, FALSE, TRUE),
		0xa5a5);
}
END_TEST

START_TEST(test_profile_mode_limits)
{
	const struct dslogic_profile *profile;
	const struct dslogic_profile *profile16;
	const struct dslogic_channel_mode *mode;

	profile = dslogic_profile_find(0x2a0e, 0x002c, LIBUSB_SPEED_SUPER);
	ck_assert_ptr_nonnull(profile);
	ck_assert_uint_eq(profile->logic_channel_count, 32);

	mode = dslogic_profile_select_mode(profile, 32, SR_MHZ(50));
	ck_assert_ptr_nonnull(mode);
	ck_assert_str_eq(mode->id, "buffer-250x32");

	mode = dslogic_profile_select_mode(profile, 32, SR_MHZ(250));
	ck_assert_ptr_nonnull(mode);
	ck_assert_str_eq(mode->id, "buffer-250x32");

	ck_assert_ptr_null(dslogic_profile_select_mode(profile, 33, SR_MHZ(10)));
	ck_assert_ptr_null(dslogic_profile_select_mode(profile, 0, SR_MHZ(10)));
	ck_assert_ptr_null(dslogic_profile_select_mode(profile, 32, SR_GHZ(1) + 1));

	profile16 = dslogic_profile_find(0x2a0e, 0x002a, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(profile16);
	mode = dslogic_profile_select_mode(profile16, 16, SR_MHZ(500));
	ck_assert_ptr_nonnull(mode);
	ck_assert_str_eq(mode->id, "buffer-500x16");
}
END_TEST

START_TEST(test_u2pro16_profile_has_explicit_dsview_hardware_modes)
{
	const struct dslogic_profile *profile;
	const struct dslogic_channel_mode *mode;

	profile = dslogic_profile_find(0x2a0e, 0x002d, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(profile);
	ck_assert_uint_eq(profile->mem_depth, UINT64_C(4) * 1024 * 1024 * 1024);
	ck_assert_uint_eq(profile->mode_bitmap, 0x3f);
	ck_assert_int_eq(profile->default_operation_mode,
		DSLOGIC_OPERATION_BUFFER);
	ck_assert(dslogic_profile_operation_supported(profile,
		DSLOGIC_OPERATION_INTERNAL_TEST));
	mode = dslogic_profile_mode_for_operation(profile,
		DSLOGIC_OPERATION_INTERNAL_TEST);
	ck_assert_ptr_nonnull(mode);
	ck_assert_str_eq(mode->id, "buffer-500x16");
	ck_assert(profile->adf4360);
	ck_assert_uint_eq(profile->half_samplerate, SR_MHZ(500));
	ck_assert_uint_eq(profile->quarter_samplerate, SR_GHZ(1));
	mode = dslogic_profile_select_mode_for_operation(profile, 16,
		SR_MHZ(500), FALSE);
	ck_assert_ptr_nonnull(mode);
	ck_assert_str_eq(mode->id, "buffer-500x16");
	ck_assert_uint_eq(mode->hw_min_samplerate, SR_KHZ(10));
	ck_assert_uint_eq(mode->hw_max_samplerate, SR_MHZ(500));
	ck_assert_uint_eq(mode->pre_div, 5);
	mode = dslogic_profile_select_mode_for_operation(profile, 8,
		SR_GHZ(1), FALSE);
	ck_assert_ptr_nonnull(mode);
	ck_assert_str_eq(mode->id, "buffer-1000x8");
	mode = dslogic_profile_select_mode_for_operation(profile, 16,
		SR_MHZ(20), TRUE);
	ck_assert_ptr_nonnull(mode);
	ck_assert_str_eq(mode->id, "stream-20x16-3dn2");
}
END_TEST

START_TEST(test_invalid_channel_config_never_reaches_usb)
{
	struct sr_dev_driver *driver;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	unsigned int index;

	driver = srtest_driver_get("dreamsourcelab-dslogic");
	srtest_driver_init(srtest_ctx, driver);
	sdi = sr_dev_inst_user_new("DreamSourceLab", "DSLogic U3Pro32", NULL);
	ck_assert_ptr_nonnull(sdi);
	devc = dslogic_dev_new();
	ck_assert_ptr_nonnull(devc);
	devc->profile = dslogic_profile_find(0x2a0e, 0x002c, LIBUSB_SPEED_SUPER);
	devc->cur_samplerate = SR_MHZ(50);
	sdi->driver = driver;
	sdi->priv = devc;

	/* No connection is present: SR_ERR_ARG proves validation is before USB I/O. */
	ck_assert_int_eq(dslogic_acquisition_start(sdi), SR_ERR_ARG);
	ck_assert_int_eq(devc->acquisition_state, DSLOGIC_ACQ_INIT);
	for (index = 0; index < 33; index++)
		ck_assert_int_eq(sr_dev_inst_channel_add(sdi, index, SR_CHANNEL_LOGIC, "D"), SR_OK);
	ck_assert_int_eq(dslogic_acquisition_start(sdi), SR_ERR_ARG);
	ck_assert_int_eq(devc->acquisition_state, DSLOGIC_ACQ_INIT);

	g_free(devc);
	sdi->priv = NULL;
	sr_dev_inst_free(sdi);
}
END_TEST

START_TEST(test_u2pro16_samplerate_list_and_setter_follow_enabled_channels)
{
	struct sr_dev_driver *driver;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	GVariant *value, *rates, *item;
	unsigned int index;
	gboolean has_500m = FALSE, has_1g = FALSE;

	driver = srtest_driver_get("dreamsourcelab-dslogic");
	srtest_driver_init(srtest_ctx, driver);
	sdi = sr_dev_inst_user_new("DreamSourceLab", "DSLogic U2Pro16", NULL);
	ck_assert_ptr_nonnull(sdi);
	devc = dslogic_dev_new();
	ck_assert_ptr_nonnull(devc);
	devc->profile = dslogic_profile_find(0x2a0e, 0x002d, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(devc->profile);
	devc->cur_samplerate = SR_MHZ(100);
	sdi->driver = driver;
	sdi->priv = devc;
	for (index = 0; index < 16; index++)
		ck_assert_int_eq(sr_dev_inst_channel_add(sdi, index,
			SR_CHANNEL_LOGIC, "D"), SR_OK);

	ck_assert_int_eq(driver->config_list(SR_CONF_SAMPLERATE, &value,
		sdi, NULL), SR_OK);
	rates = g_variant_lookup_value(value, "samplerates", G_VARIANT_TYPE("at"));
	ck_assert_ptr_nonnull(rates);
	for (index = 0; index < g_variant_n_children(rates); index++) {
		item = g_variant_get_child_value(rates, index);
		has_500m |= g_variant_get_uint64(item) == SR_MHZ(500);
		has_1g |= g_variant_get_uint64(item) == SR_GHZ(1);
		g_variant_unref(item);
	}
	g_variant_unref(rates);
	ck_assert(has_500m);
	ck_assert(!has_1g);

	value = g_variant_new_uint64(SR_GHZ(1));
	ck_assert_int_eq(driver->config_set(SR_CONF_SAMPLERATE, value, sdi, NULL),
		SR_ERR_ARG);
	for (index = 8; index < 16; index++)
		((struct sr_channel *)g_slist_nth_data(sdi->channels, index))->enabled = FALSE;
	value = g_variant_new_uint64(SR_GHZ(1));
	ck_assert_int_eq(driver->config_set(SR_CONF_SAMPLERATE, value, sdi, NULL),
		SR_OK);
	/* U2Pro16's 100 MHz stream mode is a three-channel mode. */
	for (index = 3; index < 8; index++)
		((struct sr_channel *)g_slist_nth_data(sdi->channels, index))->enabled = FALSE;
	value = g_variant_new_uint64(SR_MHZ(100));
	ck_assert_int_eq(driver->config_set(SR_CONF_SAMPLERATE, value, sdi, NULL),
		SR_OK);
	value = g_variant_new_string("stream");
	ck_assert_int_eq(driver->config_set(SR_CONF_DEVICE_MODE, value, sdi, NULL),
		SR_OK);
	value = g_variant_new_uint64(SR_MHZ(500));
	ck_assert_int_eq(driver->config_set(SR_CONF_SAMPLERATE, value, sdi, NULL),
		SR_ERR_ARG);

	g_free(devc);
	sdi->priv = NULL;
	sr_dev_inst_free(sdi);
}
END_TEST

START_TEST(test_profile_samplerate_validation)
{
	const struct dslogic_profile *profile;
	const struct dslogic_channel_mode *mode;

	profile = dslogic_profile_find(0x2a0e, 0x002a, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(profile);
	mode = dslogic_profile_mode_at(profile, 0);
	ck_assert_ptr_nonnull(mode);
	ck_assert(dslogic_profile_samplerate_supported(profile, mode, SR_MHZ(20)));
	ck_assert(!dslogic_profile_samplerate_supported(profile, mode, SR_MHZ(25)));
	ck_assert(!dslogic_profile_samplerate_supported(profile, mode, 0));
}
END_TEST

START_TEST(test_every_mode_has_explicit_limits)
{
	size_t profile_index;

	for (profile_index = 0; profile_index < G_N_ELEMENTS(test_profiles); profile_index++) {
		const struct dslogic_profile *profile = dslogic_profile_find(0x2a0e,
			test_profiles[profile_index].pid, test_profiles[profile_index].speed);
		size_t mode_index;

		ck_assert_ptr_nonnull(profile);
		ck_assert_uint_lt(profile->internal_test_mode, profile->mode_count);
		ck_assert(dslogic_profile_operation_supported(profile,
			DSLOGIC_OPERATION_INTERNAL_TEST));
		ck_assert_ptr_nonnull(dslogic_profile_mode_for_operation(profile,
			DSLOGIC_OPERATION_INTERNAL_TEST));

		for (mode_index = 0; mode_index < profile->mode_count; mode_index++) {
			const struct dslogic_channel_mode *mode =
				dslogic_profile_mode_at(profile, mode_index);

			ck_assert_ptr_nonnull(mode);
			ck_assert_uint_gt(mode->channel_count, 0);
			ck_assert_uint_le(mode->channel_count, profile->logic_channel_count);
			ck_assert_uint_gt(mode->min_samplerate, 0);
			ck_assert_uint_ge(mode->max_samplerate, mode->min_samplerate);
			ck_assert_uint_gt(mode->hw_min_samplerate, 0);
			ck_assert_uint_ge(mode->hw_max_samplerate, mode->hw_min_samplerate);
			ck_assert_uint_gt(mode->pre_div, 0);
			ck_assert(dslogic_profile_samplerate_supported(profile, mode,
					mode->min_samplerate));
			ck_assert(dslogic_profile_samplerate_supported(profile, mode,
					mode->max_samplerate));
		}
	}
}
END_TEST

START_TEST(test_continuous_mode_mapping)
{
	ck_assert(dslogic_operation_mode_is_continuous(DSLOGIC_OPERATION_STREAM));
	ck_assert(!dslogic_operation_mode_is_continuous(DSLOGIC_OPERATION_BUFFER));
	ck_assert(!dslogic_operation_mode_is_continuous(DSLOGIC_OPERATION_INTERNAL_TEST));
	ck_assert(!dslogic_operation_mode_is_continuous(DSLOGIC_OPERATION_UNSET));
}
END_TEST

START_TEST(test_continuous_mode_rejects_bufferless_profile)
{
	struct sr_dev_driver *driver;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	const struct dslogic_profile *profile;
	GVariant *value;
	unsigned int index;

	driver = srtest_driver_get("dreamsourcelab-dslogic");
	srtest_driver_init(srtest_ctx, driver);
	profile = dslogic_profile_find(0x2a0e, 0x0021, LIBUSB_SPEED_HIGH);
	ck_assert_ptr_nonnull(profile);
	ck_assert(!dslogic_profile_operation_supported(profile,
		DSLOGIC_OPERATION_BUFFER));

	sdi = sr_dev_inst_user_new("DreamSourceLab", "DSLogic Basic", NULL);
	ck_assert_ptr_nonnull(sdi);
	devc = dslogic_dev_new();
	ck_assert_ptr_nonnull(devc);
	devc->profile = profile;
	sdi->driver = driver;
	sdi->priv = devc;
	for (index = 0; index < 16; index++)
		ck_assert_int_eq(sr_dev_inst_channel_add(sdi, index,
			SR_CHANNEL_LOGIC, "D"), SR_OK);

	value = g_variant_new_string("stream");
	ck_assert_int_eq(driver->config_set(SR_CONF_DEVICE_MODE, value, sdi, NULL), SR_OK);
	value = g_variant_new_boolean(FALSE);
	ck_assert_int_eq(driver->config_set(SR_CONF_CONTINUOUS, value, sdi, NULL), SR_ERR_ARG);
	ck_assert_int_eq(devc->operation_mode, DSLOGIC_OPERATION_STREAM);
	value = g_variant_new_boolean(TRUE);
	ck_assert_int_eq(driver->config_set(SR_CONF_CONTINUOUS, value, sdi, NULL), SR_OK);
	ck_assert_int_eq(devc->operation_mode, DSLOGIC_OPERATION_STREAM);

	g_free(devc);
	sdi->priv = NULL;
}
END_TEST

Suite *suite_dreamsourcelab_dslogic(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("dreamsourcelab-dslogic");
	tc = tcase_create("profiles");
	tcase_add_test(tc, test_profile_identity_is_unique);
	tcase_add_test(tc, test_profile_mode_limits);
	tcase_add_test(tc, test_profile_samplerate_validation);
	tcase_add_test(tc, test_u2pro16_profile_has_explicit_dsview_hardware_modes);
	tcase_add_test(tc, test_every_mode_has_explicit_limits);
	tcase_add_test(tc, test_continuous_mode_mapping);
	tcase_add_test(tc, test_continuous_mode_rejects_bufferless_profile);
	tcase_add_test(tc, test_invalid_channel_config_never_reaches_usb);
	tcase_add_test(tc, test_u2pro16_samplerate_list_and_setter_follow_enabled_channels);
	tcase_add_test(tc, test_fpga_firmware_selection_comes_from_profile);
	tcase_add_test(tc, test_fx2_cold_boot_assets_are_explicit);
	tcase_add_test(tc, test_fpga_upload_uses_fake_resource_and_usb_ops);
	tcase_add_test(tc, test_fpga_done_prepare_verifies_without_uploading);
	tcase_add_test(tc, test_fpga_done_prepare_rejects_incompatible_hdl_without_uploading);
	tcase_add_test(tc, test_fpga_done_prepare_rejects_hdl_read_failure_without_uploading);
	tcase_add_test(tc, test_fpga_done_prepare_runs_security_check_without_uploading);
	tcase_add_test(tc, test_fpga_done_prepare_skips_security_for_nonsecure_profile);
	tcase_add_test(tc, test_fpga_not_done_prepare_uploads_once);
	tcase_add_test(tc, test_fpga_recovery_uses_force_ready_stop_and_transport_reset);
	tcase_add_test(tc, test_fpga_upload_failure_paths_close_resource);
	tcase_add_test(tc, test_fpga_upload_every_hardware_failure_recovers);
	tcase_add_test(tc, test_acquisition_start_order_is_hardware_safe);
	tcase_add_test(tc, test_acquisition_start_failures_drain_then_recover);
	tcase_add_test(tc, test_acquisition_stop_drains_before_recover);
	tcase_add_test(tc, test_cross_conversion_packs_3_16_32_channels);
	tcase_add_test(tc, test_cross_fixtures_cover_remainder_and_channel_order);
	tcase_add_test(tc, test_cross_fixture_rejects_incomplete_tail);
	tcase_add_test(tc, test_cross_fixture_limit_and_end_state_are_bounded);
	tcase_add_test(tc, test_trigger_header_acceptance_matches_dsview_operation_semantics);
	tcase_add_test(tc, test_fpga_wire_image_has_tlv_and_stream_bits);
	tcase_add_test(tc, test_trigger_word_transforms_match_dsview);
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_standard_config_matrix);
	suite_add_tcase(s, tc);

	return s;
}

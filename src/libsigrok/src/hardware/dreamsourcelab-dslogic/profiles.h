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

#ifndef LIBSIGROK_HARDWARE_DREAMSOURCELAB_DSLOGIC_PROFILES_H
#define LIBSIGROK_HARDWARE_DREAMSOURCELAB_DSLOGIC_PROFILES_H

#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>

enum dslogic_capability {
	DSLOGIC_CAP_VOLTAGE_THRESHOLD = 1 << 0,
	DSLOGIC_CAP_BUFFER = 1 << 1,
	DSLOGIC_CAP_32_CHANNELS = 1 << 3,
	DSLOGIC_CAP_SECURITY = 1 << 5,
	DSLOGIC_CAP_RLE = 1 << 6,
	DSLOGIC_CAP_FILTER = 1 << 8,
};

/* Mirrors DSView's buffer, stream, and internal-test operation states. */
enum dslogic_operation_mode {
	DSLOGIC_OPERATION_UNSET,
	DSLOGIC_OPERATION_BUFFER,
	DSLOGIC_OPERATION_STREAM,
	DSLOGIC_OPERATION_INTERNAL_TEST,
};

#define DSLOGIC_OPERATION_MASK(mode) (UINT32_C(1) << (mode))

struct dslogic_channel_mode {
	const char *id;
	unsigned int channel_count;
	uint64_t min_samplerate;
	uint64_t max_samplerate;
	gboolean stream;
	uint64_t hw_min_samplerate;
	uint64_t hw_max_samplerate;
	uint8_t pre_div;
};

struct dslogic_profile {
	uint16_t vid;
	uint16_t pid;
	enum libusb_speed usb_speed;
	const char *vendor;
	const char *model;
	const char *model_version;
	const char *firmware;
	const char *fpga_firmware_3v3;
	const char *fpga_firmware_5v;
	uint64_t mem_depth;
	unsigned int logic_channel_count;
	unsigned int capabilities;
	const struct dslogic_channel_mode *modes;
	size_t mode_count;
	size_t default_mode;
	uint32_t mode_bitmap;
	uint32_t operation_modes;
	enum dslogic_operation_mode default_operation_mode;
	size_t internal_test_mode;
	gboolean adf4360;
	uint64_t half_samplerate;
	uint64_t quarter_samplerate;
};

SR_PRIV const struct dslogic_profile *dslogic_profile_find(
		uint16_t vid, uint16_t pid, enum libusb_speed usb_speed);
SR_PRIV const struct dslogic_channel_mode *dslogic_profile_mode_at(
		const struct dslogic_profile *profile, size_t index);
SR_PRIV const struct dslogic_channel_mode *dslogic_profile_select_mode(
		const struct dslogic_profile *profile, unsigned int enabled_channels,
		uint64_t samplerate);
SR_PRIV const struct dslogic_channel_mode *dslogic_profile_select_mode_for_operation(
		const struct dslogic_profile *profile, unsigned int enabled_channels,
		uint64_t samplerate, gboolean stream);
/* Caller owns the returned GArray, whose elements are uint64_t samplerates. */
SR_PRIV GArray *dslogic_profile_samplerates_for_operation(
		const struct dslogic_profile *profile, unsigned int enabled_channels,
		gboolean stream);
SR_PRIV gboolean dslogic_profile_samplerate_is_available(
		const struct dslogic_profile *profile, unsigned int enabled_channels,
		uint64_t samplerate, gboolean stream);
SR_PRIV gboolean dslogic_profile_samplerate_supported(
		const struct dslogic_profile *profile,
		const struct dslogic_channel_mode *mode, uint64_t samplerate);
SR_PRIV gboolean dslogic_profile_operation_supported(
		const struct dslogic_profile *profile,
		enum dslogic_operation_mode operation_mode);
SR_PRIV gboolean dslogic_profile_operation_is_stream(
		const struct dslogic_profile *profile,
		enum dslogic_operation_mode operation_mode);
/* TRUE only for modes that run without a driver-imposed sample/time cap. */
SR_PRIV gboolean dslogic_operation_mode_is_continuous(
		enum dslogic_operation_mode operation_mode);
SR_PRIV const struct dslogic_channel_mode *dslogic_profile_mode_for_operation(
		const struct dslogic_profile *profile,
		enum dslogic_operation_mode operation_mode);
/* NULL means the profile supports normal operation only after USB firmware is loaded. */
SR_PRIV const char *dslogic_profile_fx2_firmware(
		const struct dslogic_profile *profile);

#endif

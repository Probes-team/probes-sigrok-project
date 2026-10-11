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

#include "profiles.h"

#define DSLOGIC_KIB(value) (UINT64_C(value) * 1024)
#define DSLOGIC_MIB(value) (DSLOGIC_KIB(value) * 1024)
#define DSLOGIC_GIB(value) (DSLOGIC_MIB(value) * 1024)
#define DSLOGIC_FIRMWARE(name) "dreamsourcelab-dslogic/" name
#define MODE(id_, channels_, min_, max_, hw_min_, hw_max_, pre_div_, stream_) \
	{ id_, channels_, min_, max_, stream_, hw_min_, hw_max_, pre_div_ }

/* DSView dsl.h samplerates1000: a common superset for all logic profiles. */
static const uint64_t dsview_logic_samplerates[] = {
	SR_HZ(10), SR_HZ(20), SR_HZ(50), SR_HZ(100), SR_HZ(200), SR_HZ(500),
	SR_KHZ(1), SR_KHZ(2), SR_KHZ(5), SR_KHZ(10), SR_KHZ(20), SR_KHZ(40),
	SR_KHZ(50), SR_KHZ(100), SR_KHZ(200), SR_KHZ(400), SR_KHZ(500),
	SR_MHZ(1), SR_MHZ(2), SR_MHZ(4), SR_MHZ(5), SR_MHZ(10), SR_MHZ(20),
	SR_MHZ(25), SR_MHZ(50), SR_MHZ(100), SR_MHZ(125), SR_MHZ(250),
	SR_MHZ(500), SR_GHZ(1),
};

/*
 * These logic-only mode limits are transcribed from DSView's DSL driver.
 * DSO and DAQ modes intentionally remain out of this driver's scope.
 */
static const struct dslogic_channel_mode modes_legacy[] = {
	MODE("stream-20x16", 16, SR_KHZ(50), SR_MHZ(20), SR_KHZ(10), SR_MHZ(100), 1, TRUE),
	MODE("stream-25x12", 12, SR_KHZ(50), SR_MHZ(25), SR_KHZ(10), SR_MHZ(100), 1, TRUE),
	MODE("stream-50x6", 6, SR_KHZ(50), SR_MHZ(50), SR_KHZ(10), SR_MHZ(100), 1, TRUE),
	MODE("stream-100x3", 3, SR_KHZ(50), SR_MHZ(100), SR_KHZ(10), SR_MHZ(100), 1, TRUE),
	MODE("buffer-100x16", 16, SR_KHZ(50), SR_MHZ(100), SR_KHZ(10), SR_MHZ(100), 1, FALSE),
	MODE("buffer-200x8", 8, SR_KHZ(50), SR_MHZ(200), SR_KHZ(10), SR_MHZ(100), 1, FALSE),
	MODE("buffer-400x4", 4, SR_KHZ(50), SR_MHZ(400), SR_KHZ(10), SR_MHZ(100), 1, FALSE),
};

static const struct dslogic_channel_mode modes_u2basic[] = {
	MODE("stream-20x16", 16, SR_KHZ(50), SR_MHZ(20), SR_KHZ(10), SR_MHZ(100), 1, TRUE),
	MODE("stream-25x12", 12, SR_KHZ(50), SR_MHZ(25), SR_KHZ(10), SR_MHZ(100), 1, TRUE),
	MODE("stream-50x6", 6, SR_KHZ(50), SR_MHZ(50), SR_KHZ(10), SR_MHZ(100), 1, TRUE),
	MODE("stream-100x3", 3, SR_KHZ(50), SR_MHZ(100), SR_KHZ(10), SR_MHZ(100), 1, TRUE),
	MODE("buffer-100x16", 16, SR_KHZ(50), SR_MHZ(100), SR_KHZ(10), SR_MHZ(100), 1, FALSE),
};

static const struct dslogic_channel_mode modes_u3_16_high[] = {
	MODE("stream-20x16", 16, SR_KHZ(100), SR_MHZ(20), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-25x12", 12, SR_KHZ(100), SR_MHZ(25), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-50x6", 6, SR_KHZ(100), SR_MHZ(50), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-100x3", 3, SR_KHZ(100), SR_MHZ(100), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("buffer-500x16", 16, SR_MHZ(1), SR_MHZ(500), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
	MODE("buffer-1000x8", 8, SR_MHZ(1), SR_GHZ(1), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
};

/* DSView U2Pro16: 3DN2 stream modes, ADF4360 clocking, and 4 GiB buffer. */
static const struct dslogic_channel_mode modes_u2pro16_high[] = {
	MODE("stream-20x16-3dn2", 16, SR_KHZ(100), SR_MHZ(20), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-25x12-3dn2", 12, SR_KHZ(100), SR_MHZ(25), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-50x6-3dn2", 6, SR_KHZ(100), SR_MHZ(50), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-100x3-3dn2", 3, SR_KHZ(100), SR_MHZ(100), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("buffer-500x16", 16, SR_MHZ(1), SR_MHZ(500), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
	MODE("buffer-1000x8", 8, SR_MHZ(1), SR_GHZ(1), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
};

static const struct dslogic_channel_mode modes_u3_16_super[] = {
	MODE("stream-125x16", 16, SR_MHZ(1), SR_MHZ(125), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-250x12", 12, SR_MHZ(1), SR_MHZ(250), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-500x6", 6, SR_MHZ(1), SR_MHZ(500), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-1000x3", 3, SR_MHZ(1), SR_GHZ(1), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("buffer-500x16", 16, SR_MHZ(1), SR_MHZ(500), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
	MODE("buffer-1000x8", 8, SR_MHZ(1), SR_GHZ(1), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
};

static const struct dslogic_channel_mode modes_u3_32_high[] = {
	MODE("stream-10x32", 32, SR_KHZ(100), SR_MHZ(10), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-20x16", 16, SR_KHZ(100), SR_MHZ(20), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-25x12", 12, SR_KHZ(100), SR_MHZ(25), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-50x6", 6, SR_KHZ(100), SR_MHZ(50), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-100x3", 3, SR_KHZ(100), SR_MHZ(100), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("buffer-250x32", 32, SR_MHZ(1), SR_MHZ(250), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
	MODE("buffer-500x16", 16, SR_MHZ(1), SR_MHZ(500), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
	MODE("buffer-1000x8", 8, SR_MHZ(1), SR_GHZ(1), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
};

static const struct dslogic_channel_mode modes_u3_32_super[] = {
	MODE("stream-50x32", 32, SR_MHZ(1), SR_MHZ(50), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-100x30", 30, SR_MHZ(1), SR_MHZ(100), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-250x12", 12, SR_MHZ(1), SR_MHZ(250), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-500x6", 6, SR_MHZ(1), SR_MHZ(500), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("stream-1000x3", 3, SR_MHZ(1), SR_GHZ(1), SR_KHZ(10), SR_MHZ(500), 5, TRUE),
	MODE("buffer-250x32", 32, SR_MHZ(1), SR_MHZ(250), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
	MODE("buffer-500x16", 16, SR_MHZ(1), SR_MHZ(500), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
	MODE("buffer-1000x8", 8, SR_MHZ(1), SR_GHZ(1), SR_KHZ(10), SR_MHZ(500), 5, FALSE),
};

#define PROFILE(vid_, pid_, speed_, model_, fw_, fpga3_, fpga5_, depth_, channels_, caps_, modes_, default_) \
	{ vid_, pid_, speed_, "DreamSourceLab", model_, NULL, fw_, fpga3_, fpga5_, \
		depth_, channels_, caps_, modes_, G_N_ELEMENTS(modes_), default_, 0, \
		FALSE, 0, 0 }
#define PROFILE_U2PRO16 \
	{ 0x2a0e, 0x002d, LIBUSB_SPEED_HIGH, "DreamSourceLab", "DSLogic U2Pro16", NULL, \
		NULL, DSLOGIC_FIRMWARE("DSLogicU2Pro16.bin"), DSLOGIC_FIRMWARE("DSLogicU2Pro16.bin"), \
		DSLOGIC_GIB(4), 16, DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD | \
		DSLOGIC_CAP_SECURITY | DSLOGIC_CAP_RLE | DSLOGIC_CAP_FILTER, \
		modes_u2pro16_high, G_N_ELEMENTS(modes_u2pro16_high), 4, 0x3f, \
		TRUE, SR_MHZ(500), SR_GHZ(1) }

static const struct dslogic_profile profiles[] = {
	PROFILE(0x2a0e, 0x0001, LIBUSB_SPEED_HIGH, "DSLogic",
		DSLOGIC_FIRMWARE("DSLogic.fw"), DSLOGIC_FIRMWARE("DSLogic33.bin"), DSLOGIC_FIRMWARE("DSLogic50.bin"), DSLOGIC_MIB(256), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD | DSLOGIC_CAP_RLE |
		DSLOGIC_CAP_FILTER, modes_legacy, 4),
	PROFILE(0x2a0e, 0x0003, LIBUSB_SPEED_HIGH, "DSLogic Pro",
		DSLOGIC_FIRMWARE("DSLogicPro.fw"), DSLOGIC_FIRMWARE("DSLogicPro.bin"), DSLOGIC_FIRMWARE("DSLogicPro.bin"), DSLOGIC_MIB(256), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD | DSLOGIC_CAP_RLE |
		DSLOGIC_CAP_FILTER, modes_legacy, 4),
	PROFILE(0x2a0e, 0x0020, LIBUSB_SPEED_HIGH, "DSLogic Plus",
		DSLOGIC_FIRMWARE("DSLogicPlus.fw"), DSLOGIC_FIRMWARE("DSLogicPlus.bin"), DSLOGIC_FIRMWARE("DSLogicPlus.bin"), DSLOGIC_MIB(256), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD | DSLOGIC_CAP_RLE |
		DSLOGIC_CAP_FILTER, modes_legacy, 4),
	PROFILE(0x2a0e, 0x0021, LIBUSB_SPEED_HIGH, "DSLogic Basic",
		DSLOGIC_FIRMWARE("DSLogicBasic.fw"), DSLOGIC_FIRMWARE("DSLogicBasic.bin"), DSLOGIC_FIRMWARE("DSLogicBasic.bin"), DSLOGIC_KIB(256), 16,
		DSLOGIC_CAP_VOLTAGE_THRESHOLD | DSLOGIC_CAP_RLE |
		DSLOGIC_CAP_FILTER, modes_legacy, 0),
	PROFILE(0x2a0e, 0x0029, LIBUSB_SPEED_HIGH, "DSLogic U2Basic",
		NULL, DSLOGIC_FIRMWARE("DSLogicU2Basic.bin"), DSLOGIC_FIRMWARE("DSLogicU2Basic.bin"), DSLOGIC_MIB(64), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD | DSLOGIC_CAP_RLE |
		DSLOGIC_CAP_FILTER, modes_u2basic, 4),
	PROFILE(0x2a0e, 0x002a, LIBUSB_SPEED_HIGH, "DSLogic U3Pro16",
		NULL, DSLOGIC_FIRMWARE("DSLogicU3Pro16.bin"), DSLOGIC_FIRMWARE("DSLogicU3Pro16.bin"), DSLOGIC_GIB(2), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD |
		DSLOGIC_CAP_RLE | DSLOGIC_CAP_FILTER,
		modes_u3_16_high, 4),
	PROFILE(0x2a0e, 0x002a, LIBUSB_SPEED_SUPER, "DSLogic U3Pro16",
		NULL, DSLOGIC_FIRMWARE("DSLogicU3Pro16.bin"), DSLOGIC_FIRMWARE("DSLogicU3Pro16.bin"), DSLOGIC_GIB(2), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD |
		DSLOGIC_CAP_RLE | DSLOGIC_CAP_FILTER,
		modes_u3_16_super, 4),
	PROFILE(0x2a0e, 0x002c, LIBUSB_SPEED_HIGH, "DSLogic U3Pro32",
		NULL, DSLOGIC_FIRMWARE("DSLogicU3Pro32.bin"), DSLOGIC_FIRMWARE("DSLogicU3Pro32.bin"), DSLOGIC_GIB(2), 32,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD |
		DSLOGIC_CAP_32_CHANNELS | DSLOGIC_CAP_RLE |
		DSLOGIC_CAP_FILTER,
		modes_u3_32_high, 5),
	PROFILE(0x2a0e, 0x002c, LIBUSB_SPEED_SUPER, "DSLogic U3Pro32",
		NULL, DSLOGIC_FIRMWARE("DSLogicU3Pro32.bin"), DSLOGIC_FIRMWARE("DSLogicU3Pro32.bin"), DSLOGIC_GIB(2), 32,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD |
		DSLOGIC_CAP_32_CHANNELS | DSLOGIC_CAP_RLE |
		DSLOGIC_CAP_FILTER,
		modes_u3_32_super, 5),
	PROFILE_U2PRO16,
	PROFILE(0x2a0e, 0x0030, LIBUSB_SPEED_HIGH, "DSLogic Plus",
		DSLOGIC_FIRMWARE("DSLogicPlus.fw"), DSLOGIC_FIRMWARE("DSLogicPlus-pgl12.bin"), DSLOGIC_FIRMWARE("DSLogicPlus-pgl12.bin"), DSLOGIC_MIB(256), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD | DSLOGIC_CAP_SECURITY |
		DSLOGIC_CAP_RLE | DSLOGIC_CAP_FILTER,
		modes_legacy, 4),
	PROFILE(0x2a0e, 0x0031, LIBUSB_SPEED_HIGH, "DSLogic U2Basic",
		NULL, DSLOGIC_FIRMWARE("DSLogicU2Basic-pgl12.bin"), DSLOGIC_FIRMWARE("DSLogicU2Basic-pgl12.bin"), DSLOGIC_MIB(64), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD | DSLOGIC_CAP_SECURITY |
		DSLOGIC_CAP_RLE | DSLOGIC_CAP_FILTER,
		modes_u2basic, 4),
	PROFILE(0x2a0e, 0x0034, LIBUSB_SPEED_HIGH, "DSLogic Plus",
		NULL, DSLOGIC_FIRMWARE("DSLogicPlus-pgl12-2.bin"), DSLOGIC_FIRMWARE("DSLogicPlus-pgl12-2.bin"), DSLOGIC_MIB(256), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD | DSLOGIC_CAP_SECURITY |
		DSLOGIC_CAP_RLE | DSLOGIC_CAP_FILTER,
		modes_legacy, 4),
	PROFILE(0x2a0e, 0x0035, LIBUSB_SPEED_HIGH, "DSLogic U2Basic",
		NULL, DSLOGIC_FIRMWARE("DSLogicU2Basic-pgl12-2.bin"), DSLOGIC_FIRMWARE("DSLogicU2Basic-pgl12-2.bin"), DSLOGIC_MIB(64), 16,
		DSLOGIC_CAP_BUFFER | DSLOGIC_CAP_VOLTAGE_THRESHOLD | DSLOGIC_CAP_SECURITY |
		DSLOGIC_CAP_RLE | DSLOGIC_CAP_FILTER,
		modes_u2basic, 4),
};

const struct dslogic_profile *dslogic_profile_find(
		uint16_t vid, uint16_t pid, enum libusb_speed usb_speed)
{
	size_t index;

	for (index = 0; index < G_N_ELEMENTS(profiles); index++) {
		if (profiles[index].vid == vid && profiles[index].pid == pid &&
				profiles[index].usb_speed == usb_speed)
			return &profiles[index];
	}

	return NULL;
}

const struct dslogic_channel_mode *dslogic_profile_mode_at(
		const struct dslogic_profile *profile, size_t index)
{
	if (!profile || index >= profile->mode_count)
		return NULL;

	return &profile->modes[index];
}

gboolean dslogic_profile_stream_supported(
		const struct dslogic_profile *profile, gboolean stream)
{
	if (!profile)
		return FALSE;

	if (stream)
		return TRUE;

	return (profile->capabilities & DSLOGIC_CAP_BUFFER) != 0;
}

const struct dslogic_channel_mode *dslogic_profile_mode_for_stream(
		const struct dslogic_profile *profile, gboolean stream)
{
	size_t index;

	if (!dslogic_profile_stream_supported(profile, stream))
		return NULL;

	for (index = 0; index < profile->mode_count; index++) {
		if (profile->modes[index].stream == stream)
			return &profile->modes[index];
	}

	return NULL;
}

gboolean dslogic_profile_samplerate_supported(
		const struct dslogic_profile *profile,
		const struct dslogic_channel_mode *mode, uint64_t samplerate)
{
	if (!profile || !mode || samplerate == 0)
		return FALSE;

	return samplerate >= mode->min_samplerate && samplerate <= mode->max_samplerate;
}

const char *dslogic_profile_fx2_firmware(
		const struct dslogic_profile *profile)
{
	return profile ? profile->firmware : NULL;
}


const struct dslogic_channel_mode *dslogic_profile_select_mode_for_stream(
		const struct dslogic_profile *profile, unsigned int enabled_channels,
		uint64_t samplerate, gboolean stream)
{
	const struct dslogic_channel_mode *best = NULL;
	size_t index;

	if (!profile || enabled_channels == 0 ||
			enabled_channels > profile->logic_channel_count)
		return NULL;

	for (index = 0; index < profile->mode_count; index++) {
		const struct dslogic_channel_mode *mode = &profile->modes[index];

		if (mode->stream != stream || (!stream &&
				!(profile->capabilities & DSLOGIC_CAP_BUFFER)) ||
				mode->channel_count < enabled_channels ||
				mode->hw_min_samplerate == 0 || mode->hw_max_samplerate == 0 ||
				mode->pre_div == 0 ||
				!dslogic_profile_samplerate_supported(profile, mode, samplerate))
			continue;
		/* DSView table order is the final priority tie-breaker. */
		if (!best || mode->channel_count < best->channel_count ||
				(mode->channel_count == best->channel_count &&
				 mode->max_samplerate < best->max_samplerate))
			best = mode;
	}

	return best;
}

const struct dslogic_channel_mode *dslogic_profile_select_mode(
		const struct dslogic_profile *profile, unsigned int enabled_channels,
		uint64_t samplerate)
{
	const struct dslogic_channel_mode *mode;

	mode = dslogic_profile_select_mode_for_stream(profile, enabled_channels,
		samplerate, FALSE);
	if (mode)
		return mode;

	return dslogic_profile_select_mode_for_stream(profile, enabled_channels,
		samplerate, TRUE);
}

GArray *dslogic_profile_samplerates_for_stream(
		const struct dslogic_profile *profile, unsigned int enabled_channels,
		gboolean stream)
{
	GArray *samplerates;
	size_t index;

	samplerates = g_array_new(FALSE, FALSE, sizeof(uint64_t));
	if (!samplerates)
		return NULL;
	for (index = 0; index < G_N_ELEMENTS(dsview_logic_samplerates); index++) {
		const uint64_t samplerate = dsview_logic_samplerates[index];

		if (!dslogic_profile_select_mode_for_stream(profile,
				enabled_channels, samplerate, stream))
			continue;
		g_array_append_val(samplerates, samplerate);
	}

	return samplerates;
}

gboolean dslogic_profile_samplerate_is_available(
		const struct dslogic_profile *profile, unsigned int enabled_channels,
		uint64_t samplerate, gboolean stream)
{
	GArray *samplerates;
	gboolean available = FALSE;
	guint index;

	samplerates = dslogic_profile_samplerates_for_stream(profile,
		enabled_channels, stream);
	if (!samplerates)
		return FALSE;
	for (index = 0; index < samplerates->len; index++) {
		if (g_array_index(samplerates, uint64_t, index) == samplerate) {
			available = TRUE;
			break;
		}
	}
	g_array_unref(samplerates);

	return available;
}

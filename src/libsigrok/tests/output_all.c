/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>
#include <glib/gstdio.h>
#include <zip.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "lib.h"

static char *srzip_test_filename(void)
{
	return g_strdup_printf("%s%csigrok-srzip-test-%u.sr", g_get_tmp_dir(),
		G_DIR_SEPARATOR, g_random_int());
}

static struct sr_dev_inst *srzip_test_device_new(unsigned int channel_count,
		unsigned int enabled_channel)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *channel;
	char name[16];
	unsigned int index;

	sdi = sr_dev_inst_user_new("sigrok", "srzip-test", NULL);
	ck_assert_msg(sdi != NULL, "Failed to create test device.");
	for (index = 0; index < channel_count; index++) {
		g_snprintf(name, sizeof(name), "D%u", index);
		ck_assert_int_eq(sr_dev_inst_channel_add(sdi, index,
			SR_CHANNEL_LOGIC, name), SR_OK);
		channel = g_slist_nth_data(sdi->channels, index);
		ck_assert_int_eq(sr_dev_channel_enable(channel,
			index == enabled_channel), SR_OK);
	}

	return sdi;
}

static void srzip_test_write(const char *filename, struct sr_dev_inst *sdi,
		const uint8_t *data, size_t length, unsigned int unitsize)
{
	const struct sr_output *output;
	struct sr_datafeed_logic logic = {
		.length = length,
		.unitsize = unitsize,
		.data = (uint8_t *)data,
	};
	struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic,
	};
	GString *out = NULL;

	output = sr_output_new(sr_output_find("srzip"), NULL, sdi, filename);
	ck_assert_msg(output != NULL, "Failed to create srzip output.");
	ck_assert_int_eq(sr_output_send(output, &packet, &out), SR_OK);
	if (out)
		g_string_free(out, TRUE);
	packet.type = SR_DF_END;
	packet.payload = NULL;
	ck_assert_int_eq(sr_output_send(output, &packet, &out), SR_OK);
	if (out)
		g_string_free(out, TRUE);
	ck_assert_int_eq(sr_output_free(output), SR_OK);
}

static uint8_t *srzip_test_read_entry(const char *filename,
		const char *entry_name, size_t *entry_length)
{
	struct zip *archive;
	struct zip_file *entry;
	struct zip_stat stat;
	uint8_t *data;
	int error = 0;
	zip_int64_t read_length;

	archive = zip_open(filename, ZIP_RDONLY, &error);
	ck_assert_msg(archive != NULL, "Failed to open srzip archive: %d.", error);
	zip_stat_init(&stat);
	ck_assert_int_eq(zip_stat(archive, entry_name, 0, &stat), 0);
	entry = zip_fopen(archive, entry_name, 0);
	ck_assert_msg(entry != NULL, "Failed to open srzip entry '%s'.", entry_name);
	data = g_malloc(stat.size + 1);
	read_length = zip_fread(entry, data, stat.size);
	ck_assert_int_eq(read_length, (zip_int64_t)stat.size);
	data[stat.size] = '\0';
	*entry_length = stat.size;
	zip_fclose(entry);
	ck_assert_int_eq(zip_close(archive), 0);

	return data;
}

static void srzip_test_assert_payload(unsigned int channel_count,
		unsigned int enabled_channel, unsigned int unitsize,
		const uint8_t *data, size_t length)
{
	struct sr_dev_inst *sdi;
	char *filename;
	uint8_t *metadata;
	uint8_t *payload;
	char unitsize_text[32];
	char probes_text[32];
	size_t metadata_length, payload_length;

	sdi = srzip_test_device_new(channel_count, enabled_channel);
	filename = srzip_test_filename();
	srzip_test_write(filename, sdi, data, length, unitsize);
	metadata = srzip_test_read_entry(filename, "metadata", &metadata_length);
	payload = srzip_test_read_entry(filename, "logic-1-1", &payload_length);
	g_snprintf(unitsize_text, sizeof(unitsize_text), "unitsize=%u", unitsize);
	g_snprintf(probes_text, sizeof(probes_text), "total probes=%u", channel_count);
	ck_assert_msg(metadata_length > 0, "srzip metadata is empty.");
	ck_assert_msg(strstr((char *)metadata, unitsize_text) != NULL,
		"Missing expected metadata %s.", unitsize_text);
	ck_assert_msg(strstr((char *)metadata, probes_text) != NULL,
		"Missing expected metadata %s.", probes_text);
	ck_assert_int_eq(payload_length, length);
	ck_assert_mem_eq(payload, data, length);

	g_free(payload);
	g_free(metadata);
	g_remove(filename);
	g_free(filename);
	sr_dev_inst_free(sdi);
}

struct srzip_replay_result {
	uint8_t data[16];
	size_t length;
	uint16_t unitsize;
	gboolean saw_end;
};

static void srzip_test_replay_callback(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data)
{
	struct srzip_replay_result *result = cb_data;
	const struct sr_datafeed_logic *logic;

	(void)sdi;
	if (packet->type == SR_DF_LOGIC) {
		logic = packet->payload;
		ck_assert_msg(result->length + logic->length <= sizeof(result->data),
			"Replay data exceeds test buffer.");
		memcpy(result->data + result->length, logic->data, logic->length);
		result->length += logic->length;
		result->unitsize = logic->unitsize;
	} else if (packet->type == SR_DF_END) {
		result->saw_end = TRUE;
	}
}

START_TEST(test_srzip_preserves_one_byte_selected_channel)
{
	const uint8_t data[] = { 0x00, 0x01, 0x7f, 0xff };

	srzip_test_assert_payload(16, 0, 1, data, sizeof(data));
}
END_TEST

START_TEST(test_srzip_preserves_wide_logic_samples)
{
	const uint8_t two_byte_data[] = { 0x01, 0x80, 0x02, 0x40 };
	const uint8_t four_byte_data[] = {
		0x01, 0x02, 0x04, 0x80, 0x08, 0x10, 0x20, 0x40,
	};

	srzip_test_assert_payload(16, 15, 2, two_byte_data, sizeof(two_byte_data));
	srzip_test_assert_payload(32, 31, 4, four_byte_data, sizeof(four_byte_data));
}
END_TEST

START_TEST(test_srzip_rejects_changed_logic_unitsize)
{
	const uint8_t one_byte_data[] = { 0x01, 0x02 };
	const uint8_t two_byte_data[] = { 0x01, 0x00, 0x02, 0x00 };
	struct sr_dev_inst *sdi;
	const struct sr_output *output;
	struct sr_datafeed_logic logic = {
		.length = sizeof(one_byte_data),
		.unitsize = 1,
		.data = (uint8_t *)one_byte_data,
	};
	struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic,
	};
	GString *out = NULL;
	char *filename;

	sdi = srzip_test_device_new(16, 0);
	filename = srzip_test_filename();
	output = sr_output_new(sr_output_find("srzip"), NULL, sdi, filename);
	ck_assert_msg(output != NULL, "Failed to create srzip output.");
	ck_assert_int_eq(sr_output_send(output, &packet, &out), SR_OK);
	if (out)
		g_string_free(out, TRUE);
	logic.length = sizeof(two_byte_data);
	logic.unitsize = 2;
	logic.data = (uint8_t *)two_byte_data;
	ck_assert_int_ne(sr_output_send(output, &packet, &out), SR_OK);
	if (out)
		g_string_free(out, TRUE);
	ck_assert_int_eq(sr_output_free(output), SR_OK);

	g_remove(filename);
	g_free(filename);
	sr_dev_inst_free(sdi);
}
END_TEST

START_TEST(test_srzip_replays_legacy_two_byte_session)
{
	const uint8_t data[] = { 0x01, 0x80, 0x02, 0x40 };
	struct sr_dev_inst *sdi;
	struct sr_session *session;
	struct srzip_replay_result result = { 0 };
	char *filename;

	sdi = srzip_test_device_new(16, 15);
	filename = srzip_test_filename();
	srzip_test_write(filename, sdi, data, sizeof(data), 2);
	sr_dev_inst_free(sdi);

	ck_assert_int_eq(sr_session_load(srtest_ctx, filename, &session), SR_OK);
	ck_assert_int_eq(sr_session_datafeed_callback_add(session,
		srzip_test_replay_callback, &result), SR_OK);
	ck_assert_int_eq(sr_session_start(session), SR_OK);
	ck_assert_int_eq(sr_session_run(session), SR_OK);
	ck_assert_msg(result.saw_end, "Session replay did not emit SR_DF_END.");
	ck_assert_int_eq(result.unitsize, 2);
	ck_assert_int_eq(result.length, sizeof(data));
	ck_assert_mem_eq(result.data, data, sizeof(data));
	ck_assert_int_eq(sr_session_destroy(session), SR_OK);

	g_remove(filename);
	g_free(filename);
}
END_TEST

/* Check whether at least one output module is available. */
START_TEST(test_output_available)
{
	const struct sr_output_module **outputs;

	outputs = sr_output_list();
	ck_assert_msg(outputs != NULL, "No output modules found.");
}
END_TEST

/* Check whether sr_output_id_get() works. */
START_TEST(test_output_id)
{
	const struct sr_output_module **outputs;
	const char *id;

	outputs = sr_output_list();

	id = sr_output_id_get(outputs[0]);
	ck_assert_msg(id != NULL, "No id found in output module.");
}
END_TEST

/* Check whether sr_output_name_get() works. */
START_TEST(test_output_name)
{
	const struct sr_output_module **outputs;
	const char *name;

	outputs = sr_output_list();

	name = sr_output_name_get(outputs[0]);
	ck_assert_msg(name != NULL, "No name found in output module.");
}
END_TEST

/* Check whether sr_output_description_get() works. */
START_TEST(test_output_desc)
{
	const struct sr_output_module **outputs;
	const char *desc;

	outputs = sr_output_list();

	desc = sr_output_description_get(outputs[0]);
	ck_assert_msg(desc != NULL, "No description found in output module.");
}
END_TEST

/* Check whether sr_output_find() works. */
START_TEST(test_output_find)
{
	const struct sr_output_module *omod;
	const char *id;

	omod = sr_output_find("bits");
	ck_assert_msg(omod != NULL, "Couldn't find the 'bits' output module.");
	id = sr_output_id_get(omod);
	ck_assert_msg(!strcmp(id, "bits"), "That is not the 'bits' module!");
}
END_TEST

/* Check whether sr_output_options_get() works. */
START_TEST(test_output_options)
{
	const struct sr_option **opt;

	opt = sr_output_options_get(sr_output_find("bits"));
	ck_assert_msg(opt != NULL, "Couldn't find 'bits' options.");
	ck_assert_msg(!strcmp((*opt)->id, "width"),
		      "Wrong 'bits' option found!");
}
END_TEST

Suite *suite_output_all(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("output-all");

	tc = tcase_create("basic");
	tcase_add_test(tc, test_output_available);
	tcase_add_test(tc, test_output_id);
	tcase_add_test(tc, test_output_name);
	tcase_add_test(tc, test_output_desc);
	tcase_add_test(tc, test_output_find);
	tcase_add_test(tc, test_output_options);
	tcase_add_test(tc, test_srzip_preserves_one_byte_selected_channel);
	tcase_add_test(tc, test_srzip_preserves_wide_logic_samples);
	tcase_add_test(tc, test_srzip_rejects_changed_logic_unitsize);
	suite_add_tcase(s, tc);

	tc = tcase_create("srzip-replay");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_srzip_replays_legacy_two_byte_session);
	suite_add_tcase(s, tc);

	return s;
}

/*
 * This file is part of the sigrok-cli project.
 *
 * Copyright (C) 2026 Probes Project
 */

#include <config.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
#include <sys/file.h>
#include <unistd.h>
#endif
#include "artifacts.h"

struct artifact_context {
	enum artifact_output_mode mode;
	gchar *operation;
	gchar *operation_dir;
	gchar *capture_path;
	gchar *capture_final_path;
	gchar *text_path;
	gchar *text_final_path;
	gchar *binary_path;
	gchar *binary_final_path;
	gchar *started_at;
	gchar *finished_at;
	FILE *text_stream;
	FILE *binary_stream;
#ifdef G_OS_WIN32
	HANDLE output_lock;
#else
	gchar *output_lock_path;
	int output_lock_fd;
#endif
	gboolean output_lock_acquired;
	uint64_t samples;
	uint64_t frames;
	uint64_t triggered;
	uint64_t text_records;
	uint64_t text_bytes;
	uint64_t binary_records;
	uint64_t binary_bytes;
	gboolean committed;
};

static struct artifact_context *current_context;

static gchar *json_escape(const char *value)
{
	GString *escaped;
	const unsigned char *p;

	escaped = g_string_new("\"");
	for (p = (const unsigned char *)value; p && *p; p++) {
		switch (*p) {
		case '\\': g_string_append(escaped, "\\\\"); break;
		case '"': g_string_append(escaped, "\\\""); break;
		case '\n': g_string_append(escaped, "\\n"); break;
		case '\r': g_string_append(escaped, "\\r"); break;
		case '\t': g_string_append(escaped, "\\t"); break;
		default:
			if (*p < 0x20)
				g_string_append_printf(escaped, "\\u%04x", *p);
			else
				g_string_append_c(escaped, *p);
		}
	}
	g_string_append_c(escaped, '"');
	return g_string_free(escaped, FALSE);
}

static gchar *make_operation_id(void)
{
	GDateTime *now;
	gchar *stamp;
	gchar *id;

	now = g_date_time_new_now_utc();
	stamp = g_date_time_format(now, "%Y%m%dT%H%M%SZ");
	id = g_strdup_printf("%s-%u-%08x", stamp, (unsigned int)getpid(),
		g_random_int());
	g_free(stamp);
	g_date_time_unref(now);
	return id;
}

static gchar *default_artifact_root(void)
{
	return g_build_filename(g_get_user_data_dir(), "Probes", "sigrok",
		"artifacts", NULL);
}

static int ensure_directory(const char *path)
{
	if (g_file_test(path, G_FILE_TEST_EXISTS)) {
		if (!g_file_test(path, G_FILE_TEST_IS_DIR))
			return -1;
		return 0;
	}

	return g_mkdir_with_parents(path, 0700);
}

static void close_stream(FILE **stream)
{
	if (*stream) {
		fclose(*stream);
		*stream = NULL;
	}
}

static void remove_temporary(const char *path)
{
	if (path)
		g_unlink(path);
}

static void free_context(struct artifact_context *ctx)
{
	if (!ctx)
		return;
	close_stream(&ctx->text_stream);
	close_stream(&ctx->binary_stream);
#ifdef G_OS_WIN32
	if (ctx->output_lock) {
		if (ctx->output_lock_acquired)
			ReleaseMutex(ctx->output_lock);
		CloseHandle(ctx->output_lock);
	}
#else
	if (ctx->output_lock_fd >= 0) {
		if (ctx->output_lock_acquired)
			flock(ctx->output_lock_fd, LOCK_UN);
		close(ctx->output_lock_fd);
	}
	g_free(ctx->output_lock_path);
#endif
	g_free(ctx->operation);
	g_free(ctx->operation_dir);
	g_free(ctx->capture_path);
	g_free(ctx->capture_final_path);
	g_free(ctx->text_path);
	g_free(ctx->text_final_path);
	g_free(ctx->binary_path);
	g_free(ctx->binary_final_path);
	g_free(ctx->started_at);
	g_free(ctx->finished_at);
	g_free(ctx);
}

static int acquire_output_lock(struct artifact_context *ctx)
{
	gchar *checksum = g_compute_checksum_for_string(G_CHECKSUM_SHA256,
		ctx->operation_dir, -1);
#ifdef G_OS_WIN32
	gchar *name = g_strdup_printf("Local\\ProbesSigrokArtifact-%s", checksum);
	gunichar2 *name_w = g_utf8_to_utf16(name, -1, NULL, NULL, NULL);
	DWORD result;

	if (name_w)
		ctx->output_lock = CreateMutexW(NULL, FALSE, (LPCWSTR)name_w);
	g_free(name_w);
	g_free(name);
	if (!ctx->output_lock) {
		g_free(checksum);
		return -1;
	}
	result = WaitForSingleObject(ctx->output_lock, 0);
	g_free(checksum);
	if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
		return -1;
#else
	ctx->output_lock_path = g_strdup_printf("%s%cprobes-sigrok-artifact-%s.lock",
		g_get_tmp_dir(), G_DIR_SEPARATOR, checksum);
	g_free(checksum);
	ctx->output_lock_fd = g_open(ctx->output_lock_path,
		O_CREAT | O_RDWR, 0600);
	if (ctx->output_lock_fd < 0 ||
			flock(ctx->output_lock_fd, LOCK_EX | LOCK_NB) != 0)
		return -1;
#endif
	ctx->output_lock_acquired = TRUE;
	return 0;
}

int artifact_context_begin(enum artifact_output_mode mode,
		const char *output_dir, const char *operation,
		struct artifact_context **ctx_out)
{
	struct artifact_context *ctx;
	gchar *root;
	gchar *operation_id;

	if (!ctx_out)
		return -1;
	*ctx_out = NULL;
	if (mode == ARTIFACT_OUTPUT_STDOUT)
		return 0;

	ctx = g_new0(struct artifact_context, 1);
#ifndef G_OS_WIN32
	ctx->output_lock_fd = -1;
#endif
	ctx->mode = mode;
	ctx->operation = g_strdup(operation ? operation : "data");
	operation_id = make_operation_id();
	{
		GDateTime *now = g_date_time_new_now_utc();
		ctx->started_at = g_date_time_format_iso8601(now);
		g_date_time_unref(now);
	}
	if (output_dir && output_dir[0]) {
		ctx->operation_dir = g_canonicalize_filename(output_dir, NULL);
	} else {
		root = default_artifact_root();
		ctx->operation_dir = g_build_filename(root, operation_id, NULL);
		g_free(root);
	}
	g_free(operation_id);

	if (ensure_directory(ctx->operation_dir) != 0) {
		g_critical("Artifact directory cannot be created or is not a directory: %s",
			ctx->operation_dir);
		free_context(ctx);
		return -1;
	}
	if (acquire_output_lock(ctx) != 0) {
		g_critical("Artifact directory is already in use: %s",
			ctx->operation_dir);
		free_context(ctx);
		return -1;
	}

	operation_id = make_operation_id();
	ctx->capture_path = g_strdup_printf("%s%c.capture-%s.sr.part",
		ctx->operation_dir, G_DIR_SEPARATOR, operation_id);
	ctx->capture_final_path = g_build_filename(ctx->operation_dir,
		"capture.sr", NULL);
	ctx->text_path = g_strdup_printf("%s%c.decode-text-%s.part",
		ctx->operation_dir, G_DIR_SEPARATOR, operation_id);
	ctx->text_final_path = g_build_filename(ctx->operation_dir,
		"decode.txt", NULL);
	ctx->binary_path = g_strdup_printf("%s%c.decode-binary-%s.part",
		ctx->operation_dir, G_DIR_SEPARATOR, operation_id);
	ctx->binary_final_path = g_build_filename(ctx->operation_dir,
		"decode.bin", NULL);
	g_free(operation_id);
	current_context = ctx;
	*ctx_out = ctx;
	return 0;
}

struct artifact_context *artifact_context_current(void)
{
	return current_context;
}

gboolean artifact_is_file_mode(void)
{
	return current_context && current_context->mode == ARTIFACT_OUTPUT_FILE;
}

const char *artifact_operation_dir(const struct artifact_context *ctx)
{
	return ctx ? ctx->operation_dir : NULL;
}

const char *artifact_capture_path(const struct artifact_context *ctx)
{
	return ctx ? ctx->capture_path : NULL;
}

const char *artifact_capture_final_path(const struct artifact_context *ctx)
{
	return ctx ? ctx->capture_final_path : NULL;
}

FILE *artifact_decoder_text_stream(struct artifact_context *ctx)
{
	if (!ctx)
		return stdout;
	if (ctx->mode == ARTIFACT_OUTPUT_STDOUT)
		return stdout;
	if (!ctx->text_stream)
		ctx->text_stream = g_fopen(ctx->text_path, "wb");
	return ctx->text_stream;
}

FILE *artifact_decoder_binary_stream(struct artifact_context *ctx)
{
	if (!ctx)
		return stdout;
	if (ctx->mode == ARTIFACT_OUTPUT_STDOUT)
		return stdout;
	if (!ctx->binary_stream)
		ctx->binary_stream = g_fopen(ctx->binary_path, "wb");
	return ctx->binary_stream;
}

void artifact_note_samples(struct artifact_context *ctx, uint64_t samples,
		uint64_t frames, uint64_t triggered)
{
	if (!ctx)
		return;
	ctx->samples += samples;
	ctx->frames += frames;
	ctx->triggered += triggered;
}

void artifact_note_decoder_text(struct artifact_context *ctx, size_t bytes)
{
	if (ctx) {
		ctx->text_records++;
		if (bytes)
			ctx->text_bytes += bytes;
		else if (ctx->text_stream) {
			long position;
			fflush(ctx->text_stream);
			position = ftell(ctx->text_stream);
			if (position >= 0)
				ctx->text_bytes = (uint64_t)position;
		}
	}
}

void artifact_note_decoder_binary(struct artifact_context *ctx, size_t bytes)
{
	if (ctx) {
		ctx->binary_records++;
		ctx->binary_bytes += bytes;
	}
}

static int replace_file(const char *temporary, const char *final)
{
#ifdef G_OS_WIN32
	gunichar2 *temporary_w = g_utf8_to_utf16(temporary, -1, NULL, NULL, NULL);
	gunichar2 *final_w = g_utf8_to_utf16(final, -1, NULL, NULL, NULL);
	gboolean replaced = temporary_w && final_w &&
		MoveFileExW((LPCWSTR)temporary_w, (LPCWSTR)final_w,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	g_free(temporary_w);
	g_free(final_w);
	if (!replaced)
		return -1;
#else
	if (g_rename(temporary, final) != 0)
		return -1;
#endif
	return 0;
}

static int write_result(struct artifact_context *ctx, gboolean success, gboolean publish_stdout)
{
	gchar *operation = json_escape(ctx->operation);
	gchar *directory = json_escape(ctx->operation_dir);
	gchar *format;
	gchar *started_at = json_escape(ctx->started_at ? ctx->started_at : "");
	gchar *finished_at;
	gchar *capture_path = NULL;
	gchar *text_path = NULL;
	gchar *binary_path = NULL;
	GDateTime *now;
	gchar *result_id = make_operation_id();
	gchar *result_path = g_strdup_printf("%s%c.result-%s.part",
		ctx->operation_dir, G_DIR_SEPARATOR, result_id);
	gchar *result_final = g_build_filename(ctx->operation_dir, "result.json", NULL);
	GString *json;
	FILE *result;
	int ret = -1;

	g_free(result_id);

	now = g_date_time_new_now_utc();
	g_free(ctx->finished_at);
	ctx->finished_at = g_date_time_format_iso8601(now);
	g_date_time_unref(now);
	finished_at = json_escape(ctx->finished_at);
	format = json_escape(g_strcmp0(ctx->operation, "capture") == 0 ||
		g_strcmp0(ctx->operation, "capture-decode") == 0 ? "srzip" :
		g_file_test(ctx->binary_final_path, G_FILE_TEST_EXISTS) ? "binary" : "text");

	json = g_string_new(NULL);
	g_string_append_printf(json,
		"{\"status\":\"%s\",\"operation\":%s,\"operationDir\":%s,"
		"\"artifacts\":[",
		success ? "ok" : "error", operation, directory);
	if (g_file_test(ctx->capture_final_path, G_FILE_TEST_EXISTS)) {
		capture_path = json_escape(ctx->capture_final_path);
		g_string_append_printf(json, "{\"kind\":\"capture\",\"path\":%s},", capture_path);
	}
	if (g_file_test(ctx->text_final_path, G_FILE_TEST_EXISTS)) {
		text_path = json_escape(ctx->text_final_path);
		g_string_append_printf(json, "{\"kind\":\"decode\",\"path\":%s},", text_path);
	}
	if (g_file_test(ctx->binary_final_path, G_FILE_TEST_EXISTS)) {
		binary_path = json_escape(ctx->binary_final_path);
		g_string_append_printf(json, "{\"kind\":\"decode-binary\",\"path\":%s},", binary_path);
	}
	if (json->len && json->str[json->len - 1] == ',')
		g_string_truncate(json, json->len - 1);
	g_string_append_printf(json,
		"],\"format\":%s,\"startedAt\":%s,\"finishedAt\":%s,"
		"\"summary\":{\"samples\":%" G_GUINT64_FORMAT
		",\"frames\":%" G_GUINT64_FORMAT ",\"triggered\":%" G_GUINT64_FORMAT
		",\"annotations\":%" G_GUINT64_FORMAT ",\"annotationBytes\":%" G_GUINT64_FORMAT
		",\"binaryRecords\":%" G_GUINT64_FORMAT ",\"binaryBytes\":%" G_GUINT64_FORMAT "}}\n",
		format, started_at, finished_at, ctx->samples, ctx->frames, ctx->triggered, ctx->text_records,
		ctx->text_bytes, ctx->binary_records, ctx->binary_bytes);
	result = g_fopen(result_path, "wb");
	if (result) {
		if (fwrite(json->str, 1, json->len, result) == json->len &&
			fclose(result) == 0) {
			result = NULL;
			if (replace_file(result_path, result_final) == 0) {
			if (publish_stdout) {
				fputs(json->str, stdout);
				fflush(stdout);
			}
			ret = 0;
			}
		} else if (result) {
			fclose(result);
		}
	}
	if (ret != 0)
		remove_temporary(result_path);
	g_free(operation);
	g_free(directory);
	g_free(format);
	g_free(started_at);
	g_free(finished_at);
	g_free(capture_path);
	g_free(text_path);
	g_free(binary_path);
	g_free(result_path);
	g_free(result_final);
	g_string_free(json, TRUE);
	return ret;
}

void artifact_context_abort(struct artifact_context *ctx)
{
	if (!ctx)
		return;
	close_stream(&ctx->text_stream);
	close_stream(&ctx->binary_stream);
	remove_temporary(ctx->capture_path);
	remove_temporary(ctx->text_path);
	remove_temporary(ctx->binary_path);
	if (current_context == ctx)
		current_context = NULL;
	free_context(ctx);
}

static int finish_context(struct artifact_context *ctx, gboolean success, gboolean publish_stdout)
{
	int ret = 0;

	if (!ctx || ctx->mode == ARTIFACT_OUTPUT_STDOUT)
		return 0;
	close_stream(&ctx->text_stream);
	close_stream(&ctx->binary_stream);
	if (success) {
		if ((g_file_test(ctx->capture_path, G_FILE_TEST_EXISTS) &&
				replace_file(ctx->capture_path, ctx->capture_final_path) != 0) ||
			(g_file_test(ctx->text_path, G_FILE_TEST_EXISTS) &&
				replace_file(ctx->text_path, ctx->text_final_path) != 0) ||
			(g_file_test(ctx->binary_path, G_FILE_TEST_EXISTS) &&
				replace_file(ctx->binary_path, ctx->binary_final_path) != 0))
			ret = -1;
	}
	if (ret != 0 || !success) {
		remove_temporary(ctx->capture_path);
		remove_temporary(ctx->text_path);
		remove_temporary(ctx->binary_path);
	}
	if (write_result(ctx, success && ret == 0, publish_stdout) != 0)
		ret = -1;
	if (current_context == ctx)
		current_context = NULL;
	ctx->committed = success && ret == 0;
	free_context(ctx);
	return ret;
}

int artifact_context_finish(struct artifact_context *ctx, gboolean success)
{
	return finish_context(ctx, success, TRUE);
}

int artifact_context_finish_silent(struct artifact_context *ctx, gboolean success)
{
	return finish_context(ctx, success, FALSE);
}

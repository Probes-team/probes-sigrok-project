/*
 * This file is part of the sigrok-cli project.
 *
 * Copyright (C) 2026 Probes Project
 */

#ifndef SIGROK_CLI_ARTIFACTS_H
#define SIGROK_CLI_ARTIFACTS_H

#include <stdio.h>
#include <stdint.h>
#include <glib.h>

enum artifact_output_mode {
	ARTIFACT_OUTPUT_FILE,
	ARTIFACT_OUTPUT_STDOUT,
};

struct artifact_context;

int artifact_context_begin(enum artifact_output_mode mode,
		const char *output_dir, const char *operation,
		struct artifact_context **ctx);
void artifact_context_abort(struct artifact_context *ctx);
int artifact_context_finish(struct artifact_context *ctx, gboolean success);

struct artifact_context *artifact_context_current(void);
gboolean artifact_is_file_mode(void);
const char *artifact_operation_dir(const struct artifact_context *ctx);
const char *artifact_capture_path(const struct artifact_context *ctx);

FILE *artifact_decoder_text_stream(struct artifact_context *ctx);
FILE *artifact_decoder_binary_stream(struct artifact_context *ctx);
void artifact_note_samples(struct artifact_context *ctx, uint64_t samples,
		uint64_t frames, uint64_t triggered);
void artifact_note_decoder_text(struct artifact_context *ctx, size_t bytes);
void artifact_note_decoder_binary(struct artifact_context *ctx, size_t bytes);

#endif

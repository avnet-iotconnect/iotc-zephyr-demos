/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * TFLite-Micro person-detection wrapper (C API over the C++ runtime).
 *
 * The model is DATA: vision_infer_init() builds a fresh interpreter over any
 * int8 96x96x1 person-detection .tflite flatbuffer (MobileNet-style, the ops
 * registered in vision_infer.cpp). Re-initializing with a new blob hot-swaps
 * the model; on failure the caller re-inits with the previous blob to roll
 * back. Calls are NOT thread-safe -- the app serializes them with its model
 * lock.
 */
#ifndef VISION_INFER_H
#define VISION_INFER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * (Re)build the interpreter over model_data (a .tflite flatbuffer that must
 * stay resident -- TFLM references it in place). Verifies schema version,
 * arena fit, and a 96x96x1 int8 input / 2-class int8 output. Returns 0 on
 * success; on error returns a negative value and writes a short reason into
 * err (if non-NULL).
 */
int vision_infer_init(const uint8_t *model_data, size_t model_len,
		      char *err, size_t err_len);

/*
 * Run person detection on a 96x96 grayscale image (VISION_INPUT_LEN bytes,
 * row-major, 0..255).
 *
 * On success writes the person score in percent (0..100) to *person_pct,
 * the no-person score to *clear_pct, and the wall time of the invoke to
 * *infer_ms; returns 0. Returns a negative value if no model is loaded or
 * the invoke fails.
 */
int vision_infer_run(const uint8_t *gray96, int *person_pct, int *clear_pct,
		     uint32_t *infer_ms);

/* Tensor-arena bytes actually used by the current model (0 if none). */
size_t vision_infer_arena_used(void);

#ifdef __cplusplus
}
#endif

#endif /* VISION_INFER_H */

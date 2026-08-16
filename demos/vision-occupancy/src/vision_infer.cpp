/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 */
#include <new>
#include <cstdio>
#include <cstring>

#include <zephyr/kernel.h>

#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>

#include "vision_camera.h"
#include "vision_infer.h"

/*
 * Tensor arena for the person-detect MobileNet (int8, 96x96x1): the stock
 * model needs ~100 KB; leave headroom for cloud-pushed variants. Lives in
 * SDRAM (zephyr,sram = &sdram0 on the RT1170-EVK), so size is not precious.
 */
constexpr size_t kArenaSize = 192 * 1024;
static uint8_t tensor_arena[kArenaSize] __aligned(16);

/* Ops used by the person-detection MobileNet (visualize a candidate model
 * with netron.app to check it fits this set before pushing it). */
using VisionOpResolver = tflite::MicroMutableOpResolver<6>;

static VisionOpResolver op_resolver;
static bool ops_registered;

/* Interpreter storage for placement new/delete across model hot-swaps. */
alignas(tflite::MicroInterpreter) static uint8_t
	interp_mem[sizeof(tflite::MicroInterpreter)];
static tflite::MicroInterpreter *interp;

static void fail(char *err, size_t err_len, const char *msg)
{
	if (err != nullptr && err_len > 0) {
		std::snprintf(err, err_len, "%s", msg);
	}
}

extern "C" int vision_infer_init(const uint8_t *model_data, size_t model_len,
				 char *err, size_t err_len)
{
	ARG_UNUSED(model_len);

	const tflite::Model *model = tflite::GetModel(model_data);

	if (model->version() != TFLITE_SCHEMA_VERSION) {
		fail(err, err_len, "flatbuffer schema version mismatch");
		return -1;
	}

	if (!ops_registered) {
		op_resolver.AddConv2D();
		op_resolver.AddDepthwiseConv2D();
		op_resolver.AddAveragePool2D();
		op_resolver.AddFullyConnected();
		op_resolver.AddReshape();
		op_resolver.AddSoftmax();
		ops_registered = true;
	}

	if (interp != nullptr) {
		interp->~MicroInterpreter();
		interp = nullptr;
	}

	tflite::MicroInterpreter *ni = new (interp_mem) tflite::MicroInterpreter(
		model, op_resolver, tensor_arena, kArenaSize);

	if (ni->AllocateTensors() != kTfLiteOk) {
		ni->~MicroInterpreter();
		fail(err, err_len, "AllocateTensors failed (op set/arena)");
		return -1;
	}

	TfLiteTensor *in = ni->input(0);
	TfLiteTensor *out = ni->output(0);

	if (in == nullptr || out == nullptr ||
	    in->type != kTfLiteInt8 || out->type != kTfLiteInt8 ||
	    in->dims->size != 4 ||
	    in->dims->data[1] != VISION_INPUT_H ||
	    in->dims->data[2] != VISION_INPUT_W ||
	    in->dims->data[3] != 1 ||
	    out->dims->data[out->dims->size - 1] != 2) {
		ni->~MicroInterpreter();
		fail(err, err_len, "not an int8 96x96x1 2-class model");
		return -1;
	}

	interp = ni;
	return 0;
}

extern "C" int vision_infer_run(const uint8_t *gray96, int *person_pct,
				int *clear_pct, uint32_t *infer_ms)
{
	if (interp == nullptr) {
		return -1;
	}

	TfLiteTensor *in = interp->input(0);
	const float in_scale = in->params.scale;
	const int in_zp = in->params.zero_point;

	/* Quantize 0..255 luma into the input tensor. For the stock model
	 * (scale 1/255, zero point -128) this reduces to pixel - 128. */
	for (size_t i = 0; i < VISION_INPUT_LEN; i++) {
		int q = (int)((float)gray96[i] / 255.0f / in_scale + 0.5f) + in_zp;

		in->data.int8[i] = (int8_t)CLAMP(q, -128, 127);
	}

	int64_t t0 = k_uptime_get();

	if (interp->Invoke() != kTfLiteOk) {
		return -2;
	}
	if (infer_ms != nullptr) {
		*infer_ms = (uint32_t)(k_uptime_get() - t0);
	}

	/* Output: softmaxed int8 [no-person, person] (indices per the TFLM
	 * person_detection example's model_settings). */
	TfLiteTensor *out = interp->output(0);
	const float out_scale = out->params.scale;
	const int out_zp = out->params.zero_point;

	if (person_pct != nullptr) {
		*person_pct = (int)((out->data.int8[1] - out_zp) * out_scale * 100.0f);
	}
	if (clear_pct != nullptr) {
		*clear_pct = (int)((out->data.int8[0] - out_zp) * out_scale * 100.0f);
	}
	return 0;
}

extern "C" size_t vision_infer_arena_used(void)
{
	return (interp != nullptr) ? interp->arena_used_bytes() : 0;
}

/*
 * SPDX-License-Identifier: MIT
 *
 * C-callable shim over the reused NXP MODEL_* interpreter wrapper. All the
 * TensorFlow Lite Micro / fsl_common include surface lives here; main.c sees
 * only model_bench.h. See model_bench.h for the contract.
 */
#include "model_bench.h"

/* Reused from iotc-mcx-zephyr-demos/src/model (on the include path via CMake).
 * Pulls in fsl_common.h (status_t) and tensorflow/lite/c/common.h. */
#include "model.h"

extern "C" int bench_model_init(void)
{
	return (MODEL_Init() == kStatus_Success) ? 0 : -1;
}

extern "C" const char *bench_model_name(void)
{
	return MODEL_GetModelName();
}

extern "C" unsigned bench_model_arena_kb(void)
{
	size_t max_size = 0;
	size_t used = MODEL_GetArenaUsedBytes(&max_size);

	return static_cast<unsigned>(used / 1024);
}

/* Product of a tensor's dimensions (NHWC etc.), guarding zero-sized dims. */
static size_t num_elements(const tensor_dims_t *dims)
{
	size_t n = 1;

	for (uint32_t i = 0; i < dims->size; i++) {
		if (dims->data[i]) {
			n *= dims->data[i];
		}
	}
	return n;
}

extern "C" void bench_model_fill_input(unsigned seed)
{
	tensor_dims_t dims;
	tensor_type_t type;
	uint8_t *in = MODEL_GetInputTensorData(&dims, &type);
	size_t n = num_elements(&dims);

	/* Write uint8 values; MODEL_ConvertInput maps them into the graph's
	 * actual input type (int8 subtract-127, or float normalize). */
	for (size_t i = 0; i < n; i++) {
		in[i] = static_cast<uint8_t>((i * 31u + seed) & 0xFFu);
	}

	MODEL_ConvertInput(in, &dims, type);
}

extern "C" int bench_model_invoke(void)
{
	return (MODEL_RunInference() == kStatus_Success) ? 0 : -1;
}

extern "C" double bench_model_top1(int *index)
{
	tensor_dims_t dims;
	tensor_type_t type;
	uint8_t *out = MODEL_GetOutputTensorData(&dims, &type);
	size_t n = num_elements(&dims);
	int best = 0;
	double best_val = -1e30;

	for (size_t i = 0; i < n; i++) {
		double v;

		switch (type) {
		case kTensorType_INT8:
			v = static_cast<double>(reinterpret_cast<int8_t *>(out)[i]);
			break;
		case kTensorType_UINT8:
			v = static_cast<double>(out[i]);
			break;
		case kTensorType_FLOAT32:
			v = static_cast<double>(reinterpret_cast<float *>(out)[i]);
			break;
		default:
			v = 0.0;
			break;
		}

		if (v > best_val) {
			best_val = v;
			best = static_cast<int>(i);
		}
	}

	if (index) {
		*index = best;
	}
	return best_val;
}

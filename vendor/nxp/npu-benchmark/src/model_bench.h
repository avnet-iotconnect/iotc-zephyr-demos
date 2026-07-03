/*
 * SPDX-License-Identifier: MIT
 *
 * Tiny C-callable shim over the NXP C++ MODEL_* API (iotc-mcx-zephyr-demos/
 * src/model/model.h). It keeps the TensorFlow Lite Micro / fsl_common headers
 * confined to one .cpp translation unit so main.c stays pure C and the whole
 * inference interface reduces to: init -> fill input -> invoke -> read top-1.
 *
 * The same shim drives both builds; which model actually runs (Neutron NPU vs
 * Cortex-M33) is decided at link time by CONFIG_MCUX_HAS_NPU, not here.
 */
#ifndef MODEL_BENCH_H
#define MODEL_BENCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the interpreter and allocate the tensor arena. 0 on success. */
int bench_model_init(void);

/* Human-readable model name baked into the model data (for telemetry). */
const char *bench_model_name(void);

/* Tensor arena bytes actually used, rounded to KB (logged at startup). */
unsigned bench_model_arena_kb(void);

/*
 * Fill the input tensor with a deterministic pattern derived from `seed`, then
 * run the model's input conversion (uint8 -> int8/float as the graph expects).
 * Content is irrelevant to timing -- the same ops execute regardless -- but
 * varying `seed` per cycle keeps the cloud readout looking live.
 */
void bench_model_fill_input(unsigned seed);

/* Run one inference (interpreter Invoke). 0 on success. This is the call the
 * benchmark times. */
int bench_model_invoke(void);

/*
 * Argmax over the output tensor. Returns the winning element's raw value and
 * writes its index to *index. Generic readout across int8/uint8/float outputs;
 * for a classifier this is the predicted class index.
 */
double bench_model_top1(int *index);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_BENCH_H */

# /IOTCONNECT NPU Benchmark — Demo Flow and Internals

This document walks through the npu-benchmark demo: what it measures, the
sequence of events, and what the device is doing underneath. Build
mechanics — including sourcing the NXP eIQ artifacts — are in the
[README](README.md).

## Overview

The FRDM-MCXN947 carries an **eIQ Neutron NPU** alongside its Cortex-M33.
This demo makes the accelerator's value measurable rather than asserted: it
times quantized TFLite-Micro inference on-device — warm-up runs excluded,
per-inference cycle counts converted to microseconds — and streams the
statistics to IOTCONNECT, where the NPU build and the CPU build chart as two
series on one dashboard. The gap between the two series is the result.

One Kconfig option selects the path: the default build links the Neutron
graph and runtime and labels its telemetry `neutron-npu`; a one-flag variant
(`-DEXTRA_CONF_FILE=cpu.conf`) links reference CPU kernels and labels itself
`cpu-m33`. Flash the two builds — or two boards — and the dashboard does the
comparison.

## System components

| Component | Role |
|---|---|
| FRDM-MCXN947 | Cortex-M33 + eIQ Neutron NPU, Ethernet |
| TFLite-Micro (NXP MCUXpresso middleware) | inference runtime; prebuilt library plus, on the NPU path, the Neutron driver and firmware libraries |
| Quantized model (Neutron-converted for the NPU path) | the workload being timed |
| iotc-zephyr-sdk | connectivity; each benchmark cycle publishes one telemetry record |
| /IOTCONNECT | charts `accelerator=neutron-npu` vs `accelerator=cpu-m33` |

## Demo flow

### Step 1 — Boot and model load

```
<inf> IOTCONNECT NPU benchmark starting (app v1.0.0, accelerator=neutron-npu)
<inf> Model 'cifarnet_quant_int8_npu' ready (92 KB arena used)
```

The TFLite-Micro interpreter initializes with a static tensor arena
(size a Kconfig knob, default 200 KB), reporting the bytes actually used.

### Step 2 — Benchmark cycles

Each cycle: fill the input tensor with a deterministic seeded pattern (the
input content is irrelevant to timing; the changing seed keeps the cloud
readout live), run two **untimed warm-up** inferences to exclude one-time
NPU firmware-load and cache effects, then time twenty inferences
individually with the cycle counter:

```
<inf> neutron-npu | cifarnet_quant_int8_npu | avg 0.412 ms (min 0.405 / max 0.431) | 2427 inf/s | top=3 | arena 92 KB
```

### Step 3 — Publish

After each cycle (every 5 s by default) one telemetry record goes up:

| Key | Meaning |
|---|---|
| `accelerator` | `neutron-npu` or `cpu-m33` — the dashboard series key |
| `model` | the baked-in model name |
| `inf_ms_avg` / `inf_ms_min` / `inf_ms_max` | per-inference latency statistics, ms |
| `inf_per_sec` | throughput derived from the average |
| `iterations` | timed inferences per cycle |
| `top_index` | argmax of the output tensor (a liveness check that inference ran) |
| `arena_kb` | tensor-arena memory actually used |
| `version` | application version |

A local mode (`CONFIG_APP_PUBLISH_TO_CLOUD=n`) runs the identical benchmark
loop with console output only — no network or credentials required, useful
for validating the eIQ artifacts before involving the cloud.

## Implementation notes

- **Timing method:** each inference is wrapped in raw cycle-counter reads
  (`k_cycle_get_32`), wrap-safe unsigned subtraction, floor-converted to µs;
  min/max/average accumulate per cycle. Warm-up count and iteration count
  are Kconfig knobs.
- **The eIQ artifacts are the prerequisite:** the prebuilt TFLM library,
  Neutron driver/firmware libraries, and the Neutron-converted model come
  from NXP's MCUXpresso middleware (fetched via west; pinned tag) — they are
  NXP-licensed and not vendored in this repository. Model glue is
  single-sourced from a sibling repository rather than copied.
- **Known seam — the two paths currently bake different models** (the NPU
  path a CIFAR-style int8 classifier, the CPU path a smaller detector). For
  a strict apples-to-apples number, both paths should be generated from the
  same source `.tflite`; the delivery pipeline and measurement are already
  identical.
- **C++ runtime:** TFLM forces full newlib/libc++ (the other demos use
  picolibc), hardware FPU, and restores float printf for telemetry
  serialization.
- **Arena placement:** the tensor arena defaults to `.bss`; a commented
  devicetree `zephyr,arena` chosen-node is provided to pin it to a specific
  SRAM bank if the NPU cannot reach the default region.
- **C2D handling** is a generic acknowledger (any command acks success);
  the benchmark knobs are compile-time.

## Troubleshooting quick reference

| Symptom | Meaning | Resolution |
|---|---|---|
| CMake `Model glue not found` | sibling repo path wrong | set `-DIOTC_MCX_DEMOS_DIR=` |
| link fails on `libtflm.a` / `libNeutron*.a` | eIQ middleware not fetched | fetch the pinned MCUXpresso TFLM middleware (README) |
| `Model init failed` | arena too small or artifacts mismatched | raise `CONFIG_APP_TENSOR_ARENA_SIZE_KB`; regenerate the converted model |
| `Inference failed at iteration N` (NPU build) | Neutron cannot reach the arena | pin the arena via the `zephyr,arena` chosen node |
| no cloud data | running the local mode or missing credentials | set `CONFIG_APP_PUBLISH_TO_CLOUD=y` and generate `device_credentials.h` |

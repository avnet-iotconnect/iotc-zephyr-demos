# NXP eIQ Neutron NPU benchmark → /IOTCONNECT

> **[DEMO.md](DEMO.md)** walks the demo end to end — each step's observable
> behavior and what the device and platform are doing underneath.

A vendor-specific demo that **proves NPU acceleration**: it runs the same
quantized model on the FRDM-MCXN947's eIQ **Neutron NPU** and on the
**Cortex-M33**, times each inference, and streams the numbers to Avnet
/IOTCONNECT. The cloud dashboard charts `neutron-npu` against `cpu-m33`, and the
gap between the two bars is the whole point.

```
  camera/static input ─► TFLM interpreter ─► [ Neutron NPU | Cortex-M33 ] ─► time it
                                                                              │
                          IOTCONNECT (MQTT/TLS over Ethernet) ◄── telemetry ──┘
```

This demo reuses two things you already have in the workspace:

- **Connectivity** — the IOTCONNECT Zephyr SDK (`iotc-zephyr-sdk`), same bearer
  bring-up as its `samples/telemetry`.
- **The model glue** — `iotc-mcx-zephyr-demos/src/model/` (NXP's TFLM wrapper +
  the NPU/CPU op resolvers). It is *single-sourced via CMake*, not copied.

## How the acceleration switch works

Everything pivots on one Kconfig, **`CONFIG_MCUX_HAS_NPU`**:

| Build | `CONFIG_MCUX_HAS_NPU` | Model code linked | Runs on | Label |
|-------|------------------------|-------------------|---------|-------|
| Default (`prj.conf`) | `y` | `model_npu/` + Neutron kernel + `libNeutron*.a` | Neutron NPU | `neutron-npu` |
| `-DEXTRA_CONF_FILE=cpu.conf` | `n` | `model_sw/` reference kernels | Cortex-M33 | `cpu-m33` |

Both paths call the identical `MODEL_RunInference()` (`interpreter->Invoke()`),
so the comparison is apples-to-apples — only the kernels differ. `src/main.c`
wraps that call in a `k_cycle_get_32()` window and reports min/avg/max.

## Telemetry fields

| Field | Meaning |
|-------|---------|
| `accelerator` | `neutron-npu` or `cpu-m33` — series/group by this in the dashboard |
| `model` | model name baked into the model data |
| `inf_ms_avg` / `inf_ms_min` / `inf_ms_max` | inference time over the cycle (ms) |
| `inf_per_sec` | throughput derived from the average |
| `iterations` | timed inferences in the cycle |
| `top_index` | argmax of the output tensor (predicted class for a classifier) |
| `arena_kb` | TFLM tensor-arena bytes used (KB) |

> Don't quote a fixed speedup — **measure it; the measured ratio is the demo.**
> For this class of int8 vision model expect the NPU to land roughly 10–30×
> faster than the M33.

## ⚠️ Drop in the eIQ package (the one real prerequisite)

The "eIQ package" here is NXP's MCUXpresso **TensorFlow Lite Micro middleware** —
upstream Zephyr's `tflite-micro` does *not* carry the Neutron backend. The demos
repo already pins the exact one in `iotc-mcx-zephyr-demos/west.yml`:

```yaml
- name: mcux-sdk-middleware-tensorflow      # github.com/nxp-mcuxpresso
  revision: MCUX_2.16.000
  path: modules/lib/tflite-micro
```

It ships everything the NPU build needs — TFLM sources, the Neutron kernel, the
Neutron headers, and the **prebuilt** `libtflm.a` / `libNeutron*.a` blobs.

**Good news:** the converted model is already in-tree (`model_npu/model_data.s` +
`.h`, and the source `face_detect_converted.tflite`), so you do **not** need to
run the eIQ Neutron Converter — only when you swap in a different model.

### Get it (pick one) → lands at `<workspace>/modules/lib/tflite-micro`

**A. west (recommended, version-matched).** Add the project (with the
`nxp-mcuxpresso` remote) to the manifest your build workspace uses, then:

```sh
west update mcux-sdk-middleware-tensorflow
```

**B. direct clone (no west surgery).** The CMake references it by path, so a
plain checkout at the default `TFLITE_DIR` works:

```sh
git clone -b MCUX_2.16.000 \
  https://github.com/nxp-mcuxpresso/mcux-sdk-middleware-tensorflow.git \
  <workspace>/modules/lib/tflite-micro
```

Match **MCUX_2.16.000** — the checked-in `model_data.s` and the Neutron kernel
ABI were produced against that tag.

### Verify these exist (the CMake links them by path)

```
modules/lib/tflite-micro/
├── lib/cm33/armgcc/libtflm.a
├── tensorflow/lite/micro/kernels/neutron/neutron.cpp
└── third_party/neutron/
    ├── common/include/   driver/include/
    └── mcxn/libNeutronDriver.a   libNeutronFirmware.a
```

If the prebuilt `.a` blobs aren't in the standalone repo, copy them from the
MCUXpresso SDK for FRDM-MCXN947 (`<SDK>/middleware/eiq/tensorflow-lite/...`, same
tree). Override paths with `-DTFLITE_DIR=...` / `-DIOTC_MCX_DEMOS_DIR=...`. The
build fails fast (CMake `FATAL_ERROR`) if the model glue path is wrong.

- [ ] `src/device_credentials.h` generated for cloud builds (see **Credentials**)

## Build & run

From a Zephyr 4.4 workspace that has the NXP HAL, with the SDK + c-lib modules
on the build line (same flags the SDK telemetry sample uses):

```sh
SDK=<abs path>/iotc-zephyr-sdk
CLIB=<abs path>/iotc-c-lib

# NPU build (default) — "neutron-npu"
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/npu \
  iotc-zephyr-demos/vendor/nxp/npu-benchmark \
  -- -DZEPHYR_EXTRA_MODULES=$SDK -DZEPHYR_IOTC_C_LIB_MODULE_DIR=$CLIB
west flash -d build/npu

# CPU build (same model on the M33) — "cpu-m33"
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/cpu \
  iotc-zephyr-demos/vendor/nxp/npu-benchmark \
  -- -DEXTRA_CONF_FILE=cpu.conf \
     -DZEPHYR_EXTRA_MODULES=$SDK -DZEPHYR_IOTC_C_LIB_MODULE_DIR=$CLIB
west flash -d build/cpu
```

Flash one, watch the dashboard, flash the other — same device id, two
`accelerator` series. (Or use two device ids to see both live at once.)

### Bench-only smoke test (no cloud, no credentials)

To validate NPU-vs-CPU timing before any cloud setup, build with
`-DCONFIG_APP_PUBLISH_TO_CLOUD=n`. The app skips networking and just logs each
cycle over the console:

```
neutron-npu | cifarnet_quant_int8_npu | avg 0.412 ms (min 0.405 / max 0.431) | 2427 inf/s | top=3 | arena 92 KB
```

This is the fastest way to confirm the eIQ artifacts are wired correctly.

## Credentials

Cloud builds need per-device TLS credentials in a git-ignored
`src/device_credentials.h` (defining `device_cert_pem`, `device_key_pem`,
`broker_ca_pem`, `dra_ca_pem`) — generated the same way as the SDK telemetry
sample (`creds/gen_creds_header.py` from your IOTCONNECT device package + the
AWS/GoDaddy roots). Device identity (CPID/env/DUID/AWS) is in `prj.conf`.

## Layout

```
npu-benchmark/
├── CMakeLists.txt        # reuses model.cpp + NPU/CPU sources; links Neutron libs
├── Kconfig               # bench knobs + MCUX_HAS_NPU / arena / log level
├── prj.conf              # connectivity (telemetry sample) + ML build (TFLM/C++/FPU)
├── cpu.conf              # overlay: CONFIG_MCUX_HAS_NPU=n  → CPU build
├── boards/
│   └── frdm_mcxn947_mcxn947_cpu0.{conf,overlay}   # Ethernet bearer (+ arena hook)
└── src/
    ├── main.c            # net + SDK lifecycle + timed benchmark loop
    ├── model_bench.{h,cpp}   # C shim isolating the C++/TFLM headers
    └── device_credentials.h  # (generated, git-ignored)
```

## Where this fits

Per [docs/demos-repo-structure.md](../../../../docs/demos-repo-structure.md),
the portable connectivity demos live in `demos/` and stay vendor-neutral; an
eIQ/Neutron-NPU showcase is inherently NXP-specific, so it belongs here under
`vendor/nxp/`. It still consumes the same vendor-neutral SDK for connectivity —
only the model build is NXP.

## Status / open seams

- **eIQ artifacts not in-tree** — the NPU build needs the binaries + converted
  model listed above; sourcing them is the one real prerequisite.
- **`top_index`** is a generic argmax readout; swap in real post-processing (or
  the demos repo's YOLO path) if you want detections instead of a class index.
- **Arena placement** — default `.bss` is fine; uncomment the `zephyr,arena`
  region in the overlay if the NPU needs the arena in a specific SRAM bank.
- **CPU-path model data** — `model_sw` carries its own (smaller) model; for a
  strict apples-to-apples comparison, build both paths from the same source
  `.tflite` (NPU via the Neutron converter, CPU as plain int8).
```

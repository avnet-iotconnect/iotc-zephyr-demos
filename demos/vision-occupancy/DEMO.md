# Edge Vision Occupancy with /IOTCONNECT on Zephyr

This document walks through the vision-occupancy demo end to end: the sequence
of steps, the behavior observable at each one, and what the device and
platform are doing underneath. Build, flash, and provisioning mechanics are
covered in the [README](README.md) and the
[RT1170-EVKB quickstart](../../boards/mimxrt1170-evkb/QUICKSTART.md).

## Overview

A 1 GHz microcontroller watches a doorway. Every frame from the kit's OV5640
camera is classified **on the device** by a TFLite-Micro person-detection CNN;
the cloud receives only the verdict — a person score, an OCCUPIED/CLEAR state,
and performance counters. Pixels leave the board in exactly one case: an
operator sends a `snapshot` command and one grayscale frame is PNG-encoded on
the device and uploaded into the platform's **Telemetry Files** panel.

Two lifecycle properties come with it:

- **Privacy by architecture, not policy** — there is no video stream to
  secure, store, or redact. The MQTT payloads are a few hundred bytes of JSON.
- **The model is data, not firmware** — the same IOTV envelope discipline as
  the [ml-model-update](../ml-model-update) demo (magic, version, CRC32,
  validate-then-swap), applied to a ~300 KB CNN: the platform pushes a new
  `.tflite`, the device trial-loads it with rollback, hot-swaps it without a
  reboot, and persists it to spare flash.

## System components

| Component | Role |
|---|---|
| MIMXRT1170-EVKB (`mimxrt1170_evk/mimxrt1176/cm7`) | NXP i.MX RT1176 Cortex-M7 @1 GHz, 64 MB SDRAM, Ethernet, running Zephyr RTOS |
| OV5640 camera module (in the kit box) | QVGA RGB565 capture over MIPI CSI-2 (J2 connector, `nxp_btb44_ov5640` shield) |
| Person-detect CNN | stock TFLM `person_detect.tflite`: int8 MobileNet, 96×96 grayscale in, person/no-person out |
| TFLite-Micro + CMSIS-NN | on-device inference runtime; arena and latency reported in telemetry |
| IOTV model blob | 32-byte header (version, name, length, CRC32) + the raw `.tflite` flatbuffer |
| /IOTCONNECT | MQTT transport, dashboard, AI Models registry, command console |

## Setup

1. **Provision the device** (no credentials are compiled into the binary):
   at the serial prompt, `iotcprov provision <duid>` generates a key and
   certificate on-chip; register the printed certificate (Self-Signed) on a
   device created from the
   [vision-occupancy template](../../templates/vision-occupancy-template.json),
   paste `iotcDeviceConfig.json` via `iotc config`, and `kernel reboot cold`.
2. **Aim the camera** at the area to monitor — a doorway or a desk works;
   detection range with the stock model is roughly 1–3 m.

## The demo script

### 1. Boot: the vision path proves itself before the network

Console, immediately after reset:

```
vision-occupancy demo starting (IOTV fmt v1)
model v1 "person-builtin" active (builtin, 300600 B, arena ...)
camera ...: RGB565 320x240 (pitch 640)
self-test: person=87% clear=12% (... ms, arena ... B)
```

The self-test frame is captured and classified before any cloud connection —
stand in front of the camera during reset and the score says so. This is the
five-minute bench check for hardware day: camera seated, model running,
latency printed.

### 2. Live occupancy on the dashboard

Once connected, telemetry publishes every 10 s — and **immediately** on any
occupancy flip. Walk into the frame:

- `vision.score` jumps (typically 80–100 %), `vision.state` flips to
  `occupied` after 2 consecutive frames over the threshold, the board LED
  lights, and the dashboard updates within about a second.

Walk away: 5 consecutive clear frames flip it back (hysteresis prevents
flicker at the threshold). `threshold 40` from the command console makes the
trigger more sensitive; the change is ACKed and visible in
`vision.threshold`.

### 3. The cloud asks what the camera sees

From the device's command console send **`snapshot`**. The ACK confirms the
upload started; the next frame is decimated to 320×180 grayscale, encoded as
a PNG **on the device**, and uploaded through the platform's file pipeline —
the firmware fetches temporary AWS credentials over mutual TLS with its
device identity, performs the STS AssumeRole hop when the tenant's bucket
requires it, signs the S3 PUT with an on-device AWS SigV4 implementation, and
announces the file. Seconds later the image renders as a card in the
device's **Telemetry Files** panel, its **Classification** carrying the
model's verdict at the moment of capture:

```
{"state":"occupied","person_pct":91,"model":"person-builtin v1"}
```

One frame, on request, with the device's own credentials — no RTSP, no side
channel, no video stream.

### 4. Push a new model — the device changes its eyes

Pack a model and upload it under **Devices → AI Models**, then push it to the
device (or use `model-fetch <url>`):

```sh
python tools/pack_model.py retrained.tflite --version 2 --name person-v2
```

Console during the push:

```
model push from platform: host=...
model download: https://...
downloaded 300600 B
persisting model (300600 B; erasing 303104 B first)...
model v2 "person-v2" active (cloud, 300600 B, arena ... B)
```

Watch `model.ver` flip 1 → 2 and `model.src` flip `builtin` → `cloud` on the
dashboard. Inference never stopped for longer than the swap itself; there was
no reboot and no reflash. Power-cycle the board: the model comes back as
`model.src: flash` — it was restored from the spare flash partition before
the built-in was even considered.

A malformed push (wrong dims, truncated file, bad CRC) is **rejected with the
reason in the ACK** and the previous model keeps running — the trial load
happens in staging memory with the active model intact.

### 5. Revert

`model-reset` erases the flash copy and reinstates the built-in model;
`model-info` at any time ACKs the active model's version, name, CRC and
arena use.

## What to look at underneath

- **Command ACKs** carry the interesting state transitions: every model
  install/rejection reason, snapshot arming, threshold changes.
- **`vision.infer_ms` / `vision.fps`** quantify the edge-AI claim: an int8
  MobileNet at QVGA-derived 96×96 input runs in tens of milliseconds on the
  M7 with CMSIS-NN.
- **`sys.*` vitals** show the cost: heap, CPU, uptime — the whole pipeline
  (camera DMA, inference, TLS, MQTT) fits comfortably on the RT1176 with
  headroom.

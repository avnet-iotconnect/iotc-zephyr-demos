# /IOTCONNECT ML Model Update — Demo Flow and Internals

This document walks through the ml-model-update demo end to end: the sequence
of steps, the behavior observable at each one, and what the device and
platform are doing underneath. Build, flash, and provisioning mechanics are
covered in the [README](README.md) and the
[E54 quickstart](../../boards/sam-e54-xpro/QUICKSTART.md).

## Overview

On this device, the machine-learning model is **data, not firmware**: a
124-byte, CRC-protected blob that IOTCONNECT delivers from its AI Models
registry. The device validates the blob, swaps it in without a reboot or
reflash, persists it across power cycles, and acknowledges the result to the
platform.

This separation addresses a common lifecycle problem: deployed devices
usually need model tuning after the hardware ships. When weights changes
require a firmware release, model iteration is coupled to the firmware
release cycle, with all of its testing and rollout overhead. Here the
firmware defines *what the device can compute* — the sensing, the inference
engine, the validation and safety rules — while the platform's model registry
defines *what the device currently computes*. The two evolve independently.

## System components

| Component | Role |
|---|---|
| SAM E54 Xplained Pro (`same54_xpro`) | Microchip Cortex-M4F @120 MHz with Ethernet, running Zephyr RTOS |
| IO1 Xplained Pro wing (EXT1) | sensors and actuator: AT30TSE758 temperature (I²C), TEMT6000 light (ADC), one LED |
| IOTM model blob | 64-byte header (dimensions, class labels, LED policy, CRC32) + float32 MLP weights |
| On-device inference engine | fixed C implementation of `argmax(W2·relu(W1·x+b1)+b2)`; ~40 µs per inference |
| /IOTCONNECT | MQTT transport, dashboard, AI Models registry, command console |

Five models are pre-built and verified, each 124 bytes:

| Ver | Name | Classifies on | Classes | LED policy |
|---|---|---|---|---|
| v1 | ambient *(built in)* | light | dark / dim / bright | on when **bright** |
| v2 | comfort | temperature | cool / comfy / warm | on when **warm** |
| v3 | nightlight | light | night / dusk / day | **inverted: on when night** |
| v4 | hot-alarm | temperature | normal / warm / hot | on when warm **or** hot |
| v5 | fusion | temp **and** light | gloomy / normal / sunny | on when sunny |

All five differ only in their data: same firmware, same engine, same sensors.

## Setup

1. **Provision the device** (no credentials are compiled into the binary):
   at the serial prompt, `iotcprov provision <duid>` generates a key and
   certificate on-chip; register the printed certificate (Self-Signed) on a
   device created from the
   [ml-model-update template](../../templates/ml-model-update-template.json);
   paste the downloaded `iotcDeviceConfig.json` via `iotc config`; then
   `kernel reboot cold`.
2. **Register the model catalog**: in IOTCONNECT, **AI Models → Create
   Model** for each of the five [models/*.zip](models/) files (one model
   entry per file, version matching the model number, SageMaker conversion
   off). Registering v1 as well allows the return-to-baseline step to be a
   platform push like the others.
3. Useful views while running the demo: the serial console, the device's
   live telemetry, and the AI Models push page.

## Demo flow

### Step 1 — Boot: the built-in model

On power-up the console shows:

```
*** Booting Zephyr OS build v4.4.1 ***
<inf> phy_mii: PHY (0) Link speed 100 Mb, full duplex
<inf> ml_model_update: model v1 active (builtin, 124 B, 2-2-3, classes: dark/dim/bright)
<inf> ml_model_update: IO1 self-test: temp=24.5C light=33.0% -> class=dim (38us)
...
<inf> iotc_mqtt: MQTT connected
```

The firmware installs its compiled-in v1 "ambient" model, reads both IO1
sensors before any network activity (a bench self-test that verifies the
wiring), runs one inference, then brings up Ethernet and a TLS session to AWS
IoT via IOTCONNECT. Every five seconds it publishes `io1.temperature_c`,
`io1.light_pct`, `ml.class`, `ml.model_ver`, `ml.model_src`, and
`ml.infer_us`.

Illuminating the sensor wing (a phone flashlight works) flips `ml.class`
from `dim` to `bright` on the dashboard and turns the IO1 LED on — v1's LED
policy is "on when bright". Covering the sensor yields `dark` and the LED
turns off. Temperature has no effect under this model; that baseline is the
contrast for what follows.

### Step 2 — Push "nightlight": same sensing, opposite actuation

Pushing **nightlight (v3)** from **AI Models → Push Model** produces, within
a few seconds:

```
<inf> iotc_mqtt: C2D payload: {"v":"2.1","ct":2,"urls":[{"Url":"https://iotc-...s3.us-east-1
      .amazonaws.com/global/AI%20Model/...zip?X-Amz-...","FileName":"....zip","Code":"...",
      "version":"1.0.1"}],"ack":"..."}
<inf> ml_model_update: Model push from platform: host=iotc-...s3.us-east-1.amazonaws.com
<inf> iotc_dra: DRA GET https://iotc-...s3.us-east-1.amazonaws.com/global/AI%20Model/...
<inf> ml_model_update: model v3 active (cloud, 124 B, 2-2-3, classes: night/dusk/day)
<inf> ml_model_update: Model push: model v3 deployed (124 B)
```

The sequence behind those lines:

1. The platform sends a **module command** (`ct:2`) over the existing MQTT
   connection — a small JSON envelope carrying a presigned S3 URL for the
   model file and an acknowledgment ID. The device is notified; it does not
   poll.
2. The device opens a **second TLS session** to S3 and downloads the file —
   the exact zip uploaded to the registry. The MQTT session stays up, so
   telemetry continues throughout.
3. It unwraps the zip **in place**: a ~40-line parser walks the archive's
   entries for the stored IOTM payload. No filesystem and no decompression
   library are involved.
4. It **validates** the blob: magic, format version, layer dimensions,
   total length, and a CRC32 over the weights. A corrupt or truncated model
   is rejected — with the reason reported in the acknowledgment — and can
   never be activated.
5. It **hot-swaps** the model under a lock: the next inference simply uses
   the new weights, with no reboot. The blob is also **persisted** to NVS
   flash, so it survives power cycles.
6. It **acknowledges** the push; the platform's push status shows
   `model v3 deployed (124 B)`.

The observable change: covering the sensor now turns the LED **on** in the
dark. Same firmware, same sensors, same thresholds — the pushed blob's
`led_mask` byte inverted the actuation policy, and the dashboard's class
labels changed from `dark/dim/bright` to `night/dusk/day`, read directly
from the blob header. The device's behavior changed via 124 bytes of data.

### Step 3 — Push "hot-alarm": repurposing the device

Pushing **hot-alarm (v4)** switches the classifier to tight temperature
bands. A fingertip held on the AT30TSE758 (the small chip on the IO1 wing)
walks `ml.class` through `normal → warm → hot` within seconds, and the LED
lights for either abnormal class — an alarm-style policy covering two
classes. Light no longer affects the output: one push converted an
ambient-light node into a thermal alarm.

### Step 4 — Push "fusion": multi-sensor models are just more data

**Fusion (v5)** consumes both features (the mean of normalized temperature
and light) and classifies `gloomy / normal / sunny`. Covering the sensor
yields `gloomy`; adding light and warmth together yields `sunny` and the LED
turns on. The inference engine did not change between any of these steps —
richer behavior is still just weights.

### Step 5 — Push "ambient": return to baseline

Pushing **ambient (v1)** restores the Step 1 behavior, with one detail worth
noting: `ml.model_src` now reads `cloud` rather than `builtin`, because the
registry's copy replaced the factory default and is persisted like any other
pushed model. The `model-reset` command (or a reflash) restores the true
built-in.

## Validation and failure handling

Rejections are handled as first-class outcomes: every push is acknowledged
with a result, and a bad model can never activate.

- A zip built with ordinary compression is rejected:
  `rejected: no stored IOTM entry (a zip entry is compressed; use 'store')`.
- A blob with a corrupted byte is rejected:
  `rejected: weights CRC mismatch`.
- The active model is untouched by any rejected push, and the last-good
  model reloads from NVS after a power cycle.

## The second delivery path: models as commands

Where the interaction is defined by the device template rather than the
model registry, the same blobs travel as C2D *commands*:

- `model-push <base64>` — the entire model in one 179-character command.
- `model-begin` / `model-data <chunk>` / `model-commit` — chunked transfer
  for models larger than one command (up to 768 B in this build).
- `model-info` / `model-reset` — introspection and rollback.

Both paths converge on the same validate → hot-swap → persist code.

## Implementation notes

- **IOTM blob layout** (64-byte header + float32 weights) is documented in
  the [README](README.md#iotm-blob-format-v1).
  [tools/build_model.py](tools/build_model.py) constructs each model
  deterministically and brute-force verifies its decision bands against the
  same arithmetic the firmware executes before emitting any artifact.
- **Why a small MLP rather than TFLite-Micro:** the subject of the demo is
  the *delivery pipeline* — registry → push → download → validate → swap →
  persist → acknowledge — which is model-format-agnostic. The IOTM engine
  keeps the payload small and fully inspectable. Substituting TFLite-Micro
  as the interpreter and pushing `.tflite` files through the identical
  pipeline is the natural production follow-on; the platform upload already
  accepts `.tflite`.
- **Native delivery details:** the `ct:2` module command shares the OTA
  envelope (`ack` + `urls[]`); entries carry `Url`, `FileName`, `Code`, and
  `version` fields. The S3 download is verified against the Amazon root CA
  already present for the AWS IoT broker connection.
- **Production hardening:** the same blob can ride the full OTA mechanism
  for fleets with signed-image requirements, and a signature check (for
  example ECDSA over the blob, verified against a key in the device's root
  of trust) slots naturally in front of `model_install()`.
- **Field notes** from bringing this up on hardware, all addressed in this
  repository's modules: AWS SigV4 presigned URLs require the HTTP `Host`
  header without an explicit `:443`; the C2D worker thread needs stack
  headroom when a TLS download runs in its callback; and this SoC's I²C
  driver cannot reach 100 kHz from a 120 MHz source clock (400 kHz is the
  floor). Details are in the git history.

## Troubleshooting quick reference

| Symptom (console / ack) | Meaning | Resolution |
|---|---|---|
| `no download URL in model push` | ct:2 arrived but URL extraction failed | check the `C2D payload:` log line; report it |
| `download failed (-12)` | file larger than the 8 KiB download buffer | raise `MODEL_DL_MAX` |
| `download failed (-116/-113)` | TLS to the file host failed | if the host is not S3, add its root CA |
| `rejected: ...compressed; use 'store'` | zip built with deflate | use the generated zips / `build_model.py` |
| `rejected: weights CRC mismatch` | corrupt or truncated blob | re-upload |
| body hexdump shows `<?xml ... <Error>` | S3 refused the request | check URL expiry; see field notes |

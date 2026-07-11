# /IOTCONNECT ML Model Update — Demo Walkthrough

A presenter's script for the ml-model-update demo: what to do, what the
audience sees, and what is actually happening at every step. The build/flash
mechanics live in the [README](README.md) and the
[E54 quickstart](../../boards/sam-e54-xpro/QUICKSTART.md); this document is
the story.

## The one-sentence pitch

> On this device the machine-learning model is **data, not firmware** — a
> 124-byte, CRC-protected blob that IOTCONNECT delivers from its model
> registry, and the device validates, hot-swaps, and persists **without a
> reboot, a reflash, or a toolchain**.

Why Microchip should care: every deployed fleet faces the same problem — the
model needs tuning after the hardware ships. Reflashing firmware for a
weights change is slow, risky, and couples data-science iteration to firmware
release cycles. This demo separates them completely: the firmware defines
*what the device can compute*; the platform defines *what the device
currently thinks*.

## The cast

| Piece | Role |
|---|---|
| SAM E54 Xplained Pro (`same54_xpro`) | Microchip Cortex-M4F @120 MHz, Ethernet, running Zephyr RTOS |
| IO1 Xplained Pro wing (EXT1) | the senses: AT30TSE758 temperature (I²C), TEMT6000 light (ADC), one LED (the "actuator") |
| IOTM blob | the model: header (dims, class labels, LED policy, CRC32) + float32 MLP weights |
| Fixed inference engine | ~40 lines of C: `argmax(W2·relu(W1·x+b1)+b2)`, runs in ~40 µs |
| /IOTCONNECT | transport, dashboard, **AI Models registry**, command console |

Five models are pre-built and verified (each 124 B — small enough to read as
a hex dump on a slide):

| Ver | Name | Classifies on | Classes | LED policy |
|---|---|---|---|---|
| v1 | ambient *(builtin)* | light | dark / dim / bright | on when **bright** |
| v2 | comfort | temperature | cool / comfy / warm | on when **warm** |
| v3 | nightlight | light | night / dusk / day | **inverted: on when night** |
| v4 | hot-alarm | temperature | normal / warm / hot | on when warm **or** hot |
| v5 | fusion | temp **and** light | gloomy / normal / sunny | on when sunny |

## One-time setup (before the audience arrives)

1. **Provision the device** (flash-and-provision; no credentials in the
   binary): at the serial prompt `iotcprov provision <duid>` → register the
   printed cert (Self-Signed) on the imported
   [ml-model-update template](../../templates/ml-model-update-template.json)
   → `iotc config` (paste `iotcDeviceConfig.json`) → `kernel reboot cold`.
2. **Register the model catalog**: IOTCONNECT → **AI Models → Create Model**
   for each of the five [models/*.zip](models/) files (name/code per model,
   version = the model number, SageMaker conversion off). Registering v1
   too means the "return to baseline" is also a dashboard push.
3. Open three windows: the serial console, the device's live telemetry
   dashboard, and the AI Models push page.

## The demo, act by act

### Act 1 — Boot: the built-in model

Power the board. Console:

```
*** Booting Zephyr OS build v4.4.1 ***
<inf> phy_mii: PHY (0) Link speed 100 Mb, full duplex
<inf> ml_model_update: model v1 active (builtin, 124 B, 2-2-3, classes: dark/dim/bright)
<inf> ml_model_update: IO1 self-test: temp=24.5C light=33.0% -> class=dim (38us)
...
<inf> iotc_mqtt: MQTT connected
```

**What's happening:** the firmware installed its compiled-in v1 "ambient"
model, read both IO1 sensors *before* touching the network (bench self-test),
ran one inference in ~38 µs, then brought up Ethernet + TLS to AWS IoT via
IOTCONNECT. Every 5 s it publishes `io1.temperature_c`, `io1.light_pct`,
`ml.class`, `ml.model_ver`, `ml.model_src`, `ml.infer_us`.

**Show:** shine a phone light on the little sensor wing → `ml.class`
flips `dim → bright` on the dashboard and the IO1 LED turns on (v1's LED
policy: on when bright). Cover it → `dark`, LED off. The model is working;
note it *only* reacts to light — temperature does nothing. That's the
baseline behavior to contrast against.

### Act 2 — Push "nightlight": same sensing, opposite actuation

In **AI Models → Push Model**, push **nightlight (v3)** to the device.
Console, within a few seconds:

```
<inf> iotc_mqtt: C2D payload: {"v":"2.1","ct":2,"urls":[{"Url":"https://iotc-...s3.us-east-1
      .amazonaws.com/global/AI%20Model/...zip?X-Amz-...","FileName":"....zip","Code":"...",
      "version":"1.0.1"}],"ack":"..."}
<inf> ml_model_update: Model push from platform: host=iotc-...s3.us-east-1.amazonaws.com
<inf> iotc_dra: DRA GET https://iotc-...s3.us-east-1.amazonaws.com/global/AI%20Model/...
<inf> ml_model_update: model v3 active (cloud, 124 B, 2-2-3, classes: night/dusk/day)
<inf> ml_model_update: Model push: model v3 deployed (124 B)
```

**What's happening, in order:**

1. The platform sent a **module command** (`ct:2`) over the existing MQTT
   connection — a tiny JSON envelope carrying a presigned S3 URL for the
   model file and an acknowledgment ID. No polling; the device is told.
2. The device opened a **second TLS session** to S3 (the MQTT session stays
   up — telemetry doesn't stop) and downloaded the file: the exact zip that
   was uploaded to the registry.
3. It unwrapped the zip **in place** (a ~40-line parser walks the archive's
   entries for the stored IOTM payload — no filesystem, no unzip library).
4. It **validated** the blob: magic, format version, layer dimensions,
   length, and a CRC32 over the weights. A corrupt or truncated model is
   rejected with the reason sent back in the ack — it can never be
   activated.
5. It **hot-swapped** the weights under a lock (inference between two
   samples simply uses the new model — no reboot) and **persisted** the blob
   to NVS flash, so it survives power cycles.
6. It **acknowledged** the push; the platform's push status shows
   `model v3 deployed (124 B)`.

**Show:** cover the sensor with your hand — **the LED now turns ON in the
dark**. Same firmware, same sensors, same thresholds even — but the pushed
blob's `led_mask` byte inverted the actuation policy, and its labels changed
(`dark/dim/bright` → `night/dusk/day` on the dashboard, straight out of the
blob header). This is the money shot: *behavior shipped as 124 bytes of
data.*

### Act 3 — Push "hot-alarm": repurpose the device entirely

Push **hot-alarm (v4)**. Ack: `model v4 deployed (124 B)`.

**Show:** press a fingertip on the AT30TSE758 (the small chip on the IO1).
Within seconds `ml.class` walks `normal → warm → hot` and the LED latches on
(alarm-style: lit for either abnormal class). Light now does nothing — the
same product just changed from an ambient-light node into a thermal alarm,
from the dashboard, in one push.

### Act 4 — Push "fusion": multi-sensor models are just bigger data

Push **fusion (v5)**. This model consumes **both** features (the mean of
normalized temperature and light): `gloomy / normal / sunny`. Cover the
sensor → `gloomy`; phone light + warm fingertip → `sunny`, LED on. Point
out: the engine didn't change — richer behavior is still just weights.

### Act 5 — Push "ambient": return to baseline from the dashboard

Push **ambient (v1)**. The device is back to Act 1 behavior — with one
telling detail: `ml.model_src` now reads `cloud`, not `builtin`, because the
registry's copy replaced the factory default and is persisted like any other
push. (`model-reset` from the command console — or a reflash — restores the
true builtin.)

## Failure handling worth demonstrating

Rejections are first-class: every push is acked with the outcome, and a bad
model can never activate.

- Upload a zip made with normal compression → ack:
  `rejected: no stored IOTM entry (a zip entry is compressed; use 'store')`.
- Corrupt a byte of a blob → ack: `rejected: weights CRC mismatch`.
- The active model is untouched by any rejected push, and the last-good
  model reloads from NVS after a power cycle.

## The second path: models as commands (no registry)

For templates that define the commands, the same blobs travel as C2D
*commands* — useful where the interaction is defined by the device template
rather than the model registry:

- `model-push <base64>` — the entire model in one 179-character command.
- `model-begin` / `model-data <chunk>` / `model-commit` — chunked transfer
  for models beyond one command (up to 768 B in this build).
- `model-info` / `model-reset` — introspection and rollback.

Both paths converge on the same validate → hot-swap → persist code.

## Under the hood (for the technical audience)

- **IOTM blob layout** (64-byte header + float32 weights) is documented in
  the [README](README.md#iotm-blob-format-v1);
  [tools/build_model.py](tools/build_model.py) constructs each model
  deterministically and **brute-force verifies** its decision bands against
  the same math the firmware executes before emitting artifacts.
- **Why an MLP and not TFLite-Micro?** The point being demonstrated is the
  *delivery pipeline* (registry → push → validate → swap → persist → ack),
  which is model-format-agnostic. The IOTM engine keeps the payload small
  enough to read on a slide; swapping the interpreter for TFLite-Micro and
  pushing `.tflite` files through the identical pipeline is the natural
  production follow-on (the platform upload already accepts `.tflite`).
- **Native delivery details**: the `ct:2` module command shares the OTA
  envelope (`ack` + `urls[]`); entries carry `Url` / `FileName` / `Code` /
  `version`. The download is verified against the Amazon root CA already
  compiled in for the AWS IoT broker.
- **Production path**: the same blob can ride full OTA for signed-image
  fleets; a signing step (e.g. ECDSA over the blob, verified with a key in
  the device root of trust) slots naturally in front of `model_install()`.
- **Field notes** (all found on this hardware, all fixed upstream in this
  repo's modules): SigV4 presigned URLs require the `Host` header without
  `:443`; the C2D worker needs stack headroom for an in-callback TLS
  download; and this SoC's I²C driver can't reach 100 kHz from a 120 MHz
  GCLK — details in the git history for anyone productizing this path.

## Troubleshooting quick reference

| Symptom (console / ack) | Meaning | Fix |
|---|---|---|
| `no download URL in model push` | ct:2 arrived but URL extraction failed | check the `C2D payload:` log line, file an issue with it |
| `download failed (-12)` | file larger than the 8 KiB download buffer | raise `MODEL_DL_MAX` |
| `download failed (-116/-113)` | TLS to the file host failed | host not S3? add its root CA |
| `rejected: ...compressed; use 'store'` | zip built with deflate | use the generated zips / `build_model.py` |
| `rejected: weights CRC mismatch` | corrupt/truncated blob | re-upload |
| body hexdump shows `<?xml ... <Error>` | S3 refused the request | check URL expiry; see field notes |

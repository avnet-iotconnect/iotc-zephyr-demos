# /IOTCONNECT eIQ Predictive Maintenance (Vibration) — Demo Flow and Internals

This document walks through the eiq-pdm-vibration demo: the sequence of
steps, the behavior observable at each one, and what the device and platform
are doing underneath. Build, flash, and provisioning mechanics are in the
[README](README.md) and [QUICKSTART](QUICKSTART.md).

## Overview

Predictive maintenance is the canonical industrial edge-ML use case: watch a
machine's vibration signature and flag developing faults before failure.
This demo runs the entire loop on a single dev board plus one add-on —
including the *machine itself*: the MIKROE ML Vibro Sens Click carries both
a 3-axis accelerometer (NXP FXLS8974) and **two small DC motors**, one
balanced and one deliberately unbalanced. The unbalanced motor *is* the
fault, and it is switched on from the cloud.

That closes a loop most ML demos leave open: the operator injects a known
ground-truth condition from the IOTCONNECT console (`inject-fault`), and
watches the on-device model detect it seconds later — ground truth
(`vib.motor`) and model verdict (`vib.model_class`) sit side by side on the
dashboard.

The classifier is a genuine trained model: a 4-class Random Forest produced
by **NXP eIQ Time Series Studio** from data captured by this same firmware,
compiled to a Cortex-M33 library. Inference costs ~0.05 ms per sample with
~0.3 KB of RAM. When the model library is absent, the firmware falls back to
an RMS-threshold heuristic — the demo degrades, it does not break.

## System components

| Component | Role |
|---|---|
| FRDM-MCXN947 | Ethernet-connected Zephyr target (Cortex-M33) |
| ML Vibro Sens Click (MIKROE-6470) | FXLS8974 accelerometer @400 Hz **and** the vibration source: balanced + unbalanced motors, driven by the firmware |
| eIQ Time Series Studio model (`model/libtss.a`) | 4-class Random Forest: idle / balanced / unbalanced / both |
| RMS heuristic | threshold fallback used when no model is linked |
| /IOTCONNECT | fault-injection commands, live `vib.*` telemetry, ack trail |

## The two build phases

The same source file builds two ways:

- **Capture (default build):** network-free. The firmware cycles the motors
  through the four labeled states (4 s each) while streaming ~100 Hz
  accelerometer CSV to the console:

  ```
  VIB,t_ms,state,ax_g,ay_g,az_g
  VIB,8003,unbalanced,-0.310,0.092,1.560
  ```

  That stream, split per class by `tools/tss_convert.py`, is exactly what
  eIQ Time Series Studio imports for training — the demo generates its own
  training set with **self-labeled** data (the firmware knows which motor it
  is driving, so labels are free and correct).

- **Connect (overlay build):** adds the IOTCONNECT stack. The device windows
  the accelerometer, classifies each window, and publishes; the cloud drives
  the motors. The shipped, pre-trained model is committed in `model/`, so
  the connect build works out of the box; retraining is optional.

## Demo flow (connect build)

### Step 1 — Provision and connect

The connect build uses the flash-and-provision identity flow (no compiled-in
device key): `iotcprov provision <duid>` generates an EC P-256 key on-chip,
the printed certificate registers a Self-Signed device on the
[eIQ PdM template](../../templates/eiq-pdm-vibration-template.json), and
after `iotc config` + reboot:

```
<inf> eiq_pdm: eIQ TSS model ready: 4-class classifier (per-sample)
<inf> eiq_pdm: Provisioned as duid=... -- bringing up network
<inf> eiq_pdm: Connected. Send C2D 'inject-fault' / 'inject-healthy' / 'motor-stop' to drive the demo.
```

### Step 2 — Baseline: healthy machine

Send `inject-healthy`. The balanced motor spins; the board vibrates gently.
Each second the firmware collects a 1-second accelerometer window, runs the
classifier on **every sample** in the window, and majority-votes the result
for stability:

```
<inf> eiq_pdm: vib: state=healthy score=0.08 rms=0.220 src=eiq-model (driving balanced)
```

Telemetry: `vib.state` (healthy/warning/fault), `vib.anomaly_score` (mean
fault probability), `vib.rms_g` plus per-axis `vib.rms_x/y/z`, `vib.motor`
(the injected ground truth), `vib.model_class` (the raw model verdict), and
`vib.source` (`eiq-model` or `rms-heuristic`).

### Step 3 — Inject the fault from the cloud

Send `inject-fault`. The unbalanced motor spins up (software-pulsed at ~30 %
duty — it is never held at full power), the vibration signature changes, and
within a window or two:

```
<inf> eiq_pdm: vib: state=fault score=0.87 rms=0.502 src=eiq-model (driving unbalanced)
```

On the dashboard, `vib.motor` flipped the moment the command was acked —
that is the ground truth. `vib.model_class` follows within seconds — that is
the detection. The gap between the two lines is the model's response time,
visible live. `motor-stop` and `run-both` complete the state set (the
model's hardest class is `both`, where the balanced motor partially masks
the unbalanced one — its per-class recall is the lowest, a realistic
imperfection worth showing rather than hiding).

### Step 4 — Tuning knobs over C2D

| Command | Effect |
|---|---|
| `inject-fault` / `inject-healthy` / `run-both` / `motor-stop` | drive the motor state (ground truth) |
| `set-threshold <g>` | RMS fault threshold for the fallback path (0–2 g) |
| `set-interval <s>` | publish period, 1–3600 s |
| `reboot` | acks first, then cold-reboots |

All commands are acknowledged; unknown commands ack as failed with
`unknown command`.

## Implementation notes

- **Per-sample classification + majority vote:** the model classifies every
  ~100 Hz sample and the window votes, which suppresses single-sample
  flicker without adding latency beyond the 1 s window.
- **Model footprint:** the committed Random Forest reports ~0.3 KB RAM and
  ~276 KB flash, ~0.05 ms per inference; balanced accuracy ≈0.77 with
  `both` as the weakest class (recall ≈0.44). The full training report is in
  `training/`.
- **Sensor rate matters:** the accelerometer driver defaults to 6.25 Hz and
  only accepts rate changes in standby, so the firmware explicitly
  standbys → sets 400 Hz → reactivates. Without this, every reading repeats
  ~16× and the vibration data is meaningless.
- **Graceful degradation:** if `model/libtss.a` is absent or fails to
  initialize, the same firmware classifies via RMS thresholds and reports
  `vib.source=rms-heuristic` — the hardware-verified separation (idle ≈0.04,
  balanced ≈0.22, unbalanced ≈0.50 g-RMS) makes the fallback credible.
- **Licensing:** the model library is NXP-licensed (LA_OPT online code
  hosting); it is committed for out-of-the-box builds, usable in
  combination with NXP products.
- **Hard-float linkage:** the model library uses VFP register arguments, so
  the connect build enables the FPU; a soft-float build fails to link.

## Troubleshooting quick reference

| Symptom | Meaning | Resolution |
|---|---|---|
| readings repeat ~16× in capture CSV | accelerometer stuck at default 6.25 Hz | keep the standby→ODR→active sequence (in `hw_init`) |
| `eiq TSS model init failed; falling back to RMS heuristic` | model library missing/incompatible | check `model/libtss.a` + `TimeSeries.h`; rebuild |
| link error "uses VFP register arguments" | FPU disabled with hard-float model | `CONFIG_FPU=y` (set in connect.conf) |
| provisioning guide at boot | no identity in NVS | run the `iotcprov provision` flow |
| LinkServer `CRITICAL` on flash | probe wedge | replug MCU-Link USB, reflash promptly |

# eIQ predictive-maintenance — vibration (FRDM-MCXN947)

On-device **predictive maintenance** for rotating machines: a MIKROE **ML Vibro
Sens Click** (NXP **FXLS8974CF** 3-axis accelerometer) measures vibration, an
**eIQ Time Series Studio** model flags **imbalance / bearing wear / blocked /
abnormal** states, and the verdict streams to **/IOTCONNECT**. The eIQ **Neutron
NPU** on the MCXN947 runs the model. This is NXP's reference vibration-anomaly
setup (eIQ TSS + the Smart Fan / portable-anomaly-detect demos).

The eIQ workflow is **capture → train → deploy**, so this demo ships in phases:

| Phase | What | Status |
|---|---|---|
| **1 — capture** *(this build)* | Sample the FXLS8974 and print CSV for eIQ TSS training | ✅ builds |
| **2 — deploy** | Load the TSS model, infer on-device, stream `vib.state`/`vib.anomaly_*` to /IOTCONNECT (+ device-vitals) | 🔜 needs a trained model |

## Hardware — and why it's simple

- **FRDM-MCXN947** + a **mikroBUS Shuttle** (or a direct socket).
- **ML Vibro Sens Click** (FXLS8974CF) on the mikroBUS I²C (`@0x18`).
- A **vibration source** — no rig or plumbing needed: mount the board/Click on a
  small **fan or motor** and stage states by hand:
  *healthy* → *imbalance* (stick a scrap of tape/a paperclip to a blade) →
  *blocked* (restrict the airflow) → *bearing/rough* (press on it) → *off*.
  You can also just **tap or shake the board** to prove the signal before mounting.

## Phase 1 — capture training data (build)

```sh
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/eiq_vib \
  C:/dev/zephyr/iotc-zephyr-demos/demos/eiq-pdm-vibration
west flash -d build/eiq_vib      # onboard MCU-Link (LinkServer)
```

Open the MCU-Link VCom @115200. The board streams CSV at ~100 Hz:

```
VIB,t_ms,ax_g,ay_g,az_g
VIB,120,0.0132,-0.0071,1.0024
VIB,130,0.0145,-0.0069,0.9981
...
```

**Log the console to a file** while you run each machine state for a while, then
**label** the segments in **eIQ Time Series Studio** (import → label
*healthy/imbalance/…* → autoML → export a deployment library). `t_ms` records the
exact sample time so TSS can resample to a fixed window.

> **Sample rate:** ~100 Hz over the default 115200 console captures vibration
> content up to ~50 Hz (fine for typical fan/motor RPM bands). For higher-
> frequency content, raise the console baud, or use the FXLS8974 FIFO + on-device
> windowing (a Phase-2 refinement).

## Phase 2 — deploy to /IOTCONNECT (next)

Once you export the TSS model, Phase 2 wires it in: window the accel stream, run
inference (Neutron NPU), and publish per interval to /IOTCONNECT —

```json
{ "vib": { "rms_g": 0.42, "state": "imbalance", "anomaly_score": 0.88 },
  "sys": { ... device vitals ... } }
```

with a device template (state + anomaly chart + alerts) and C2D (reset baseline /
set threshold). This reuses the SDK connect path from `click-telemetry`
(Ethernet → mutual-TLS MQTT) and the `sys` device-vitals sidecar.

> **Note on eIQ TSS:** Time Series Studio is a desktop autoML tool (MCUXpresso /
> VS Code deploy target). This repo's firmware is Zephyr; the TSS-generated
> library is plain C (init + inference) and is expected to link into the Zephyr
> app, validated in Phase 2. If it needs the MCUXpresso runtime, the fallback is
> a MCUXpresso inference app bridged to /IOTCONNECT via the portable `iotc-c-lib`.

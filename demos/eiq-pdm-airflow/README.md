# eIQ predictive-maintenance — airflow ΔP (FRDM-MCXN947)

On-device **predictive maintenance** for HVAC/airflow: a MIKROE **Ultra-Low Press
Click** (Sensirion **SDP810**, ±125 Pa) measures the differential pressure across
a filter/restriction, an **eIQ Time Series Studio** model flags **filter-clog /
airflow anomalies**, and the verdict streams to **/IOTCONNECT**. The eIQ **Neutron
NPU** on the MCXN947 runs the model.

The eIQ workflow is **capture → train → deploy**, so this demo ships in phases:

| Phase | What | Status |
|---|---|---|
| **1 — capture** *(this build)* | Sample the SDP810 and print CSV for eIQ TSS training | ✅ builds |
| **2 — deploy** | Load the TSS model, infer on-device, stream `airflow.anomaly_*` + ΔP to /IOTCONNECT (+ device-vitals) | 🔜 needs a trained model |

## Hardware

- **FRDM-MCXN947** + a **mikroBUS Shuttle** (or a direct socket).
- **Ultra-Low Press Click** (MIKROE-4676, SDP810) on the mikroBUS I²C (`@0x6C`).
- An **airflow rig** to create ΔP episodes — a pump/fan + tube + an adjustable
  restriction (the "filter"). See the repo notes: an aquarium/marine air pump +
  airline **T-fittings** + a control valve is the cheapest controllable rig; tap
  the SDP810's two ports across the valve, then open/close it to stage
  *normal → clogging → leak → fan-fault*. You can also just **blow into the port**
  to prove the signal before the rig arrives.

## Phase 1 — capture training data (build)

```sh
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/eiq_pdm \
  C:/dev/zephyr/iotc-zephyr-demos/demos/eiq-pdm-airflow
west flash -d build/eiq_pdm      # onboard MCU-Link (LinkServer)
```

Open the MCU-Link VCom @115200. The board streams CSV at ~10 Hz:

```
PDM,t_ms,dp_pa,temp_c
PDM,120,0.412,24.83
PDM,220,0.418,24.84
...
```

**Capture the log to a file** (your terminal's "log to file", or pipe a serial
reader) while you run each rig episode for a while, then **label** the segments
in **eIQ Time Series Studio** (import → label *normal/clog/leak/...* → autoML →
export a deployment library). `t_ms` records the exact sample time so TSS can
resample to a fixed window if needed.

## Phase 2 — deploy to /IOTCONNECT (next)

Once you export the TSS model library, Phase 2 wires it in: run inference on the
ΔP window (Neutron NPU), and publish per interval to /IOTCONNECT —

```json
{ "airflow": { "dp_pa": 83.1, "dp_rms": 2.4, "state": "clog", "anomaly_score": 0.86 },
  "sys": { ... device vitals ... } }
```

with a device template (anomaly chart + state + alerts) and C2D (reset baseline /
set threshold). This phase reuses the SDK connect path from `click-telemetry`
(Ethernet → mutual-TLS MQTT) and the `sys` device-vitals sidecar.

> **Note on eIQ TSS:** Time Series Studio is a desktop autoML tool (MCUXpresso /
> VS Code deploy target). This repo's firmware is Zephyr; the TSS-generated
> library is plain C (init + inference) and is expected to link into the Zephyr
> app, but that integration is validated in Phase 2. If it needs the MCUXpresso
> runtime, the fallback is a MCUXpresso inference app bridged to /IOTCONNECT via
> the portable `iotc-c-lib` (same pattern as the UART telemetry source).

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
| **2 — connect** | Connect + publish `vib.*` (RMS heuristic now, eIQ model later) + cloud fault-injection commands (+ device-vitals) | ✅ builds |

## Hardware — self-contained, no external rig

- **FRDM-MCXN947** + a **mikroBUS Shuttle** (or a direct socket).
- **ML Vibro Sens Click** (MIKROE-6470) on the mikroBUS — that's it.

The Click carries **both the sensor and the vibration source**: the FXLS8974CF
accelerometer (I²C `@0x18`) plus **two onboard DC motors** — a *balanced* motor
(steady "healthy" baseline) and an *unbalanced* motor ("fault"/imbalance). The
firmware drives the motors itself, so **no external fan/motor or rig is needed**.
Motor pins (from the board overlay): BAL → mikroBUS CS (`gpio3 23`), UNB →
mikroBUS PWM (`gpio3 19`); the unbalanced motor is software-pulsed (~30 % duty)
so it is never held at continuous full power.

## Phase 1 — capture (self-labeling) training data (build)

```sh
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/eiq_vib \
  C:/dev/zephyr/iotc-zephyr-demos/demos/eiq-pdm-vibration
west flash -d build/eiq_vib      # onboard MCU-Link (LinkServer)
```

The firmware cycles the motors through labeled states (4 s each) and streams CSV
at ~100 Hz on the MCU-Link VCom @115200, with the **state column auto-labeling
the data** for eIQ Time Series Studio:

```
VIB,t_ms,state,ax_g,ay_g,az_g
VIB,4002,balanced,0.155,0.085,1.180
VIB,8003,unbalanced,-0.310,0.092,1.560
...
```

`state` ∈ `idle` / `balanced` (healthy) / `unbalanced` (fault) / `both`. **Log the
console to a file**, then in eIQ Time Series Studio import it, use the `state`
column as the label, autoML a model, and export a deployment library — no manual
labeling. HW-verified per-state separation on the FRDM-MCXN947 (g-rms of the AC
component): idle ≈ 0.04, balanced ≈ 0.22, unbalanced ≈ 0.50 — cleanly distinct.

> **Sample rate:** ~100 Hz over the 115200 console captures vibration content up
> to ~50 Hz (fine for these motors / typical RPM bands). For higher-frequency
> content, raise the console baud, or use the FXLS8974 FIFO + on-device windowing
> (a Phase-2 refinement).

## Phase 2 — connect to /IOTCONNECT (build)

The **same demo** builds a connected monitor when `CONFIG_IOTCONNECT` is on: it
windows the accelerometer, classifies the state, and publishes to /IOTCONNECT.
Connect mode adds Ethernet (`connect.overlay`) + the SDK stack (`connect.conf`)
and pulls in the `iotc-zephyr-sdk` module. Because `.conf`/`.overlay` selection
mangles under some shells' `-D` passthrough, pass them via the environment:

```sh
export ZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk
export ZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
export EXTRA_CONF_FILE=connect.conf
export EXTRA_DTC_OVERLAY_FILE=connect.overlay
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/eiq_vib_connect \
  C:/dev/zephyr/iotc-zephyr-demos/demos/eiq-pdm-vibration
west flash -d build/eiq_vib_connect
```

Provision device identity the usual way (baked `device_credentials.h`, or the
quickstart flow). It publishes every ~2 s:

```json
{ "vib": { "state": "fault", "anomaly_score": 0.86, "rms_g": 0.50,
           "rms_x": 0.19, "rms_y": 0.09, "rms_z": 0.45, "motor": "unbalanced" },
  "sys": { "cpu_pct": 3.1, "freq_mhz": 150, "heap_used": ..., "uptime_s": ... } }
```

### Cloud-driven fault injection (the demo)

Because the Click drives its own motors, you inject faults **from the IOTCONNECT
dashboard** and watch them get detected. Commands (device template:
[`templates/eiq-pdm-vibration-template.json`](../../templates/eiq-pdm-vibration-template.json)):

| Command | Effect |
|---|---|
| `inject-fault` | spin the **unbalanced** motor → `vib.state` goes `fault`, score spikes |
| `inject-healthy` | spin the **balanced** motor → back to `healthy` |
| `run-both` / `motor-stop` | both motors / idle |
| `set-threshold <g>` | fault threshold on `vib.rms_g` |
| `set-interval <s>` | reporting cadence |
| `reboot` | restart |

Classification today is an **RMS-threshold heuristic** (idle 0.04 / balanced 0.22
/ unbalanced 0.50 g-rms are cleanly separable). When you export a model from eIQ
Time Series Studio, it drops in behind the same `vib.*` telemetry.

> **Note on eIQ TSS:** Time Series Studio is a desktop autoML tool (MCUXpresso /
> VS Code deploy target). The generated library is plain C (init + inference) and
> is expected to link into the Zephyr app; if it needs the MCUXpresso runtime,
> the fallback is a MCUXpresso inference app bridged to /IOTCONNECT via the
> portable `iotc-c-lib` (same pattern as the UART telemetry source).

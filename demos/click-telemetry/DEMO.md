# /IOTCONNECT Click Telemetry — Demo Flow and Internals

This document walks through the click-telemetry demo: the sequence of
events, the behavior observable at each step, and what the device is doing
underneath. Build mechanics are in the [README](README.md).

## Overview

Real deployments rarely ship one fixed sensor. This demo treats the sensor
complement as **discoverable at power-up**: a registry of supported MikroE
Click sensor boards is probed over I²C, whatever answers is adopted, and
each publish cycle reads every recognized sensor into a single
nested-object telemetry message. Fitting a different combination of Clicks
changes what the device reports — with no rebuild.

On the FRDM-MCXN947, a MikroE **Shuttle** fans one mikroBUS socket out to
four Click positions sharing the same I²C bus, so up to four sensors ride
one socket. On the SAM E54 Xplained Pro, the same registry recognizes the
**IO1 Xplained Pro** sensor wing. All sensors are read with raw I²C register
sequences (no per-sensor Zephyr drivers), which keeps the registry easy to
extend: one probe address and one read function per sensor.

## System components

| Component | Role |
|---|---|
| FRDM-MCXN947 (or `/ns` TF-M build), SAM E54 Xplained Pro | Ethernet-connected Zephyr target with a sensor socket |
| MikroE Shuttle + Click sensors, or IO1 Xplained Pro | the interchangeable sensor complement |
| Sensor registry (in the application) | probe address + raw-I²C read routine per supported sensor |
| /IOTCONNECT | dashboard charting each sensor as a nested object; command console |

Supported sensors and their telemetry namespaces:

| Sensor | Chip | I²C addr | Namespace | Values published |
|---|---|---|---|---|
| Temp&Hum 14 Click | TE HTU31D | 0x40 | `temp_hum_14` | `temperature_c`, `humidity_pct` |
| Altitude 2 Click | TE MS5607 | 0x76 | `altitude_2` | `pressure_mbar`, `temperature_c`, `altitude_m` |
| Altitude 4 Click | baro | 0x27 | `altitude_4` | `pressure_hpa`, `temperature_c`, `altitude_m` |
| Ultra-Low Press Click | diff-press | 0x6C | `ultra_low_press` | `pressure_pa`, `temperature_c` |
| VAV Press Click | diff-press | 0x5C | `vav_press` | `pressure_pa`, `temperature_c` |
| Air quality 7 Click | MiCS-VZ-89TE | 0x70 | `air_quality_7` | `co2eq_ppm`, `tvoc_ppb` |
| T6713 CO₂ | Amphenol T6713 | 0x15 | `t6713` | `co2_ppm` |
| T9602 | Amphenol T9602 | 0x28 | `t9602` | `humidity_pct`, `temperature_c` |
| PHT Click *(off by default)* | TE MS8607 | 0x40/0x76 | `pht` | `pressure_mbar`, `temperature_c`, `humidity_pct` |
| IO1 Xplained Pro wing | AT30TSE758 | 0x4F | `io1` | `temperature_c` |

## Demo flow

### Step 1 — Boot: the detection scan

```
<inf> click_telemetry: Scanning mikroBUS/Shuttle Click positions...
<inf> click_telemetry:   [RECOGNIZED] Temp&Hum 14      0x40  TE HTU31D
<inf> click_telemetry:   [  absent  ] Altitude 2       0x76  TE MS5607
<inf> click_telemetry:   [RECOGNIZED] Air quality 7    0x70  MiCS-VZ-89TE
...
```

Each enabled registry entry gets a best-effort presence probe — a one-byte
I²C read; an ACK marks the sensor present. The scan result *is* the device's
sensor configuration for this power cycle. Physically swapping Clicks and
rebooting yields a different scan and different telemetry, no rebuild
involved.

### Step 2 — Connected publishing

```
<inf> click_telemetry: Published telemetry from 4 Click(s): temp_hum_14 altitude_4 ultra_low_press air_quality_7
```

After the standard bring-up (network → SNTP → discovery → MQTT/TLS), each
cycle reads every recognized sensor using its reference register sequence
and conversion math, and publishes all readings in one message using dotted
keys (`temp_hum_14.temperature_c`, `air_quality_7.co2eq_ppm`, …). The
protocol library nests these one level, matching the OBJECT-typed
attributes in the device template — so the dashboard groups each sensor's
channels together. The `sys` device-vitals object rides along.

### Step 3 — Cloud control

Commands from the device console, matched case-insensitively and always
acknowledged:

| Command | Effect |
|---|---|
| `led-on` / `led-off` / `led-toggle` | drives the board LED |
| `set-reporting-interval <seconds>` | publish period, 1–3600 s |
| `reboot` | acks first, then cold-reboots — after which the detection scan runs again |

The reboot command is a natural way to show re-detection live: change the
fitted Clicks, send `reboot`, and watch the new scan and the new telemetry
shape arrive.

## Implementation notes

- **Address collisions are the constraint of a shared bus:** the PHT Click
  (MS8607) is registry-disabled by default because it answers on 0x40 and
  0x76 — the same addresses as Temp&Hum 14 and Altitude 2. It can be
  enabled when it is the only sensor fitted. On the shared bus, fitted
  sensors must have distinct addresses.
- **Template:**
  [click-demos-device-template.JSON](../../templates/click-demos-device-template.JSON)
  defines one OBJECT attribute per sensor namespace plus the `sys` vitals
  object and all four commands.
- **Two identity models, one source file:** the standard MCXN947 build uses
  compiled-in credentials; the `/ns` **TF-M build** of the same demo loads
  its identity from TF-M-sealed Protected Storage (provisioned via the
  quickstart flow) — the device key is hardware-protected and crypto runs
  in the secure world. Both are hardware-verified.
- **Board specifics:** the FRDM mikroBUS socket has no I²C pull-ups — the
  bus needs a Click that provides them or external ~4.7 kΩ resistors. On
  the SAM E54 the sensor bus is EXT1's private SERCOM3, and it runs at
  400 kHz out of necessity (this SoC's I²C divider cannot reach 100 kHz
  from its 120 MHz clock) — which makes the 100 kHz-only Air quality 7 the
  one unsupported sensor on that board.

## Troubleshooting quick reference

| Symptom | Meaning | Resolution |
|---|---|---|
| every sensor scans `absent` | bus not wired / no pull-ups | check socket seating; add pull-ups (FRDM) |
| a fitted sensor scans `absent` | address clash or damaged sensor | check the address column above; fit distinct addresses |
| PHT readings missing | disabled by default (clashes with two other Clicks) | enable it in the registry when fitted alone |
| values print as `*float*` | picolibc float printf disabled | `CONFIG_PICOLIBC_IO_FLOAT=y` |
| no dashboard grouping per sensor | template not imported | import the click-demos device template |

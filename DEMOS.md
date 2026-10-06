# Demonstration Guide

What each demonstration shows, which boards run it, the device template it
needs, and where its documentation lives. Verification status per board is
maintained on the manufacturer pages ([NXP](README_NXP.md),
[Microchip](README_MICROCHIP.md)).

Every device is created from a template, and the template is fixed at
device creation — import the one matching the demonstration you will run
(Devices → Templates → Import in /IOTCONNECT), from
[templates/](templates/).

Troubleshooting: if cloud-to-device commands fail with a generic
"Internal server error" (or report success but never arrive) while
telemetry flows normally, check that every template in the account has a
UNIQUE `msgCode` — duplicated msgCodes (typically from re-importing an
edited template export without changing it) break command dispatch. Delete
or fix the duplicates, keep one template per msgCode, and resend.

> **Demo quota protection:** every demo stops publishing after **500
> telemetry messages per boot** so an evaluation device cannot drain your
> /IOTCONNECT message quota. The console announces the limit at connect
> and when it is reached. Change it live with `iotc limit <n>` (0 =
> unlimited), by reboot, or at build time via
> `CONFIG_IOTCONNECT_DEMO_MSG_LIMIT`.

## Portable demonstrations

| Demonstration | Shows | Boards | Template | Prebuilt image | Dashboard |
|---|---|---|---|---|---|
| [quickstart](demos/quickstart) | Flash-and-provision baseline: on-device keygen, runtime identity, telemetry | RW612, MCXN947 (+TF-M), RT1170, i.MX93, SAM E54 | `zephyr-telemetry-template.json` | RW612, MCXN947, RT1170, SAM E54 ([Releases](https://github.com/avnet-iotconnect/iotc-zephyr-demos/releases)) | `zephyr-telemetry` (shared) |
| [telemetry](demos/telemetry) | Portable periodic telemetry with device vitals | RW612, MCXN947, RT1170, i.MX93, SAM E54 | `zephyr-telemetry-template.json` | RW612 | [`zephyr-telemetry`](demos/telemetry/dashboard) |
| [c2d-led](demos/c2d-led) | Cloud-to-device commands driving the board LED | RW612, MCXN947, RT1170, SAM E54 | `c2d-led-template.json` | RW612 | [`c2d-led`](demos/c2d-led/dashboard) |
| [softap-provisioning](demos/softap-provisioning) | Phone-browser onboarding: the device raises a setup Wi-Fi AP and a web portal provisions everything | RW612 | `zephyr-telemetry-template.json` | — | `zephyr-telemetry` (shared) |
| [kinetis-meter](demos/kinetis-meter) | Meter host for the NXP Kinetis-M metrology board: Soft-AP onboarding, UART ingest, onboard temp, LED commands, OTA | RW612 | `kinetis-meter-template.json` | RW612 | [`kinetis-meter`](demos/kinetis-meter/dashboard) |
| [click-telemetry](demos/click-telemetry) | Auto-detected MikroE Click sensors on the mikroBUS/Shuttle I2C bus | RW612, MCXN947 (+TF-M), SAM E54 | `click-demos-device-template.JSON` | RW612 | [`click-telemetry`](demos/click-telemetry/dashboard) |
| [eiq-pdm-vibration](demos/eiq-pdm-vibration) | eIQ-trained vibration classifier from a PDM microphone, with cloud fault injection | MCXN947 | `eiq-pdm-vibration-template.json` | `frdm_mcxn947_eiq-pdm-vibration.hex` | [`eiq-pdm-vibration`](demos/eiq-pdm-vibration/dashboard) |
| [vision-occupancy](demos/vision-occupancy) | Camera + TFLM person detection with cloud snapshots and model push | RT1170 (OV5640 shield) | `vision-occupancy-template.json` | — | [`vision-occupancy`](demos/vision-occupancy/dashboard) |
| [gateway](demos/gateway) | i.MX93 gateway: UART child ingest, store-and-forward spool on eMMC | i.MX93 | `gateway-template.json` | — | [`gateway`](demos/gateway/dashboard) |
| [uart-telemetry-source](demos/uart-telemetry-source) | Radio-less boards emitting IOTCONNECT telemetry JSON over UART for a gateway | MCXE31B, MCXW72 | none (children of `gateway-template.json`, tag `uartsrc`) | — | (via gateway) |
| [ml-model-update](demos/ml-model-update) | Cloud-pushed ML model updates | SAM E54 | `ml-model-update-template.json` | — | [`ml-model-update`](demos/ml-model-update/dashboard) |

## Vendor demonstrations

| Demonstration | Shows | Boards | Template | Notes |
|---|---|---|---|---|
| [npu-benchmark](vendor/nxp/npu-benchmark) | eIQ Neutron NPU vs CPU inference benchmark | MCXN947 | telemetry attributes documented in its README | needs NXP's eIQ TFLM middleware (`-DTFLITE_DIR`) |
| [face-detect](vendor/nxp/face-detect) | Camera face detection with LCD overlay and cellular uplink | MCXN947 (reworked) | `mcxn947-facedet-device-template.JSON` | camera rework disconnects Ethernet; two Zephyr patches ship in the demo |

## How to run one

1. **Prebuilt image available?** Follow the [quickstart](QUICKSTART.md) —
   flash, provision at the console, import the demonstration's template.
2. **Building from source?** The [developer guide](DEVELOPER_GUIDE.md)
   covers the workspace, board targets, and the two device-identity models;
   each demonstration's README gives its exact build command and any
   hardware setup.
3. Several demonstrations have a **DEMO.md** beside their README — a
   narrated end-to-end walkthrough of what you observe at each step and
   what the device and platform are doing underneath. Some also ship a
   ready-made dashboard export (click-telemetry, eiq-pdm-vibration,
   gateway) — import it via Dashboards → Create Dashboard → Import.

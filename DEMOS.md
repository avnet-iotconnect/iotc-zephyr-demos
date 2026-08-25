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

## Portable demonstrations

| Demonstration | Shows | Boards | Template | Prebuilt image |
|---|---|---|---|---|
| [quickstart](demos/quickstart) | Flash-and-provision baseline: on-device keygen, runtime identity, telemetry | RW612, MCXN947 (+TF-M), RT1170, i.MX93, SAM E54 | `zephyr-telemetry-template.json` | RW612, MCXN947, RT1170, SAM E54 ([Releases](https://github.com/avnet-iotconnect/iotc-zephyr-demos/releases)) |
| [telemetry](demos/telemetry) | Portable periodic telemetry with device vitals | RW612, MCXN947, RT1170, i.MX93, SAM E54 | `zephyr-telemetry-template.json` | RW612 |
| [c2d-led](demos/c2d-led) | Cloud-to-device commands driving the board LED | RW612, MCXN947, RT1170, SAM E54 | `c2d-led-template.json` | RW612 |
| [click-telemetry](demos/click-telemetry) | Auto-detected MikroE Click sensors on the mikroBUS/Shuttle I2C bus | RW612, MCXN947 (+TF-M), SAM E54 | `click-demos-device-template.JSON` | RW612 |
| [eiq-pdm-vibration](demos/eiq-pdm-vibration) | eIQ-trained vibration classifier from a PDM microphone, with cloud fault injection | MCXN947 | `eiq-pdm-vibration-template.json` | — |
| [vision-occupancy](demos/vision-occupancy) | Camera + TFLM person detection with cloud snapshots and model push | RT1170 (OV5640 shield) | `vision-occupancy-template.json` | — |
| [gateway](demos/gateway) | i.MX93 gateway: UART child ingest, store-and-forward spool on eMMC | i.MX93 | `gateway-template.json` | — |
| [uart-telemetry-source](demos/uart-telemetry-source) | Radio-less boards emitting IOTCONNECT telemetry JSON over UART for a gateway | MCXE31B, MCXW72 | none (children of `gateway-template.json`, tag `uartsrc`) | — |
| [ml-model-update](demos/ml-model-update) | Cloud-pushed ML model updates | SAM E54 | `ml-model-update-template.json` | — |

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

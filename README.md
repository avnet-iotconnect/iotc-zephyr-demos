# iotc-zephyr-demos

Demonstration applications for connecting Zephyr RTOS devices to Avnet
/IOTCONNECT, built on the
[IOTCONNECT Zephyr SDK](https://github.com/avnet-iotconnect/iotc-zephyr-sdk).
A single codebase serves boards from multiple manufacturers: portable
demonstrations are written once and gain board support through devicetree
overlays, while manufacturer-specific demonstrations showcase capabilities
unique to a given silicon family.

## Getting started

The fastest path needs no Zephyr toolchain:

1. Pick your board on a manufacturer page below and open its quickstart
   guide ([HOST-SETUP.md](boards/HOST-SETUP.md) first if your machine has no
   flashing tool yet).
2. Flash a prebuilt image from the
   [Releases page](https://github.com/avnet-iotconnect/iotc-zephyr-demos/releases)
   for the demonstration you want to run.
3. Provision the device from its serial console following the quickstart —
   network credentials and the device identity are entered at the prompt,
   never compiled into the image. Import the demonstration's device template
   (from [templates/](templates/)) when creating the device in IOTCONNECT.

## Demonstrations by manufacturer

- [NXP](README_NXP.md)
- [Microchip](README_MICROCHIP.md)

Each page lists the supported boards, the demonstrations available for them,
verification status, and links to the board quickstart guides.

## Demonstration index

| Demonstration | What it shows |
|---|---|
| [quickstart](demos/quickstart) | Flash-and-provision baseline: on-device keygen, runtime identity, telemetry |
| [telemetry](demos/telemetry) | Portable periodic telemetry with device vitals |
| [c2d-led](demos/c2d-led) | Cloud-to-device commands driving the board LED |
| [click-telemetry](demos/click-telemetry) | Auto-detected MikroE Click sensors on the mikroBUS/Shuttle bus |
| [eiq-pdm-vibration](demos/eiq-pdm-vibration) | eIQ-trained vibration classifier from a PDM microphone |
| [vision-occupancy](demos/vision-occupancy) | Camera + TFLM person detection with cloud snapshots |
| [gateway](demos/gateway) | i.MX93 gateway: UART child ingest, store-and-forward spool |
| [uart-telemetry-source](demos/uart-telemetry-source) | Radio-less boards emitting telemetry JSON for a gateway |
| [ml-model-update](demos/ml-model-update) | Cloud-pushed ML model updates |
| [npu-benchmark](vendor/nxp/npu-benchmark) | eIQ Neutron NPU vs CPU inference benchmark |
| [face-detect](vendor/nxp/face-detect) | Camera face detection with LCD overlay and cellular uplink |

## Repository layout

| Directory | Contents |
|---|---|
| `demos/` | Portable demonstrations. Application code resides in the SDK's `samples/` tree; each demonstration folder contains the per-board configuration and documentation. Support for an additional board is added through a `boards/<board>.conf`/`.overlay` pair. |
| `vendor/<manufacturer>/` | Demonstrations built around manufacturer-specific silicon capabilities. |
| `boards/` | Per-board quickstart guides covering flashing and device provisioning, plus [HOST-SETUP.md](boards/HOST-SETUP.md) for preparing a Windows or Linux machine from scratch (LinkServer install, serial console). |
| `templates/` | IOTCONNECT device templates used by the demonstrations. |

## Building

The demonstrations build against Zephyr v4.4.1 with the SDK included as a
Zephyr module. Use this repository as a west manifest
(`west init -m <repository URL>`), or reference the modules from an existing
workspace:

```sh
west build -p always -b <board-target> -d build/<name> demos/<demo> \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```

Board targets and demonstration-specific notes are documented on the
manufacturer pages.

## Device identity

Most demonstrations provision device identity at runtime: the device
generates its own key and certificate on-chip and stores them in persistent
storage, so binaries contain no credentials. The provisioning flow is
described in each board quickstart.

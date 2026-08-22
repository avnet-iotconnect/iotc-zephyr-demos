# iotc-zephyr-demos

Demonstration applications for connecting Zephyr RTOS devices to Avnet
/IOTCONNECT, built on the
[IOTCONNECT Zephyr SDK](https://github.com/avnet-iotconnect/iotc-zephyr-sdk).
A single codebase serves boards from multiple manufacturers: portable
demonstrations are written once and gain board support through devicetree
overlays, while manufacturer-specific demonstrations showcase capabilities
unique to a given silicon family.

## Demonstrations by manufacturer

- [NXP](README_NXP.md)
- [Microchip](README_MICROCHIP.md)

Each page lists the supported boards, the demonstrations available for them,
verification status, and links to the board quickstart guides.

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

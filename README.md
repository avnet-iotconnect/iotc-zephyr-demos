# iotc-zephyr-demos

Demos for connecting Zephyr RTOS devices to Avnet /IOTCONNECT, built on the
[IOTCONNECT Zephyr SDK](https://github.com/avnet-iotconnect/iotc-zephyr-sdk).
One codebase covers boards from multiple manufacturers: portable demos are
written once and gain boards through devicetree overlays, and each
manufacturer also gets demos built around its specific silicon.

## Start with your manufacturer

- **[NXP](README_NXP.md)** — FRDM-MCXN947, MIMXRT1170-EVKB, FRDM-IMX93,
  FRDM-MCXE31B, FRDM-MCXW72
- **[Microchip](README_MICROCHIP.md)** — SAM E54 Xplained Pro

Each page lists the boards, which demos run on them, their verification
status, and the board quickstart guides.

## How the repository is organized

| Directory | Contents |
|---|---|
| `demos/` | Portable demos. The application code lives in the SDK's `samples/`; each demo folder holds the per-board configuration and the walkthrough docs. Adding a board means adding a `boards/<board>.conf`/`.overlay`, not forking the app. |
| `vendor/<manufacturer>/` | Demos built around manufacturer-specific silicon (for example the NXP NPU, or the MCXN947 camera pipeline). These do not port to other manufacturers. |
| `boards/` | Per-board quickstart guides: flash a prebuilt binary and provision from the serial prompt. |
| `templates/` | IOTCONNECT device templates used by the demos. |

## Building

The demos build against Zephyr v4.4.1 with the SDK as a Zephyr module. Either
use this repository as a west manifest (`west init -m <this repo>`), or point
an existing workspace at the modules:

```sh
west build -p always -b <board-target> -d build/<name> demos/<demo> \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```

Board targets and per-demo notes are on the manufacturer pages.

Device identity is provisioned at runtime on most demos: the device generates
its own key and certificate on-chip and stores them in flash, so binaries
contain no secrets. See any board quickstart for the flow.

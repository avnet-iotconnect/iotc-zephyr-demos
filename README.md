# iotc-zephyr-demos

Demonstration applications for connecting Zephyr RTOS devices to Avnet
/IOTCONNECT, built on the
[IOTCONNECT Zephyr SDK](https://github.com/avnet-iotconnect/iotc-zephyr-sdk).
A single codebase serves boards from multiple manufacturers: portable
demonstrations are written once and gain board support through devicetree
overlays, while manufacturer-specific demonstrations showcase capabilities
unique to a given silicon family.

## Documentation

| Guide | Use it to |
|---|---|
| **[QUICKSTART.md](QUICKSTART.md)** | Get a board onto /IOTCONNECT with a downloaded image — no toolchain. Flash, provision at the serial console, see live data. |
| **[DEMOS.md](DEMOS.md)** | Choose a demonstration: what each shows, its boards, its device template, and its documentation. |
| **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)** | Build from source: workspace setup, board targets, the two device-identity models, OTA firmware updates, adding boards, releasing images. |
| **[boards/HOST-SETUP.md](boards/HOST-SETUP.md)** | Prepare a bare Windows or Linux machine: flashing tool (LinkServer or J-Link) and serial console. |

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
| `boards/` | Per-board quickstart guides covering flashing and device provisioning, plus host setup. |
| `templates/` | IOTCONNECT device templates used by the demonstrations. |

## Device identity

Most demonstrations provision device identity at runtime: the device
generates its own key and certificate on-chip and stores them in persistent
storage, so binaries contain no credentials — prebuilt images on the
[Releases page](https://github.com/avnet-iotconnect/iotc-zephyr-demos/releases)
are safe to flash on any device and provision to any account. The
[developer guide](DEVELOPER_GUIDE.md#device-identity) covers both this and
the compiled-in identity model.

# /IOTCONNECT Zephyr Demonstrations for NXP

## Contents

- [FRDM-MCXN947](#frdm-mcxn947)
- [MIMXRT1170-EVKB](#mimxrt1170-evkb)
- [FRDM-RW612](#frdm-rw612)
- [FRDM-IMX93](#frdm-imx93)
- [FRDM-MCXE31B and FRDM-MCXW72](#frdm-mcxe31b-and-frdm-mcxw72)
- [Building](#building)

## Boards

### FRDM-MCXN947

Ethernet via the on-chip ENET-QoS MAC and the onboard LAN8741 PHY. Also the
board with the deepest coverage here, including a TrustZone/TF-M build
(`frdm_mcxn947/mcxn947/cpu0/ns`) where the device key is sealed in Protected
Storage and never appears in the binary.

Quickstart: [boards/frdm-mcxn947/QUICKSTART.md](boards/frdm-mcxn947/QUICKSTART.md)

| Demo | Status |
|---|---|
| [quickstart](demos/quickstart) | hardware-verified, including the TF-M sealed-key build |
| [telemetry](demos/telemetry) | hardware-verified |
| [c2d-led](demos/c2d-led) | builds |
| [click-telemetry](demos/click-telemetry) | hardware-verified with four Click sensors on a Shuttle, on the TF-M sealed-key build |
| [eiq-pdm-vibration](demos/eiq-pdm-vibration) | hardware-verified end to end (capture, eIQ training, on-device model, cloud fault injection) |
| [npu-benchmark](vendor/nxp/npu-benchmark) | builds; needs the eIQ Neutron artifacts |
| [face-detect](vendor/nxp/face-detect) | vision pipeline hardware-verified; cellular leg implemented, pending verification |

Note on face-detect: the camera configuration requires NXP's solder-jumper
rework, which disconnects the board's Ethernet; a reworked board should be
dedicated to the camera demonstrations. The demonstration also requires two
Zephyr driver patches, provided with apply instructions in
[vendor/nxp/face-detect/patches](vendor/nxp/face-detect/patches).

### MIMXRT1170-EVKB

100M Ethernet, enabled by default in the board devicetree. No board-specific
flashing procedure is required.

Quickstart: [boards/mimxrt1170-evkb/QUICKSTART.md](boards/mimxrt1170-evkb/QUICKSTART.md)

| Demo | Status |
|---|---|
| [quickstart](demos/quickstart) | hardware-verified (on-device keygen, identity in NVS) |
| [telemetry](demos/telemetry) | hardware-verified |
| [c2d-led](demos/c2d-led) | builds |
| [vision-occupancy](demos/vision-occupancy) | builds (OV5640 camera shield + TFLM person detection); pending hardware run |

click-telemetry needs a mikroBUS-to-Arduino adapter on this board plus an
`arduino_i2c` overlay — there is no mikroBUS socket.

### FRDM-RW612

The first Wi-Fi board in this repository: the RW612's onboard 802.11ax radio
is driven by Zephyr's in-tree NXP Wi-Fi driver, with the radio firmware blob
linked into the image (`west blobs fetch hal_nxp` once per workspace).
Nothing network- or device-specific is compiled in: Wi-Fi credentials are
entered at the shell (`wifi cred add`) and persist in flash, the SDK's
auto-connect associates from the stored credentials at boot, and the cloud
identity is provisioned on-device — so a single downloaded binary works on
any network and account, for every demo on this board. No devicetree overlay
is required — each demo adds Wi-Fi through a `boards/frdm_rw612.conf` alone.

Quickstart: [boards/frdm-rw612/QUICKSTART.md](boards/frdm-rw612/QUICKSTART.md)

| Demo | Status |
|---|---|
| [quickstart](demos/quickstart) | hardware-verified end to end over Wi-Fi (runtime `wifi cred` provisioning, on-device keygen, DHCP, discovery, MQTT telemetry) |
| [telemetry](demos/telemetry) | hardware-verified over Wi-Fi with the NVS-provisioned identity (credential-free binary) |
| [c2d-led](demos/c2d-led) | hardware-verified end to end over Wi-Fi, including cloud LED commands (credential-free binary) |

All three binaries are credential-free: provision Wi-Fi and identity once at
the console (see the quickstart) and every demo connects as the same device.
Prebuilt images for all three are on the
[Releases page](https://github.com/avnet-iotconnect/iotc-zephyr-demos/releases).

### FRDM-IMX93

Zephyr runs bare-metal on the Cortex-A55 (the M33 has no Ethernet), booted
from an SPSDK-built SD image, and connects over gigabit Ethernet. The
provisioned identity persists to the on-SOM eMMC so it survives power
cycles. Building needs the `aarch64-zephyr-elf` toolchain and `spsdk`.

Quickstart: [boards/frdm-imx93/QUICKSTART.md](boards/frdm-imx93/QUICKSTART.md)

| Demo | Status |
|---|---|
| [gateway](demos/gateway) | hardware-verified (UART ingest from a child device, store-and-forward spool on eMMC) |
| [quickstart](demos/quickstart) | hardware-verified |
| [telemetry](demos/telemetry) | hardware-verified |

### FRDM-MCXE31B and FRDM-MCXW72

Neither has an IP-capable radio or MAC (the E31B has no network hardware; the
W72 is 802.15.4-only), so both run
[uart-telemetry-source](demos/uart-telemetry-source): the device builds
IOTCONNECT telemetry JSON locally and emits it over UART for a gateway (such
as the i.MX93 gateway demo) to forward. Hardware-verified on both boards.
Flash the W72 with the `jlink` runner — its onboard debugger is J-Link OB.

## Building

```sh
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/telemetry demos/telemetry \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```

Board targets: `frdm_mcxn947/mcxn947/cpu0` (add `/ns` for TF-M),
`mimxrt1170_evk/mimxrt1176/cm7`, `frdm_rw612`, `frdm_imx93/mimx9352/a55`,
`frdm_mcxe31b`, `frdm_mcxw72/mcxw727c/cpu0`.

The FRDM-RW612 additionally needs the Wi-Fi firmware blob
(`west blobs fetch hal_nxp`). Wi-Fi credentials are not part of the build —
they are provisioned at the device shell and stored in flash.

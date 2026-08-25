# MIMXRT1170-EVKB Quickstart

<img src="media/mimxrt1170-evkb.jpg" width="480" alt="NXP MIMXRT1170-EVKB">

This guide provisions an NXP MIMXRT1170-EVKB to Avnet /IOTCONNECT using a
prebuilt binary. Device identity is generated on the device itself; no
credentials are stored on the host PC or compiled into the binary.

| | |
|---|---|
| SoC | i.MX RT1176 (Cortex-M7 at 1 GHz) |
| Build target | `mimxrt1170_evk/mimxrt1176/cm7` |
| Connectivity | Onboard 100 Mbit Ethernet, DHCP |
| Debug probe / console | Onboard MCU-Link (CMSIS-DAP), USB VCom at 115200 8N1 |

## Requirements

- MIMXRT1170-EVKB, a USB-C cable to the MCU-Link port, and an Ethernet
  connection with DHCP.
- An /IOTCONNECT account. Free trials are available:
  [via AWS Marketplace](https://github.com/avnet-iotconnect/avnet-iotconnect.github.io/blob/main/documentation/iotconnect/subscription/iotconnect_aws_marketplace.md)
  (60 days, AWS account required) or
  [via iotconnect.io](https://subscription.iotconnect.io/subscribe?cloud=aws)
  (30 days, no credit card). Check your spam folder for the temporary
  password after registering.
- A flashing tool: NXP LinkServer, MCUXpresso, or a J-Link. No Zephyr
  toolchain is required to use the prebuilt binary.
- A serial terminal at 115200 8N1.

Starting from a machine with neither installed? [HOST-SETUP.md](../HOST-SETUP.md)
helps you choose between LinkServer and J-Link, install either on Windows or
Linux, verify the probe, and open the serial console.

## Flashing

The RT1170 boots from external QSPI flash:

```sh
LinkServer flash MIMXRT1176xxxxx:MIMXRT1170-EVKB \
    load --addr 0x30000000 zephyr.bin
# or, from a Zephyr workspace:
west flash -d build/quickstart_rt1170
```

Download `mimxrt1170_evkb_quickstart.hex` from the
[Releases page](https://github.com/avnet-iotconnect/iotc-zephyr-demos/releases),
or build [demos/quickstart](../../demos/quickstart) from source — the
artifact lands in `build/<name>/zephyr/zephyr.hex` (or `.bin`).

## Provisioning

Open the MCU-Link VCom port. On first boot the device prints a provisioning
guide. Then:

1. Generate the device identity on the device. Choose a DUID (Device Unique ID) for the board — you invent this
   yourself: up to 10 characters, letters and digits only, and it must
   start with a letter (e.g. `rt1170a`). Then:
   ```
   iotcprov provision <your-duid>
   ```
   The device generates an EC P-256 key and self-signed certificate on-chip
   and prints the certificate.
2. In /IOTCONNECT, first import the device template for the demonstration
   you flashed — the template is fixed when the device is created. In
   [console.iotconnect.io](https://console.iotconnect.io): hover the
   processor icon, select **Device**, open the **Templates** tab, then
   **Create Template** → **Import** the file and save.

   <img src="../media/iotconnect/templates-button.png" width="510">
   <img src="../media/iotconnect/import-button.png" width="295">

   Templates for this board's demonstrations:

   | Demonstration | Template |
   |---|---|
   | quickstart, telemetry | [templates/zephyr-telemetry-template.json](../../templates/zephyr-telemetry-template.json) |
   | c2d-led | [templates/c2d-led-template.json](../../templates/c2d-led-template.json) |
   | vision-occupancy | [templates/vision-occupancy-template.json](../../templates/vision-occupancy-template.json) |

   Then open the **Devices** tab → **Create Device**: set the Unique ID
   (and Device Name) to your chosen DUID, pick the imported template,
   choose **Use my certificate**, paste the certificate the board
   printed (including the BEGIN/END lines), and click **Save and View**.

   <img src="../media/iotconnect/create-device-button.png" width="260">
   <img src="../media/iotconnect/use-my-cert.png" width="280">
   <img src="../media/iotconnect/save-and-view.png" width="600">
3. On the device's page, click the paper-and-cog icon (top right, above
   "Connection Info") to download `iotcDeviceConfig.json`, then paste
   its contents at the prompt:

   <img src="../media/iotconnect/paper-and-cog.png" width="165">

   ```
   iotc config
   { ...paste the JSON block... }
   ```
4. Reboot to connect:
   ```
   kernel reboot cold
   ```
   The board comes up as your device and begins streaming telemetry.

## Board notes

- No overlay is needed for Ethernet: the board devicetree enables the ENET MAC
  and PHY by default. The demo configuration enables the network stack and
  DHCP.
- Reflashing works directly with `west flash`; no recovery procedure is
  needed on this board.
- The stored identity lives in the flash storage partition and survives an
  application reflash.
- Key protection is software-only on this board. The RT1176 is a Cortex-M7
  without TrustZone-M, so there is no TF-M target and the device-generated key
  is stored in NVS. For hardware-sealed key storage, see the FRDM-MCXN947 and
  the SDK
  [key-protection matrix](https://github.com/avnet-iotconnect/iotc-zephyr-sdk/blob/main/docs/provisioning-nvs.md#key-protection--tf-m-capability-per-board).
- click-telemetry requires a mikroBUS-to-Arduino adapter; the board has no
  mikroBUS socket.

## Demonstrations for this board

See the demonstration list and verification status in
[README_NXP.md](../../README_NXP.md#mimxrt1170-evkb).

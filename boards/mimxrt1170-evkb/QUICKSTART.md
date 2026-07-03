# /IOTCONNECT Quickstart — MIMXRT1170-EVKB

Get an **NXP MIMXRT1170-EVKB** onto your /IOTCONNECT account in minutes: flash a
prebuilt binary, then provision from the serial prompt. The device generates its
**own** key on-chip — no build toolchain, no credentials on your PC.

| | |
|---|---|
| **SoC** | i.MX RT1176 (Cortex-M7 @1 GHz) |
| **Build target** | `mimxrt1170_evk/mimxrt1176/cm7` |
| **Connectivity** | Onboard 100 Mbit Ethernet (RJ45), DHCP |
| **Debug probe / console** | Onboard MCU-Link (CMSIS-DAP) + its USB VCom @115200 8N1 |

## 1. Requirements

- MIMXRT1170-EVKB, USB-C cable (to the MCU-Link port), and an **Ethernet cable**
  into the RJ45 with DHCP.
- A flashing tool: **NXP LinkServer** (or MCUXpresso / J-Link). No Zephyr
  toolchain needed to *use* the prebuilt binary.
- A serial terminal (Tera Term, PuTTY, `screen`) at **115200 8N1**.

## 2. Flash the binary

The RT1170 boots from external QSPI flash, so flash the image with LinkServer:

```sh
LinkServer flash MIMXRT1176xxxxx:MIMXRT1170-EVKB \
    load --addr 0x30000000 zephyr.bin
# or, from a Zephyr workspace:
west flash -d build/quickstart_rt1170
```

Prebuilt artifact: `build/quickstart_rt1170/zephyr/zephyr.bin` (or `.hex`) from
[demos/quickstart](../../demos/quickstart).

## 3. Provision from the prompt

Open the MCU-Link VCom (e.g. `COM43`). On first boot it prints a guide. Then:

```
iotcprov provision <your-duid>
```
→ the device generates an EC P-256 key + self-signed cert **on-chip** and prints
the certificate.

1. In /IOTCONNECT: **Devices → Create Device**, Unique ID = `<your-duid>`,
   **Self-Signed**, paste the printed certificate.
2. Download `iotcDeviceConfig.json` from the device's Info panel, then:
   ```
   iotc config
   { ...paste the json block... }
   ```
3. Connect:
   ```
   kernel reboot cold
   ```
   The board comes up as your device and streams telemetry.

## 4. Board notes

- **No overlay needed for Ethernet** — the RT1170-EVK's 100M ENET MAC + PHY are
  enabled by default; the quickstart's `boards/mimxrt1170_evk_mimxrt1176_cm7.conf`
  just turns on the L2 + DHCP.
- Reflashing works directly (`west flash`) — no debug-port recovery dance.
- NVS (the stored identity) lives in the `storage_partition` and **survives an
  application reflash**.
- **Key protection: software only (no TF-M).** The RT1170 is Cortex-M7 — no
  TrustZone-M — so there is no TF-M target and no hardware-sealed key store. The
  device-generated key lives in NVS. For hardware-backed key protection use a
  TF-M board (e.g. FRDM-MCXN947). See the SDK
  [key-protection matrix](../../../iotc-zephyr-sdk/docs/provisioning-nvs.md#key-protection--tf-m-capability-per-board).

## 5. Demos for this board

Telemetry ✅ · c2d-led ✅ · quickstart ✅ — see the
[board × demo matrix](../../README.md#board--demo-support-matrix). click-telemetry
needs a mikroBUS-to-Arduino Click adapter (RT1170 has no mikroBUS socket).

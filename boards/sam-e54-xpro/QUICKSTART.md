# /IOTCONNECT Quickstart — SAM E54 Xplained Pro

Get a **Microchip SAM E54 Xplained Pro** onto your /IOTCONNECT account in
minutes: flash a prebuilt binary, then provision from the serial prompt. The
device generates its **own** key on-chip — no build toolchain, no credentials
on your PC.

| | |
|---|---|
| **SoC** | ATSAME54P20A (Cortex-M4F @120 MHz, 1 MB flash, 256 KB RAM) |
| **Build target** | `same54_xpro` (legacy Atmel tree — see Board notes) |
| **Connectivity** | Onboard 10/100 Ethernet (GMAC + KSZ8091 PHY, RJ45), DHCP |
| **Debug probe / console** | Onboard EDBG (CMSIS-DAP) + its USB VCom @115200 8N1 |

## 1. Requirements

- SAM E54 Xplained Pro (ATSAME54-XPRO), micro-USB cable (to the **DEBUG USB**
  port), and an **Ethernet cable** into the RJ45 with DHCP.
- A flashing tool: **OpenOCD** (`west flash` default runner, drives the onboard
  EDBG) or a J-Link on the 10-pin Cortex header. No Zephyr toolchain needed to
  *use* the prebuilt binary.
- A serial terminal (Tera Term, PuTTY, `screen`) at **115200 8N1**.

## 2. Flash the binary

```sh
west flash -d build/quickstart_e54
# or with a raw hex + OpenOCD:
openocd -f board/microchip_same54_xplained_pro.cfg \
    -c "program zephyr.hex verify reset exit"
```

Prebuilt artifact: `build/quickstart_e54/zephyr/zephyr.hex` from
[demos/quickstart](../../demos/quickstart).

## 3. Provision from the prompt

Open the EDBG VCom. On first boot it prints a guide. Then:

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

- **No overlay needed for Ethernet** — the board devicetree enables
  gmac/mdio/phy by default, and the **factory MAC address** comes from the
  onboard AT24MAC402 EEPROM (nvmem cell), so every board has a stable, unique
  MAC out of the box. The demos' `boards/same54_xpro.conf` just turns on the
  L2 + DHCP.
- **Two Zephyr targets, one board.** Zephyr v4.4.1 carries both the legacy
  Atmel-tree `same54_xpro` (working Ethernet — what we use) and Microchip's
  new-tree `sam_e54_xpro` (their active investment, but **no GMAC node yet**).
  Migrate the confs when Microchip's tree gains Ethernet; the hardware is the
  same. The demos manifest pulls `hal_atmel` for this target.
- NVS (the stored identity) lives in the `storage_partition` (last 16 KiB of
  flash) and **survives an application reflash**.
- **Key protection: software only (no TF-M).** The E54 is Cortex-M4F — no
  TrustZone-M — so the device-generated key lives in NVS. For hardware-backed
  key protection use a TF-M board (e.g. FRDM-MCXN947). See the SDK
  [key-protection matrix](../../../iotc-zephyr-sdk/docs/provisioning-nvs.md#key-protection--tf-m-capability-per-board).
- **Sensor wing goes on EXT1.** EXT1's I²C is **SERCOM3** (PA22/PA23) — a
  private bus; the demo overlays expose it as `mikrobus_i2c` / `io1-i2c`.
  (EXT2/EXT3 instead share **SERCOM7** with the AT24MAC402 EEPROM @0x5E and
  the ATECC508 @0x60 — usable, but mind the addresses.) Two wing options:
  - **IO1 Xplained Pro** (Atmel-42078): AT30TSE758 temp @0x4F (auto-detected
    by click-telemetry), TEMT6000 light → EXT1 pin 3 = ADC1/AIN6, LED on
    EXT1 pin 7 = PB08 (active low). This is the **ml-model-update** demo's
    sensor set.
  - **mikroBUS Xplained Pro adapter (ATMBUSADAPTER-XPRO)** for MikroE
    Clicks/Shuttle.

## 5. Demos for this board

quickstart 🔨 · telemetry 🔨 · c2d-led 🔨 · click-telemetry 🔨 (IO1 or Click
adapter on EXT1) · **ml-model-update 🔨 (IO1 on EXT1 — cloud-pushed model
swap)** — all build-verified for `same54_xpro`, awaiting hardware.
See the [board × demo matrix](../../README.md#board--demo-support-matrix).

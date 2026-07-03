# /IOTCONNECT Quickstart — FRDM-MCXN947

Get an **NXP FRDM-MCXN947** onto your /IOTCONNECT account in minutes: flash a
prebuilt binary, then provision from the serial prompt. The device generates its
**own** key on-chip — no build toolchain, no credentials on your PC.

| | |
|---|---|
| **SoC** | MCXN947 (dual Cortex-M33) |
| **Build target** | `frdm_mcxn947/mcxn947/cpu0` |
| **Connectivity** | Onboard Ethernet (ENET-QoS + Microchip LAN8741 PHY + RJ45), DHCP |
| **Debug probe / console** | Onboard MCU-Link (CMSIS-DAP) + its USB VCom @115200 8N1 |

## 1. Requirements

- FRDM-MCXN947, USB-C cable (to the MCU-Link port), and an **Ethernet cable**
  into the RJ45 with DHCP.
- A flashing tool: **NXP LinkServer** (or MCUXpresso / J-Link). No Zephyr
  toolchain needed to *use* the prebuilt binary.
- A serial terminal (Tera Term, PuTTY, `screen`) at **115200 8N1**.

## 2. Flash the binary

```sh
LinkServer flash MCXN947:FRDM-MCXN947 load --addr 0x10000000 zephyr.bin
# or, from a Zephyr workspace:
west flash -d build/quickstart_n947
```

Prebuilt artifact: `build/quickstart_n947/zephyr/zephyr.hex` (or `.bin`) from
[demos/quickstart](../../demos/quickstart).

## 3. Provision from the prompt

Open the MCU-Link VCom (e.g. `COM41`). On first boot it prints a guide. Then:

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

- **Flash-recovery quirk:** after the app runs, the MCXN947's debug MEM-AP can
  become unreachable, so a plain reflash may fail
  (`Cannot find MEM-AP … / Flash operation exited with code 1`). Fix: **unplug/
  replug the MCU-Link USB (full power-cycle), then flash within a few seconds.**
- Ethernet is brought up by the board overlay
  (`boards/frdm_mcxn947_mcxn947_cpu0.overlay` enables enet_mac/enet_mdio/phy).
- NVS (the stored identity) **survives an application reflash**.
- **Key protection: TF-M capable.** The MCXN947 is Cortex-M33 with TrustZone-M
  and an EdgeLock secure subsystem, and Zephyr provides a TF-M target
  (`frdm_mcxn947/mcxn947/cpu0/ns`). Built that way with
  `CONFIG_IOTCONNECT_USE_PSA_PROTECTED_STORAGE`, the device key is sealed in
  **hardware-backed PSA Protected Storage** — the strongest posture. See the SDK
  [key-protection matrix](../../../iotc-zephyr-sdk/docs/provisioning-nvs.md#key-protection--tf-m-capability-per-board).

## Hardware-backed key protection (TF-M build) — ✅ HW-verified

The MCXN947 has a Zephyr **TF-M** target, so the device key is sealed in
**hardware-backed PSA Protected Storage** instead of plaintext NVS. This path is
**hardware-verified end-to-end**: the `/ns` device provisions itself
(`iotcprov keygen`/`provision` — EC P-256 on-chip), seals the identity in TF-M
PS, survives reboot/reflash, and connects with mutual-TLS MQTT to AWS IoT Core
(discovery → identity → broker), all with crypto in the TF-M secure world.

```sh
# one-time host tooling for the TF-M build:
pip install cryptography cbor2 pyyaml jinja2 click imgtool

# one-time: apply the module edits for the TF-M /ns build (see
# iotc-zephyr-sdk/patches/README.md). Re-apply after any `west update`.
# (1) required TLS-handshake fix, and (2) the secure/NS RAM rebalance the /ns
# config needs (secure 160 KB / NS 160 KB). Flash-only users of the prebuilt
# tfm_merged.hex need none of this -- the boundary is baked into the image.
(cd <zephyrproject>/modules/crypto/mbedtls && \
   git apply <path>/iotc-zephyr-sdk/patches/mbedtls-ssl-premaster-ecp-max-bytes.patch)
(cd <zephyrproject>/modules/tee/tf-m/trusted-firmware-m && \
   git apply <path>/iotc-zephyr-sdk/patches/tfm-frdmmcxn947-ram-rebalance-region-defs.patch)
(cd <zephyrproject>/zephyr && \
   git apply <path>/iotc-zephyr-sdk/patches/zephyr-frdmmcxn947-ns-ram-rebalance-dts.patch)

west build -p always -b frdm_mcxn947/mcxn947/cpu0/ns demos/quickstart \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
# flash the merged secure+non-secure image:
west flash -d build/<name>          # uses build/<name>/zephyr/tfm_merged.hex
```

This build sets `CONFIG_BUILD_WITH_TFM=y`. The device identity (cpid/env/duid +
cert/key) is stored as a **single packed PSA Protected Storage asset**
(`iotc_identity.c` `CONFIG_BUILD_WITH_TFM` path) — the TF-M PS filesystem
reserves a full slot per asset and only allows a few, so one blob is used. Live
TLS credentials use the **VOLATILE** backend (loaded from the sealed blob into
RAM for the handshake; not the tiny PS). The `/ns` conf trims the mbedTLS heap /
TLS out-buffer and sizes the k-heap (`HEAP_MEM_POOL_SIZE`, used by iotc-c-lib +
cJSON) and RX pool to fit the 128 KB non-secure RAM partition — this is tight;
see the comments in `boards/frdm_mcxn947_mcxn947_cpu0_ns.conf`.

> **Module edits are needed to build the TF-M `/ns` image from source** (not for
> non-TF-M boards, and not for flash-only users of the prebuilt binary): a
> PSA-only-ECDHE premaster-buffer fix in mbedTLS, plus a secure/non-secure RAM
> rebalance (the `/ns` config uses the full 160 KB NS partition; `click-telemetry`
> uses the same headroom). See
> [iotc-zephyr-sdk/patches/README.md](../../../iotc-zephyr-sdk/patches/README.md).
> Re-apply after any `west update`.

## 5. Demos for this board

Telemetry ✅ · c2d-led ✅ (LED verified) · quickstart ✅ 🔒 · click-telemetry
✅ 🔒 (mikroBUS Click sensors) · NPU benchmark 🔨 — see the
[board × demo matrix](../../README.md#board--demo-support-matrix).

🔒 = **hardware-verified on the TF-M sealed-key `/ns` build**. Both the quickstart
and **click-telemetry** run as `frdm_mcxn947/mcxn947/cpu0/ns` with the device key
sealed in TF-M Protected Storage: provision once with the quickstart flow, then
either image loads that sealed identity. click-telemetry `/ns` was verified
streaming 4 Clicks (Temp&Hum 14, Altitude 4, Ultra-Low Press, Air quality 7) to
AWS IoT Core every 10 s. Build it with `-b frdm_mcxn947/mcxn947/cpu0/ns` (same
module edits as above); see
[demos/click-telemetry](../../demos/click-telemetry/README.md).

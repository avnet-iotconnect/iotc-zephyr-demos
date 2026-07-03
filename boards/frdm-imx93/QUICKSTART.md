# /IOTCONNECT Quickstart — FRDM-IMX93 (Cortex-A55, Zephyr)

The **NXP FRDM-IMX93** is an application processor: **2× Cortex-A55** (which run
Linux) + **1× Cortex-M33** companion. For a Zephyr /IOTCONNECT **IP client** the
target is the **A55** — that's where the Ethernet MAC and Zephyr `net` support
live (the M33 has no Ethernet). Zephyr runs **bare-metal on the A55 from DRAM**.

| | |
|---|---|
| **SoC** | i.MX93 (MIMX9352: 2× Cortex-A55 + 1× Cortex-M33) |
| **Build target** | `frdm_imx93/mimx9352/a55` (arm64) |
| **Connectivity** | Ethernet — ENET MAC (RGMII) + Motorcomm **YT8521** PHY, DHCP |
| **Console** | `lpuart2` @115200 8N1 (the debug USB exposes A55 + M33 COM ports) |
| **Debug/flash** | SEGGER **J-Link** (`--runner jlink`) to the A55, or an SPSDK SD boot image |

> **Status: ✅ HARDWARE-VERIFIED (both demos).** Booting an SPSDK SD image, Zephyr
> on the A55 brings up **gigabit** Ethernet (YT8521 PHY), runs DRA → mutual-TLS
> MQTT, and **connects to AWS IoT Core**, telemetry delivered. Console is
> `lpuart2`.
> - **telemetry** — baked-in creds, connects (`duid=mclMCXNzeph`).
> - **quickstart** — on-device EC P-256 keygen + `iotcprov provision`, identity
>   **persisted to the on-SOM eMMC** (survives power-cycle, auto-connects as
>   `duid=mcl93zep`). See [§5](#5-quickstart-variant--runtime-provisioning--emmc-persistence).
>
> Note: the FRDM-IMX93 has two RJ45 jacks — use the ENET one, and two debug COM
> ports — SPL/ATF print on one, Zephyr's console on `lpuart2`.

## 1. One-time toolchain

The A55 is **arm64**, so it needs the `aarch64-zephyr-elf` toolchain (the MCX
boards use `arm-zephyr-eabi`). Add it to your Zephyr SDK:

```sh
west sdk install -t aarch64-zephyr-elf -d <your-zephyr-sdk-dir>
```

## 2. Build

```sh
west build -p always -b frdm_imx93/mimx9352/a55 -d build/imx93a55 \
  demos/telemetry \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```

The board files:
- [`boards/frdm_imx93_mimx9352_a55.conf`](../../demos/telemetry/boards/frdm_imx93_mimx9352_a55.conf)
  — NXP ENET driver + L2 + DHCP; drops NVS/SETTINGS/FLASH (bare-metal A55 has no
  NOR flash), so identity comes from the compiled-in creds header.
- [`boards/frdm_imx93_mimx9352_a55.overlay`](../../demos/telemetry/boards/frdm_imx93_mimx9352_a55.overlay)
  — extends the Zephyr DRAM window from the 1 MB board default (too small for
  arm64 + net + TLS) to 16 MB.

## 3. Load & run (A55 runs from DRAM)

Unlike the self-contained MCX boards, the A55 image runs from **DRAM @
0xd0000000**, so DDR must be initialized **before** the image is loaded. Two
paths:

**A) J-Link to DRAM (dev loop)** — boot the board through its normal SD boot to
the **U-Boot** prompt (SPL/U-Boot initializes DDR), then load Zephyr into DDR:

```sh
west flash -d build/imx93a55 --runner jlink   # --flash-sram --no-reset: loads ELF to RAM
```

(or from U-Boot: `bootelf` the image). The J-Link runner does **not** program
flash — it loads into the already-initialized DDR and runs.

**B) SPSDK SD boot image (standalone — no Linux)** ✅ *build verified* — package
Zephyr with the NXP i.MX93 boot firmware into a self-booting `flash.bin` (ELE
AHAB container + LPDDR training firmware + U-Boot SPL + ATF + the Zephyr A55
image) and write it to the SD at the **32 KB** boot offset. The ROM loads it, SPL
brings up DDR, ATF jumps to Zephyr — no Linux/U-Boot on the card.

```sh
pip install spsdk                                   # provides nxpimage
# fetch just the i.MX93 boot firmware blob (regex must match the full path):
west blobs fetch hal_nxp -l ".*imx93evk.*" -a
# build with the SPSDK image post-step (needs 7-Zip on PATH):
USE_NXP_SPSDK_IMAGE=y west build -p always -b frdm_imx93/mimx9352/a55 -d build/imx93a55 \
  demos/telemetry -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
                     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
# -> build/imx93a55/zephyr/flash.bin
```

Write `flash.bin` to the SD at 32 KB. On Linux: `sudo dd if=flash.bin of=/dev/sdX
bs=1k seek=32 conv=fsync`. On Windows (Admin PowerShell), clear the card then raw-
write at offset 32768 — **the write length must be sector-aligned** (pad to a
4 KB multiple) or the raw write fails with an "IO operation will not work" error.
`west flash --runner spsdk` instead loads over **USB serial-download** (board in
download mode), no SD needed.

Set the board's boot switches to **SD**, insert the card, power on. Open the A55
COM port (`lpuart2`) at 115200; it runs the same DRA → mutual-TLS MQTT → AWS IoT
Core telemetry loop as the other boards.

## 4. Caveats (for the hardware bring-up)

- **DRAM carveout:** the overlay's `0xd0000000 + 16 MB` must fit the DDR region
  your Linux/U-Boot reserves for the M33/Zephyr companion (reserved-memory node)
  so Zephyr doesn't collide with Linux. Adjust to match your BSP.
- **Entropy for TLS:** the build uses the generic entropy path; confirm a real
  RNG source (the i.MX93 **ELE/EdgeLock** secure enclave) is wired on hardware —
  TLS needs genuine entropy, not a test generator.
- **Identity:** the **telemetry** demo has provisioning/NVS off — cert/key come
  from the compiled-in `device_credentials.h` (generate it with
  `creds/gen_creds_header.py`). The **quickstart** demo (§5) instead does
  on-device keygen and persists the identity to the **eMMC** (no baked creds). A
  hardware-**sealed** key here would be an ELE effort, separate from the TF-M
  sealed key on the MCXN947 — the eMMC store is plaintext reserved sectors.
- **Linux/KVS alternative:** the i.MX93's flagship IOTCONNECT use is **Linux on
  the A55** (Python/Lite SDK + KVS/WebRTC video) — a separate, deferred track.
  This Zephyr-on-A55 path is the direct-IP telemetry/control client.

## 5. Quickstart variant — runtime provisioning + eMMC persistence

The **quickstart** demo (`demos/quickstart`) is the *releasable* image: no
compiled-in device identity (only public CA roots), on-device EC P-256 keygen,
and an `iotcprov`/`iotc` provisioning shell. Anyone can write this one `flash.bin`
to an SD, provision it to **their own** IOTCONNECT account, and it connects — no
toolchain, no recompile. **Hardware-verified** on the FRDM-IMX93 A55.

### Where the identity is stored (and why not the SD)

The bare-metal A55 has no NOR flash and no NVS, so the provisioned identity is
written to **raw reserved sectors on a block device**. The obvious choice — the
SD we boot from (uSDHC2) — **does not work**: the board doc states that when the
ROM boots from the uSDHC2 SD card, that controller must not also be driven from
Zephyr. In practice the card *initializes* fine (clock/CMD8/ACMD41/CID/CSD all
succeed once the root clock is set), but the **first data-line transfer stalls**
(ADMA never completes → ~10 s timeout → `-116`). So the identity is persisted to
the **on-SOM eMMC on uSDHC1** (disk `SD2`) — a separate controller the boot ROM
doesn't touch. It comes up at 1.8 V / HS200 (200 MHz) and reads/writes cleanly.

> The standalone SPSDK boot (ROM → SPL → ATF → Zephyr, no U-Boot) also leaves the
> uSDHC **root clocks** unconfigured, so the SDK configures them in the identity
> layer before first block access (SysPll1Pfd1 / 2 = 400 MHz for both uSDHC1/2).
> This is handled automatically for `CONFIG_SOC_MIMX9352`.

Board config for the quickstart (see
[`demos/quickstart/boards/frdm_imx93_mimx9352_a55.{conf,overlay}`](../../demos/quickstart/boards/)):

```conf
CONFIG_IOTCONNECT_IDENTITY_DISK=y
CONFIG_IOTCONNECT_IDENTITY_DISK_NAME="SD2"       # eMMC on uSDHC1
CONFIG_IOTCONNECT_IDENTITY_DISK_SECTOR=20000000  # ~10 GB in, clear of any factory image
CONFIG_DISK_ACCESS=y
CONFIG_DISK_DRIVER_MMC=y
CONFIG_MMC_STACK=y
CONFIG_SDHC=y
```

The overlay enables `&usdhc1` (eMMC) and its `sdmmc { }` child; it does **not**
enable uSDHC2 for Zephyr. 16 sectors (8 KB) at sector 20,000,000 hold the packed
`{cpid, env, duid, cert, key}`. Because the demo boots from the SD, the eMMC user
area is otherwise unused here — but note this **overwrites** any factory eMMC
content at that offset (the board still boots from the SD regardless).

### Build

Same as §2/§3 but point at `demos/quickstart` and build the SPSDK image:

```sh
# module paths can be passed as -D or (to dodge shell quoting of C:/… on Windows)
# as environment variables of the same name:
export ZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk
export ZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
export USE_NXP_SPSDK_IMAGE=y          # + 7-Zip and `spsdk` on PATH
west build -p always -b frdm_imx93/mimx9352/a55 -d build/imx93a55_qs demos/quickstart
# -> build/imx93a55_qs/zephyr/flash.bin   (write to the SD at 32 KB, as in §3)
```

### Provision & run (on the device)

Boot the SD image; it comes up **unprovisioned** and prints the guide. Then:

```text
uart:~$ iotcprov provision <your-duid>
  -> generates an EC P-256 key + self-signed cert ON the device,
     persists {duid,cpid,env,cert,key} to the eMMC, and prints the cert.
# In IOTCONNECT: Devices -> Create Device, Auth = Self-Signed, paste that cert.
# Download iotcDeviceConfig.json from the device's Info panel, then:
uart:~$ iotc config
  { ...paste the JSON block... }        # sets cpid/env/duid, also persisted
# Reboot to connect. `kernel reboot cold` has no effect on this bare-metal A55
# (no PSCI/software-reset path), so just POWER-CYCLE the board:
```

On the next power-up the SDK reads the identity back from the eMMC, comes up
**provisioned**, and connects to IOTCONNECT/AWS IoT Core automatically — no
re-provisioning. Verified end-to-end (`duid=mcl93zep`, MQTT connected, C2D
subscribed).

# iotc-zephyr-demos

Use-case demos for the [IOTCONNECT Zephyr SDK](../iotc-zephyr-sdk).

The demos are **use-case-first** (one app, many boards): a demo lives under
`demos/<use-case>/` with a portable `src/main.c`, and a board is "supported" by
adding a `boards/<board>.{conf,overlay}` — not a fork. This is the Zephyr
"write once, run on many boards" model. Each demo folder carries its own README
(requirements → build → flash → onboard → run), and the **board × demo support
matrix** below is the at-a-glance capability grid. Inherently vendor-specific
showcases live under `vendor/<name>/`.

## Board × demo support matrix

Legend: ✅ verified on hardware · 🔒 verified on the **TF-M hardware-sealed-key**
(`/ns`) build · 🔨 builds (not yet hardware-tested) · 🔜 planned · — not applicable

| Demo | `frdm_mcxn947`<br>MCXN947 | `mimxrt1170_evk`<br>RT1170-EVKB | `frdm_imx93`<br>i.MX93 (A55) | `frdm_mcxe31b`<br>MCXE31B | `frdm_mcxw72`<br>MCXW72 | `same54_xpro`<br>SAM E54 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|
| **quickstart** | ✅ 🔒 sealed key⁴ | ✅ connects | ✅ A55 · eMMC persist⁵ | — | — (no IP link)² | 🔨 builds⁶ |
| **telemetry** | ✅ connects | ✅ connects | ✅ A55 · SD boot⁵ | —¹ | —² | 🔨 builds⁶ |
| **uart-telemetry-source** | — | — | — | ✅ M7¹ | ✅ M33² | — |
| **c2d-led** | ✅ LED verified | 🔨 builds | 🔜⁵ | — (no IP link) | — (no IP link)² | 🔨 builds⁶ |
| **click-telemetry** | ✅ 🔒 sealed key⁴ | ◇ adapter³ | 🔜⁵ | ⇢ separate probe¹ | — | 🔨 IO1 wing⁶ |
| **ml-model-update** | — | — | — | — | — | 🔨 builds⁶ |
| **npu-benchmark** (vendor) | 🔨 builds | — | — | — | — | — |

¹ MCXE31B (Cortex-M7) has no Ethernet/Wi‑Fi → it runs the in-repo
**[uart-telemetry-source](demos/uart-telemetry-source)** demo (**hardware-verified**):
iotc-c-lib builds IOTCONNECT 2.1 JSON on-device and emits it on the console UART
(LPUART5) for a gateway to forward, rather than connecting directly.
² MCXW72 (Cortex-M33) is 802.15.4-only (no Eth/Wi‑Fi) → same in-repo
**[uart-telemetry-source](demos/uart-telemetry-source)** demo (**hardware-verified**
emitting IOTCONNECT 2.1 JSON), feeding a gateway. A **direct** IP path is planned
via a **cell-modem Click** bearer (the Thread → border-router route is the
wireless alternative). Onboard debugger is **J-Link OB**, so flash with the
`jlink` runner (LinkServer sees no probe).
³ RT1170-EVKB has no mikroBUS socket; click-telemetry needs an Arduino-header
I²C overlay (`arduino_i2c` = `lpi2c5`) + a mikroBUS-to-Arduino Click adapter.
⁴ MCXN947 has a Cortex-M33 TrustZone/EdgeLock TF-M target
(`frdm_mcxn947/mcxn947/cpu0/ns`). Built that way the device key is **sealed in
TF-M Protected Storage** (never in the binary) and crypto runs in the secure
world. Quickstart (on-device keygen + connect) and click-telemetry (4 Clicks →
AWS IoT Core) are both **hardware-verified** on this sealed-key path. See the
[N947 quickstart](boards/frdm-mcxn947/QUICKSTART.md) and
[patches](../iotc-zephyr-sdk/patches/README.md).
⁵ i.MX93 IP lives on the **Cortex-A55** (arm64), not the M33 (which has no
Ethernet). Zephyr runs bare-metal on the A55 and is **hardware-verified**:
booting an **SPSDK SD image** (ELE + LPDDR firmware + SPL + ATF + Zephyr, no
Linux), it connects over **gigabit** Ethernet to AWS IoT Core. Both demos are
verified on HW: **telemetry** (baked creds) and **quickstart** — on-device EC
P-256 keygen + `iotcprov provision`, with the generated identity **persisted to
the on-SOM eMMC** (uSDHC1, disk `SD2`) so it survives a power-cycle and
auto-connects. (The SPSDK boot uses uSDHC2 for the SD card; per the board doc the
boot controller can't also be driven from Zephyr, so identity lives on the eMMC —
a separate controller. There is no hardware-sealed key here — that's the M33/TF-M
story on the MCXN947; an A55 sealed key would be an ELE effort.) Build needs the
`aarch64-zephyr-elf` toolchain + `spsdk`; console is `lpuart2`. See the
[i.MX93 quickstart](boards/frdm-imx93/QUICKSTART.md). The chip's flagship
IOTCONNECT path is still Linux/A55 + KVS (deferred).
⁶ **First Microchip board.** SAM E54 Xplained Pro (ATSAME54P20A, Cortex-M4F
@120 MHz) — all four demos **build-verified** for the legacy Atmel-tree
`same54_xpro` target (working GMAC Ethernet + factory MAC from the onboard
AT24MAC402); hardware validation pending. Zephyr also carries Microchip's
new-tree port of this board (`sam_e54_xpro`) — their active Zephyr investment,
but **no Ethernet node yet**; migrate when GMAC lands. The sensor wing goes on
**EXT1** (its I²C is SERCOM3, a private bus; EXT2/EXT3 share SERCOM7 with the
onboard EEPROM/ATECC508): either the **IO1 Xplained Pro** (AT30TSE758 temp
auto-detected by click-telemetry; light + LED used by **ml-model-update**) or
a mikroBUS Xplained Pro adapter (ATMBUSADAPTER-XPRO) for Clicks. See the
[E54 quickstart](boards/sam-e54-xpro/QUICKSTART.md).
The Microchip **BLE tier** (WBZ451HPE Curiosity beacon → SAMA7D65 Curiosity
gateway) is deferred until Microchip's WBZ45x Zephyr support lands (tracked in
their Zephyr re-org RFC, zephyr#92168) — no PIC32CX-BZ2 Zephyr support exists
today, and Zephyr's `sama7d65_curiosity` has no networking yet.

**Connectivity per board** (why the matrix looks the way it does):
MCXN947 = Ethernet (ENET-QoS + LAN8741) · RT1170-EVKB = Ethernet (100M ENET,
on by default) · i.MX93 = Ethernet on the **Cortex-A55** (RGMII + YT8521 PHY;
Zephyr runs bare-metal there — the M33 has no Ethernet; Linux/A55 + KVS is the
deferred streaming tier) · MCXE31B = none (UART source) · MCXW72 = 802.15.4 only ·
SAM E54 Xpro = Ethernet (GMAC + KSZ8091, factory MAC from EEPROM).

## Per-board quickstarts

Each supported board has its own flash-and-provision guide (every new board
adds one under `boards/<vendor>-<board>/QUICKSTART.md`):

- [MIMXRT1170-EVKB](boards/mimxrt1170-evkb/QUICKSTART.md)
- [FRDM-MCXN947](boards/frdm-mcxn947/QUICKSTART.md) (incl. TF-M sealed-key `/ns`)
- [FRDM-IMX93](boards/frdm-imx93/QUICKSTART.md) (Zephyr on the Cortex-A55)
- [SAM E54 Xplained Pro](boards/sam-e54-xpro/QUICKSTART.md) (first Microchip board)

## Demos

| Demo | Path | What it shows |
|------|------|---------------|
| **Quickstart** ⭐ | [demos/quickstart](demos/quickstart) | Flash-and-provision binary: no toolchain, no baked-in creds. Device generates its **own** key+cert on-chip (`iotcprov provision`), you register it, paste `iotcDeviceConfig.json` (`iotc config`), and it connects. Only public CAs compiled in. |
| Telemetry (reference) | [demos/telemetry](demos/telemetry) | Connect + periodic telemetry over MQTT/TLS. Reuses the SDK sample. |
| UART telemetry source | [demos/uart-telemetry-source](demos/uart-telemetry-source) | **Constrained tier** — for MCUs with no IP stack (MCXE31B, MCXW72). iotc-c-lib builds IOTCONNECT 2.1 JSON on-device and prints it on the console UART for a gateway to forward. Board name via `CONFIG_BOARD`; no SDK/network module. |
| c2d-led | [demos/c2d-led](demos/c2d-led) | Cloud→device commands (`led-on/off/toggle`) drive the board LED, with ACKs. |
| Click telemetry | [demos/click-telemetry](demos/click-telemetry) | Auto-detect MikroE Click sensors on a Shuttle → nested-object telemetry + C2D commands (LED, reporting interval, reboot). Device template: [templates/click-demos-device-template.JSON](templates/click-demos-device-template.JSON). |
| NXP eIQ Neutron NPU benchmark | [vendor/nxp/npu-benchmark](vendor/nxp/npu-benchmark) | NPU-vs-CPU inference timing → IOTCONNECT. Needs the eIQ/Neutron artifacts. |
| eIQ PdM (vibration) | [demos/eiq-pdm-vibration](demos/eiq-pdm-vibration) | Predictive maintenance: ML Vibro Sens Click (NXP FXLS8974 + onboard fault motors) → **eIQ Time Series Studio model on-device** → IOTCONNECT, with cloud fault-injection. Pretrained model + dataset bundled; HW-verified end-to-end. [Quickstart](demos/eiq-pdm-vibration/QUICKSTART.md). |
| **ML model update** | [demos/ml-model-update](demos/ml-model-update) | **The model is data, not firmware**: a fixed tiny-MLP engine + a validated ~124 B IOTM weights blob. IOTCONNECT pushes a NEW model as a single C2D command (chunked path for bigger blobs) → CRC-checked, hot-swapped, NVS-persisted — device behavior changes with no reflash. IO1 Xplained Pro sensors (temp+light) + LED. Template: [templates/ml-model-update-template.json](templates/ml-model-update-template.json). |

## Device vitals (operational telemetry)

Set `CONFIG_IOTCONNECT_DEVICE_VITALS=y` and every telemetry message carries a
nested **`sys`** object of operational metrics, so devices look fleet-managed on
the IOTCONNECT dashboard:

> `sys.cpu_pct` · `sys.freq_mhz` · `sys.heap_used`/`sys.heap_free` ·
> `sys.uptime_s` · `sys.reset_cause` · `sys.fw` — plus `sys.temp_c` where a board
> wires a calibrated `die-temp` devicetree alias.

It rides along automatically for demos that send through the SDK, and via one
`iotc_vitals_append(msg)` call for raw-send paths (already wired into all demos
here). Every field degrades gracefully per board, and it's left off on the
RAM-tight TF-M `/ns` builds. The device templates in [templates/](templates/)
include the matching `sys` OBJECT so the dashboard charts it. HW-verified on
FRDM-MCXE31B.

## Building a demo for a board

```sh
west build -p always -b <board-target> -d build/<name> \
  demos/<use-case> \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```

Board targets: `frdm_mcxn947/mcxn947/cpu0`, `mimxrt1170_evk/mimxrt1176/cm7`.
Device identity + certs come from the SDK sample's generated
`src/device_credentials.h` (see the SDK's provisioning docs); the include is
shared so one provisioning step covers every demo.

The **uart-telemetry-source** demo is the exception: it targets MCUs with no IP
stack, so it does **not** use the SDK module (drop `-DZEPHYR_EXTRA_MODULES`) and
needs no creds — just `-DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib`. See its
[README](demos/uart-telemetry-source/README.md) for the `frdm_mcxe31b` /
`frdm_mcxw72` build + flash commands.

## Roadmap

- **Telemetry/control tier** (direct IP): MCXN947 ✅ (+ 🔒 TF-M sealed key) ·
  RT1170-EVKB 🔨 · i.MX93 ✅ (Zephyr on the A55 — HW-verified connecting to AWS
  over gigabit Ethernet, booted from an SPSDK SD image; quickstart adds on-device
  keygen + provisioning with the identity persisted to the on-SOM eMMC)
- **Constrained tier** ([uart-telemetry-source](demos/uart-telemetry-source)
  demo → gateway): MCXE31B ✅ · MCXW72 ✅ (both HW-verified) — direct IP for
  MCXW72 planned via a cell-modem Click bearer
- **Streaming tier** (KVS/WebRTC): deferred — anchored on i.MX93 under Linux
  (upstream KVS WebRTC SDK + GStreamer/VPU), reusing IOTCONNECT's STS +
  channel-ARN brokering.
- **Microchip**: SAM E54 Xpro 🔨 (all four demos build; HW validation next).
  Then the **BLE tier**: WBZ451HPE Curiosity as a Click-carrying BLE
  beacon/peripheral (adv telemetry + GATT commands) feeding a SAMA7D65
  Curiosity gateway (Linux + BlueZ) into IOTCONNECT — firmware blocked on
  Microchip landing WBZ45x Zephyr support (zephyr#92168; today the part is
  MPLAB-Harmony-only).

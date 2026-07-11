# uart-telemetry-source demo

> **[DEMO.md](DEMO.md)** walks the demo end to end — each step's observable
> behavior and what the device and platform are doing underneath.

The **constrained-tier** demo: for MCUs whose Zephyr port has **no IP transport**
(no Ethernet/Wi-Fi, so no MQTT/TLS). It compiles the portable **iotc-c-lib**
protocol core + cJSON straight into the app and emits IOTCONNECT 2.1 telemetry
JSON on the **console UART** every 5 s:

```
IOTC-TELEMETRY: {"d":[{"d":{"sequence":9,"uptime_s":45.005,"cpu_temp_c":34.5,"board":"frdm_mcxe31b","bearer":"uart-source","status":"Ready","safe_state":true}}]}
```

A **gateway** relays that line to the cloud (over CAN FD / UART, or a future
cell-modem Click bearer) — the connectivity-less MCU still contributes to
IOTCONNECT as a verified *telemetry source*. Unlike the other demos, this one
does **not** pull in the `iotc-zephyr-sdk` module (there is no network stack to
drive) — it needs only `iotc-c-lib`. The board name comes from `CONFIG_BOARD`, so
one app serves every target.

## Supported boards

| Board target | Core | Console | Why UART-source |
|---|---|---|---|
| `frdm_mcxe31b` | Cortex-M7 @160 MHz | LPUART5 @115200 (MCU-Link VCom) | no Ethernet/Wi-Fi in the Zephyr port |
| `frdm_mcxw72` | Cortex-M33 | lpuart1 @115200 (J-Link OB VCom) | 802.15.4 (Thread) radio only |

Add a board by dropping a `boards/<board>.conf` here — no code change needed.

## Build

Point the build at `iotc-c-lib` (this demo skips the SDK module). Pass it as a
`-D` cache var, or export `ZEPHYR_IOTC_C_LIB_MODULE_DIR` in the environment:

```sh
# FRDM-MCXE31B (Cortex-M7, onboard MCU-Link -> LinkServer runner):
west build -p always -b frdm_mcxe31b -d build/uart_src_mcxe31b \
  C:/dev/zephyr/iotc-zephyr-demos/demos/uart-telemetry-source \
  -- -DZEPHYR_IOTC_C_LIB_MODULE_DIR=C:/dev/zephyr/iotc-c-lib

# FRDM-MCXW72 (Cortex-M33, onboard J-Link OB):
west build -p always -b frdm_mcxw72 -d build/uart_src_mcxw72 \
  C:/dev/zephyr/iotc-zephyr-demos/demos/uart-telemetry-source \
  -- -DZEPHYR_IOTC_C_LIB_MODULE_DIR=C:/dev/zephyr/iotc-c-lib
```

## Flash & run

**FRDM-MCXE31B** — onboard MCU-Link (LinkServer):

```sh
west flash -d build/uart_src_mcxe31b       # LinkServer runner
```

**FRDM-MCXW72** — onboard debugger is **J-Link OB** (LinkServer sees no probe).
`west flash --runner jlink` can hang on a firmware prompt; the reliable path is
to drive `JLink.exe` directly (flash `zephyr.elf` at `0x10000000`):

```sh
& "C:/Program Files/SEGGER/JLink_V874a/JLink.exe" -nogui 1 -if SWD -speed 4000 \
  -device MCXW727 -AutoConnect 1 -ExitOnError 1 -CommanderScript flash.jlink
# flash.jlink: r / h / loadfile build/uart_src_mcxw72/zephyr/zephyr.elf / r / g / q
```

Open the board's VCom at **115200 8N1** and you should see an `IOTC-TELEMETRY:`
line every 5 seconds.

## Getting this board onto IOTCONNECT for real

This board has no Zephyr IP stack today. Options, easiest first:

1. **Gateway pattern (recommended now):** forward the UART/CAN-FD JSON to an
   IP-capable node (e.g. FRDM-MCXN947) running the full `iotc-zephyr-sdk`.
2. Add a **mikroBUS / Arduino Wi-Fi or Ethernet Click** with an existing Zephyr
   driver and bind the SDK transport to it (MCXW72: a **cell-modem Click** is the
   planned direct-IP bearer).
3. Port the silicon's native Ethernet/TSN driver in Zephyr, then use the full
   telemetry demo instead of this source.

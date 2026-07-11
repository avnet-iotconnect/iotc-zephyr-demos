# /IOTCONNECT UART Telemetry Source — Demo Flow and Internals

This document walks through the uart-telemetry-source demo: what it shows,
the sequence of events, and what the device is doing underneath. Build and
flash mechanics are in the [README](README.md).

## Overview

Not every microcontroller can carry an IP stack. Safety-oriented and
radio-constrained parts — an automotive-class MCU with CAN but no Ethernet, a
Thread radio SoC with no Wi-Fi — still produce data a fleet wants. This demo
is the **constrained tier** of the IOTCONNECT story: the device builds real
IOTCONNECT 2.1 telemetry JSON **on-chip** with the portable protocol core
(`iotc-c-lib`) and emits it on its console UART, one line per message, for a
gateway to forward to the cloud over whatever link exists (UART, CAN FD, or
a future cellular bearer).

The point being demonstrated: the *protocol work* — message structure,
attribute encoding, the exact JSON the cloud expects — runs on silicon with
no network stack at all. The gateway only relays bytes; it does not need to
understand or rebuild the payload. When the constrained device is later
paired with a gateway, its data arrives at IOTCONNECT indistinguishable from
a directly connected device's.

## System components

| Component | Role |
|---|---|
| FRDM-MCXE31B (Cortex-M7) or FRDM-MCXW72 (Cortex-M33) | MCUs with no IP transport in their Zephyr port (MCXE31B: none; MCXW72: 802.15.4 only) |
| iotc-c-lib + cJSON | builds and serializes the IOTCONNECT 2.1 telemetry envelope on-device |
| Console UART | the "bearer": MCXE31B → LPUART5 via MCU-Link VCom; MCXW72 → LPUART1 via J-Link OB VCom, both 115200 8N1 |
| A gateway (separate device) | greps the UART stream for `IOTC-TELEMETRY:` lines and forwards them |

Notably absent: the iotc-zephyr-sdk network module. This build compiles the
protocol core directly — no MQTT, no TLS, no sockets — which is exactly the
constraint being demonstrated.

## Demo flow

### Step 1 — Boot

```
=== IOTCONNECT UART telemetry source -- frdm_mcxe31b ===
Building IOTCONNECT 2.1 telemetry JSON on-device via iotc-c-lib.
No IP transport in this build -> output to UART for a gateway to forward.
```

The app routes the protocol library's allocations onto the Zephyr heap,
initializes iotc-c-lib in its "custom" mode (building messages rather than
connecting), and takes its identity from the build itself: the device unique
ID is `<board>-01`, so one application serves every supported board without
per-target code.

### Step 2 — Telemetry every five seconds

```
IOTC-TELEMETRY: {"d":[{"d":{"sequence":9,"uptime_s":45.005,"cpu_temp_c":34.5,
"board":"frdm_mcxe31b","bearer":"uart-source","status":"Ready","safe_state":true}}]}
```

Each line is a complete IOTCONNECT 2.1 telemetry message — the same
double-nested `d` envelope a connected device publishes over MQTT. The
attributes:

| Key | Meaning |
|---|---|
| `sequence` | monotonic message counter |
| `uptime_s` | seconds since boot |
| `cpu_temp_c` | a synthetic, slowly varying value standing in for a real sensor |
| `board` | the Zephyr board name (`CONFIG_BOARD`) |
| `bearer` | literal `uart-source` — how this data reached the cloud |
| `status`, `safe_state` | representative status fields for a safety-style device |

When the SDK's device-vitals module is present at build time, each message
also carries the nested `sys` object (`sys.cpu_pct`, `sys.heap_used`,
`sys.uptime_s`, `sys.reset_cause`, `sys.fw`, …), so even the constrained tier
reports operational health. No timestamp is set on-device — the server
timestamps on arrival, so the demo needs no clock source.

### Step 3 — The gateway's side of the contract

Anything that can read the UART can be the gateway: it watches for the
`IOTC-TELEMETRY: ` prefix and forwards the JSON that follows, unmodified, to
IOTCONNECT over its own connection. The prefix is the entire integration
contract between the two devices.

## Implementation notes

- **No-SDK build:** the CMake pulls five iotc-c-lib core sources plus cJSON
  straight into the application. The network-capable SDK module is not in
  the build at all; only `-DZEPHYR_IOTC_C_LIB_MODULE_DIR` is needed.
- **Vitals are optional at compile time:** the build automatically links the
  SDK's `iotc_device_vitals.c` if the SDK checkout is present (it depends
  only on iotc-c-lib and Zephyr, not on networking) and omits the `sys`
  object otherwise, printing which choice it made during CMake.
- **Float printing:** cJSON serializes doubles through libc `sprintf("%g")`;
  picolibc needs `CONFIG_PICOLIBC_IO_FLOAT=y` or every number renders as the
  literal string `*float*`.
- **Identity is a label, not a credential:** with no connection there is
  nothing to authenticate; `cpid`/`duid` exist only to shape the JSON. Real
  authentication happens at the gateway that owns the cloud connection.
- **MCXW72 flashing:** the onboard debugger is J-Link OB, so LinkServer sees
  no probe; the README documents flashing via a J-Link CommanderScript with
  `zephyr.elf` at `0x10000000` (device `MCXW727`, SWD @4000).

## Troubleshooting quick reference

| Symptom | Meaning | Resolution |
|---|---|---|
| numbers print as `*float*` | picolibc float printf not enabled | `CONFIG_PICOLIBC_IO_FLOAT=y` (set in prj.conf) |
| `telemetry_create failed (out of memory)` | heap exhausted | raise `CONFIG_HEAP_MEM_POOL_SIZE` |
| no `sys` object in messages | vitals source not found at build | build with the iotc-zephyr-sdk checkout present |
| nothing on the VCom (MCXW72) | flash didn't complete via LinkServer | use the J-Link flow in the README |

# /IOTCONNECT C2D LED Control — Demo Flow and Internals

This document walks through the c2d-led demo: the sequence of events, the
behavior observable at each step, and what the device is doing underneath.
Build and flash mechanics are in the [README](README.md).

## Overview

Telemetry demos show data flowing *up*; this demo shows control flowing
*down*. A command entered in the IOTCONNECT device console — `led-on`,
`led-off`, `led-toggle` — arrives at the device over its MQTT session,
drives the board LED, is **acknowledged** back to the platform, and the
resulting LED state is immediately reflected in telemetry. It is the
smallest complete round trip of the cloud-to-device (C2D) path: command →
action → ack → observable state change, in about a second.

## System components

| Component | Role |
|---|---|
| Any supported board (FRDM-MCXN947, MIMXRT1170-EVKB, SAM E54 Xplained Pro) | Ethernet-connected Zephyr target; the board LED (devicetree alias `led0`) is the actuator |
| iotc-zephyr-sdk | network bring-up, DRA bootstrap, MQTT/TLS session, command callback and ack API |
| /IOTCONNECT device console | where commands are issued and acks/telemetry observed |

## Demo flow

### Step 1 — Boot and connect

```
<inf> c2d_led: c2d-led demo starting
<inf> c2d_led: Waiting for network connectivity...
<inf> c2d_led: Network connectivity established (L4 up)
<inf> c2d_led: MQTT status: 1
```

The app configures the LED GPIO (initially off), blocks until the network
reports L4 connectivity, syncs wall-clock time over SNTP (TLS certificate
validation requires a correct clock), runs the DRA discovery/identity
bootstrap, and connects to the broker with X.509 device credentials. Once
connected it publishes a heartbeat every 10 seconds: a single numeric
attribute, `led`, carrying `0` or `1`. With device vitals enabled, the
nested `sys` health object rides along.

### Step 2 — Send a command

From the device's command console, send `led-on`. On the device:

```
<inf> c2d_led: C2D command: led-on
<inf> c2d_led: LED -> ON
```

What happens underneath:

1. The platform publishes the command to the device's C2D MQTT topic; the
   SDK parses the envelope and invokes the application's command callback
   with the command string and an acknowledgment ID.
2. The handler matches **case-insensitive substrings** — `toggle`, `off`,
   then `on`, in that order (order matters: checking `off` before `on`
   prevents `led-off` from matching the `on` inside other words).
3. The GPIO is driven, and the ack is sent with a success status. An
   unrecognized command acks as **failed** with the message
   `unknown command` — visible in the platform's command history either way.
4. The handler immediately publishes `led` with the new state, so the
   dashboard reflects the change without waiting for the next heartbeat.

The board LED is now lit, the command shows acknowledged in the console,
and the `led` attribute reads `1`.

### Step 3 — Toggle and observe

`led-toggle` inverts whatever state the LED is in; `led-off` clears it.
Sending nonsense (for example `blink-fast`) demonstrates the failure path:
the device logs `Unrecognized LED command` and the platform receives a
failed ack with a reason — commands are never silently dropped.

## Implementation notes

- **Command matching is template-driven on the platform side:** the device
  matches substrings, so the IOTCONNECT template can name the commands
  anything containing `on`/`off`/`toggle`. The template needs a numeric
  `led` telemetry attribute and the three commands; no template JSON ships
  with this demo.
- **Credentials:** the build compiles in per-device X.509 credentials from
  the shared, git-ignored `device_credentials.h` (generated once from an
  IOTCONNECT device package). CPID/environment/DUID come from Kconfig.
- **Ordering guarantees:** the state-reflecting telemetry publish happens in
  the command callback itself, after the ack — so platform-side observers
  see ack-then-state in a deterministic order.
- **Per-board bearer configs** live in `boards/<board>.conf`: MCXN947 uses
  ENET-QoS with an RMII LAN8741 PHY (enabled by overlay), RT1170-EVKB and
  SAM E54 Xplained Pro have Ethernet on by default in their devicetrees. The
  E54 config also enables mbedTLS's NIST-curve optimization — without it the
  software TLS handshake on the 120 MHz M4F exceeds AWS IoT's handshake
  window (observed as `mqtt_connect` error `-116`).

## Troubleshooting quick reference

| Symptom | Meaning | Resolution |
|---|---|---|
| stuck at `Waiting for network connectivity...` | no DHCP lease / link down | check cable, DHCP |
| `SNTP sync failed ...; TLS will likely fail` | UDP/123 blocked or no route | allow NTP, retry |
| `connect failed (-116); retrying` (SAM E54) | TLS handshake exceeded the broker's window | keep `CONFIG_MBEDTLS_ECP_NIST_OPTIM=y` (set in the board conf) |
| command acked as failed, `unknown command` | command text lacks on/off/toggle | match the template command names |
| LED changes but dashboard lags | watching the heartbeat only | state is also pushed immediately on change; check the `led` attribute |

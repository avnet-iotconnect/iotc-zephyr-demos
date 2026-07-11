# /IOTCONNECT Telemetry Reference — Demo Flow and Internals

This document walks through the telemetry demo: what it shows, the sequence
of events, and what the device is doing underneath. Build mechanics are in
the [README](README.md).

## Overview

This is the **reference connection path** — the smallest complete
IOTCONNECT device: connect over MQTT/TLS, publish periodic telemetry,
acknowledge anything the cloud sends. It carries no application logic of its
own; the demo directory is a thin wrapper (configuration and per-board
bearer files) around the SDK's own telemetry sample, which is the point:
what this demo exercises is exactly what every other demo in this repository
builds on.

It is the natural first build for a **new board bring-up**: if this demo
connects, the board's Ethernet, TLS, clock, and credential plumbing are all
proven, and every richer demo is expected to work.

## System components

| Component | Role |
|---|---|
| Any supported board (FRDM-MCXN947, MIMXRT1170-EVKB, FRDM-IMX93, SAM E54 Xplained Pro) | Ethernet-connected Zephyr target |
| iotc-zephyr-sdk sample application | network bring-up, SNTP, DRA bootstrap, MQTT/TLS session, publish loop, generic C2D/OTA acks |
| device_credentials.h | compiled-in X.509 device certificate + key (generated per device, git-ignored) |
| /IOTCONNECT | broker coordinates via the Device REST API, dashboard |

## Demo flow

### Step 1 — Boot and network

```
<inf> IOTCONNECT telemetry sample starting (app v1.0.0)
<inf> Waiting for network connectivity...
<inf> Network connectivity established (L4 up)
<inf> Wall-clock synced (epoch=1783722560)
```

The app blocks until the interface reports L4 connectivity (a DHCP lease),
then syncs wall-clock time via SNTP — a hard prerequisite, since X.509
validation compares certificate validity windows against the device clock.

### Step 2 — Discovery, identity, connect

```
<inf> iotc_dra: DRA GET https://awsdiscovery.iotconnect.io/api/v2.1/dsdk/cpId/.../env/...
<inf> iotc_dra: DRA: discovery/identity complete; MQTT config populated
<inf> iotc_mqtt: MQTT connected
```

The SDK runs the two-step **Device REST API** bootstrap over HTTPS:
*discovery* resolves the account's regional endpoint, *identity* returns the
device's broker hostname, client ID, and topics. Nothing broker-specific is
hardcoded — the same binary follows the account wherever it is hosted. The
MQTT session then comes up with mutual TLS using the compiled-in device
certificate.

### Step 3 — The publish loop

```
<inf> Telemetry sent: random=42 version=1.0.0
```

Every five seconds (configurable) the device publishes two attributes: a
changing `random` value (0–99) — deliberately trivial, so dashboard charts
visibly update — and the application `version` string. A run-duration knob
exists for soak testing; the default runs forever, reconnecting with backoff
whenever the session drops.

### Step 4 — Cloud-to-device, acknowledged

Any command sent from the device console is received, logged, and
acknowledged as successful:

```
<inf> C2D command received: hello
<inf> Acking command (ack_id=...)
```

The handler is deliberately generic — the c2d-led demo is the one that
attaches behavior to specific commands. OTA offers are likewise received
and acknowledged (the download path itself is a documented seam, not
implemented here).

## Implementation notes

- **Credentials model:** this demo uses compiled-in per-device credentials
  (`device_credentials.h`, generated once from an IOTCONNECT device package
  and git-ignored — it contains a private key). Contrast with the
  quickstart, which generates its key on-chip and compiles in nothing but
  public roots. If an NVS identity is present and enabled, the sample
  prefers it: `Using NVS-provisioned identity` vs
  `Using compiled-in device credentials` in the boot log.
- **Template:** the matching device template is
  [zephyr-telemetry-template.json](../../templates/zephyr-telemetry-template.json).
- **Per-board bearers** live in `boards/<board>.conf`: MCXN947 (ENET-QoS +
  LAN8741, enabled by overlay), RT1170-EVKB (ENET on by default), i.MX93
  (RGMII + YT8521 on the Cortex-A55, bare-metal from DRAM — no flash, so
  compiled-in credentials only), SAM E54 Xplained Pro (GMAC + KSZ8091, plus
  the mbedTLS NIST-curve optimization the 120 MHz M4F needs to fit the
  broker's handshake window).
- **Float printing:** cJSON serializes numbers through libc `sprintf("%g")`;
  picolibc needs `CONFIG_PICOLIBC_IO_FLOAT=y` or values render as the
  literal `*float*`.

## Troubleshooting quick reference

| Symptom | Meaning | Resolution |
|---|---|---|
| stuck at `Waiting for network connectivity...` | no DHCP lease / link down | check cable and DHCP |
| `SNTP time sync failed ...; TLS will likely fail` | NTP unreachable | allow UDP/123 |
| TLS handshake fails immediately | placeholder credentials still compiled in | generate `device_credentials.h` from a real device package |
| `connect failed (-116); retrying` (SAM E54) | software handshake exceeded the broker window | keep `CONFIG_MBEDTLS_ECP_NIST_OPTIM=y` (set in the board conf) |
| values chart as `*float*` | picolibc float printf disabled | `CONFIG_PICOLIBC_IO_FLOAT=y` |

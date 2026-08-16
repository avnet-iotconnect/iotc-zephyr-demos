# Fleet Gateway with /IOTCONNECT on Zephyr

This document walks through the gateway demo end to end: the sequence of
steps, the behavior observable at each one, and what the device and platform
are doing underneath. Build, flash, and provisioning mechanics are covered in
the [README](README.md) and the
[FRDM-i.MX93 quickstart](../../boards/frdm-imx93/QUICKSTART.md).

## Overview

Three boards, one MQTT connection, one dashboard. Two connectivity-less MCUs
(FRDM-MCXE31B, FRDM-MCXW72 — no Ethernet, no Wi-Fi) build their own
IOTCONNECT 2.1 telemetry on-device and print it on a UART. A FRDM-i.MX93
running Zephyr on its Cortex-A55 ingests those lines, wraps each as a
child-device record, and carries the fleet uplink — while continuously
reporting its own health.

The property this demo exists to show is **store-and-forward**: the gateway
treats the uplink as unreliable by design. Every record that cannot be
published immediately is written to a raw-sector ring on the on-SOM eMMC with
its original timestamp; reconnection drains the ring oldest-first and the
cloud timeline backfills without a gap. Losing the network loses nothing.

## System components

| Component | Role |
|---|---|
| FRDM-i.MX93 (`frdm_imx93/mimx9352/a55`) | gateway: UART ingest, child wrapping, MQTT/TLS uplink, eMMC spool |
| FRDM-MCXE31B / FRDM-MCXW72 | telemetry sources ([uart-telemetry-source](../uart-telemetry-source)): iotc-c-lib builds 2.1 JSON on-device, emitted as `IOTC-TELEMETRY: {...}` lines |
| eMMC spool ring | 3-sector (1536 B) records: magic + sequence + CRC32 + ready-to-publish JSON; ring scan recovers state at boot |
| /IOTCONNECT | gateway + child device model, dashboard, command console |

## Setup

1. Import the gateway template (tags `gw` + `uartsrc`), provision the
   gateway, and create the child devices under it (README §Onboard).
2. Wire a source MCU's TX to the gateway's UART1 RXD + common GND. The child
   Unique ID for each link is Kconfig (`CONFIG_GATEWAY_LINK0_CHILD_ID`,
   default `frdmmcxe31b01` — the id the source announces).

## The demo script

### 1. The fleet appears on one connection

Gateway console after boot:

```
gateway demo starting (1 UART link(s) in devicetree)
link0: listening on lpuart1@44380000
spool: 2048 slots @ sector 20100000, 0 queued (seq 0..0)
Provisioned as duid=imx93-gw-01 -- bringing up network
Connected. Try: links, spool-info; then pull the cable.
```

Within ~5 s the first source line arrives and its child's tiles populate on
the dashboard — `sequence`, `cpu_temp_c`, `board: frdm_mcxe31b` — under the
gateway device. The `links` command ACKs the mapping:
`link0(lpuart1->frdmmcxe31b01): rx=12 drop=0`.

Underneath: each `IOTC-TELEMETRY:` line is parsed, its data object detached
and re-wrapped as `{"d":[{"id":"frdmmcxe31b01","tg":"uartsrc","dt":...,
"d":{...}}]}` — the 2.1 gateway schema — and published on the gateway's
reporting topic. The source MCU's fields pass through byte-identical.

### 2. Pull the cable

Yank the Ethernet cable mid-stream. Console:

```
Network connectivity lost (L4 down) -- spooling
connect failed; 14 records spooled so far
```

Nothing else changes: sources keep printing, the gateway keeps ingesting,
and every record — child lines and its own 10 s health record — goes to the
eMMC ring instead of the socket. Each carries the `dt` of the moment it was
*captured*, not the moment it will eventually be sent. The board survives a
reboot in this state: the ring is scanned and the queue continues where it
left off. On the dashboard, mid-outage looks like this:

![Dashboard mid-outage — spooling to eMMC](docs/images/dashboard-offline.png)

### 3. Plug it back in — the timeline heals

Within the 20 s retry cadence the gateway reconnects and starts draining
four spooled records per half-second tick alongside live traffic. On the
dashboard, the charts **backfill**: the offline minutes fill in with
correctly-timestamped points, and `gw.online: 0` marks exactly which records
lived through the outage. `spool-info` ACKs the accounting:

```
0/2048 queued, pushed=47 drained=47 drops=0
```

Drained slots are invalidated on the eMMC as they go, so power-cycling
mid-drain re-sends nothing. The healed timeline on the dashboard — the
spool's rise-and-drain curve, the outage band, and the children's
temperature charts continuing without a gap:

![Dashboard after reconnect — timeline healed](docs/images/dashboard.png)

### 4. Inspect the honesty counters

Everything the gateway does is measured and published: per-link RX/drop
counts, seconds since each link's last message (`gw.link0_age_s` — unplug a
source's TX wire and watch it climb), spool depth/drops, forwarded-record
totals, and the `sys.*` vitals of the gateway itself.

## What to look at underneath

- **One TLS session for the fleet** — the child records ride the gateway's
  connection; the sources hold no credentials at all. Silicon with no radio
  and no IP stack still reports to the cloud with per-device identity.
- **`dt` semantics** — the platform accepts historical timestamps in 2.1
  records; that single property is what turns a dumb retry buffer into
  gap-free history.
- **Bounded, honest storage** — the ring overwrites oldest-first when full
  and counts every sacrifice in `gw.spool_drops`. No unbounded queues, no
  silent loss.

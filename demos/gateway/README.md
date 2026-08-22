# Gateway — child devices + offline store-and-forward (FRDM-i.MX93)

> **[DEMO.md](DEMO.md)** walks the demo end to end — each step's observable
> behavior and what the device and platform are doing underneath.

An **IOTCONNECT gateway** on the NXP **FRDM-i.MX93** (Cortex-A55, Zephyr) that
completes this repo's "telemetry source" arc: connectivity-less MCUs running
[uart-telemetry-source](../uart-telemetry-source) (FRDM-MCXE31B,
FRDM-MCXW72) print IOTCONNECT 2.1 JSON on a UART; the gateway ingests those
lines, forwards each as a **child-device record** (`id` + `tg` per the 2.1
gateway schema), adds its own gateway health telemetry, and carries the whole
fleet over one MQTT/TLS connection.

The headline is **resilience**: pull the Ethernet cable and nothing is lost.
Every record — gateway and children — diverts to a **raw-sector ring on the
on-SOM eMMC** with its original timestamp embedded; on reconnect the spool
drains oldest-first and the platform **backfills a gap-free timeline**, with
honest `gw.spool_*` counters telling the story.

![Fleet gateway dashboard](docs/images/dashboard.png)

```
FRDM-MCXE31B ──UART──▶│                │
                      │  FRDM-i.MX93   │──MQTT/TLS──▶ IOTCONNECT
FRDM-MCXW72  ──UART──▶│  gateway (A55) │              gateway + 2 children
                      │  eMMC spool    │              on one dashboard
```

## Hardware

- **FRDM-i.MX93**, Ethernet into the RJ45 (DHCP), boot SD card, USB-C console.
- A **source MCU** flashed with
  [uart-telemetry-source](../uart-telemetry-source): its console TX wired to
  the gateway's **UART1 RXD = 40-pin header P11 pin 7** (net `GPIO_IO04`;
  GND is conveniently on pin 9 next door), 115200 8N1. Source TX pins:
  FRDM-MCXE31B `PTE14` (LPUART5), FRDM-MCXW72 `J4 pin 2` (LPUART1/PTC3).
  One wired source at a time (the A55 exposes a single spare UART); the
  gateway identifies whichever board is talking from the message's own
  `board` field, so swapping the TX wire swaps the child. Link 1 remains
  scaffolded (alias `iotc-link1`) if a second UART is ever added to the
  A55 devicetree.
- **No source MCUs handy? No wiring needed at all** (hardware-verified): on
  the FRDM-IMX93 the debug USB's *second* CH342 COM port is wired to UART1 —
  the very UART link 0 listens on — so a PC can play the source MCU over the
  same USB cable that carries the console:

  ```sh
  python tools/emulate_source.py COM20      # your second i.MX93 COM port
  ```

  The gateway can't tell the difference; the link-0 child device comes alive
  on the dashboard within seconds.

## Build

```sh
west build -p always -b frdm_imx93/mimx9352/a55 -d build/gateway \
  demos/gateway \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```

A step-by-step bring-up walkthrough is in [QUICKSTART.md](QUICKSTART.md).
Needs the `aarch64-zephyr-elf` toolchain; for a bootable `flash.bin` add
`-DUSE_NXP_SPSDK_IMAGE=y` (see the
[board quickstart](../../boards/frdm-imx93/QUICKSTART.md) for SD imaging).

## Onboard

1. Import [gateway-template.json](../../templates/gateway-template.json)
   (code `zephyrgw`). It is a **Gateway** template with two tags: `gw`
   (the gateway's own attributes/commands) and `uartsrc` (the child-device
   attributes) — one template covers the whole fleet.
2. Provision the gateway from its serial prompt (`iotcprov provision <duid>`
   → Create Device, **Gateway**, Self-Signed, paste the certificate; `iotc
   config` → paste `iotcDeviceConfig.json`; `kernel reboot cold`).
3. Create the **child devices** under the gateway device (Gateway Device →
   Child Devices tab), choosing tag `uartsrc`, with Unique IDs matching the
   firmware's mapping: `frdmmcxe31b01` and `frdmmcxw7201` (the gateway also
   derives the child id from each message's own `board` field, so any
   source board resolves to `<sanitized-board>01`). Note: elsewhere in the
   platform UI, children display **prefixed with the gateway's ID** — e.g.
   `imx93-gw-01-frdmmcxe31b01` — but the id the firmware sends stays the
   short form. Child ids allow only letters and digits (no `-`/`_`).

## Run

- Sources publish every ~5 s; the gateway forwards each line within half a
  second as its child's telemetry. `links` (C2D) ACKs per-link RX/drop
  counters; `gw.link0_age_s` on the dashboard shows link liveness.
- **Pull the Ethernet cable.** Console logs "spooling"; `gw.spool_count`
  grows (3-sector records on the eMMC at
  `CONFIG_GATEWAY_SPOOL_SECTOR`, default ring 3 MB ≈ hours of buffering).
  Records keep their original `dt` timestamps.
- **Plug it back in.** The gateway reconnects (retry every 20 s), drains the
  spool oldest-first (4 records/tick alongside live traffic), and the
  dashboard timeline backfills without a gap. `gw.spool_drained` totals the
  recovery; drained slots are invalidated on the eMMC so a reboot never
  re-sends them.

| Command | Effect |
|---|---|
| `interval <sec>` | gateway-record publish interval (default 10) |
| `spool-info` | ACK with queued/capacity/pushed/drained/drops |
| `spool-wipe` | erase the ring (also clears any stale records) |
| `links` | ACK with per-link UART + child mapping + counters |
| `reboot` | cold reboot |

## Dashboard

A ready-made dashboard export ships in [dashboard/](dashboard/) — the
screenshots above, on live data: gateway health + spool gauges, outage
charts, and a card per child device. **This is a gateway dashboard**: its
widgets bind to the gateway *and* to each child device, so create the
gateway and both children before importing. Full import steps (including
the gateway-prefixed child names in the device picker and the
re-binding-on-a-new-tenant procedure) in
[dashboard/README.md](dashboard/README.md).

## Notes

- **Message shape:** child records ride the standard 2.1 gateway schema —
  `{"d":[{"id":"<child>","tg":"uartsrc","dt":"<iso>","d":{...}}]}` — built
  with cJSON from the source's own serialized message, so the source MCU's
  fields pass through unmodified.
- **Why raw sectors, not a filesystem:** this target runs bare-metal from
  DRAM with no NOR flash; the SDK already persists identity to a raw eMMC
  region. The spool reuses that pattern — no format step, CRC32 per record,
  deterministic recovery by ring scan at boot.
- **Boot needs the network once** (SNTP + discovery run before the resilient
  loop starts); offline tolerance covers uplink loss *after* startup.
- The ingest ring absorbs ~10 s of source traffic per link while the loop is
  busy inside a reconnect attempt; overflow is counted per link
  (`gw.linkN_drop`), never silent.

# click-telemetry demo

Auto-detect MikroE **Click** sensor boards on a **Shuttle** (up to 4 Clicks on
one mikroBUS socket) and stream their readings to Avnet /IOTCONNECT. Clicks are
**recognized on power-up** and every recognized Click is read each publish cycle
into one nested-object telemetry record.

Runs two ways on the FRDM-MCXN947:

- **`frdm_mcxn947/mcxn947/cpu0`** — software TLS, identity from the compiled-in
  creds header (or NVS). Simplest to build.
- **`frdm_mcxn947/mcxn947/cpu0/ns`** — **TF-M, hardware-sealed key**: the device
  key lives in TF-M Protected Storage (provisioned once via the quickstart), the
  binary carries only public CA roots, and crypto runs in the secure world.
  **HW-verified** streaming 4 Clicks to AWS IoT Core.

## How recognition works

The Shuttle fans one mikroBUS socket out to 4 positions that **share the mikroBUS
I2C bus**, so the fitted Clicks must have **distinct I2C addresses**. At boot the
app walks a registry and marks each Click present by a raw I2C probe, then reads
every recognized Click each cycle. The boot log prints `RECOGNIZED` / `absent`
per Click, and each publish logs e.g. `Published telemetry from 4 Click(s): ...`.

The FRDM mikroBUS has **no onboard I2C pull-ups** — add external ~4.7 kΩ, or use
a Click that supplies them (e.g. Air quality 7).

## Click coverage (in-app raw-I2C readers)

Addresses and conversions come from the proven Microchip WFI32-IoT reference
(not the parts catalog). Every Click below is read directly over I2C — no Zephyr
sensor drivers or DT sensor nodes.

| Click | IC | Addr | Telemetry keys |
|---|---|---|---|
| Temp&Hum 14 | TE HTU31D | 0x40 | temperature_c, humidity_pct |
| Altitude 2 | TE MS5607 | 0x76 | pressure_mbar, temperature_c, altitude_m |
| Altitude 4 | baro (0xAC) | 0x27 | pressure_hpa, temperature_c, altitude_m |
| Ultra-Low Press | diff-press | 0x6C | pressure_pa, temperature_c |
| VAV Press | diff-press | 0x5C | pressure_pa, temperature_c |
| Air quality 7 | Amphenol MiCS-VZ-89TE | 0x70 | co2eq_ppm, tvoc_ppb |
| T6713 CO2 | Amphenol T6713 | 0x15 | co2_ppm |
| T9602 | Amphenol T9602 | 0x28 | humidity_pct, temperature_c |
| PHT | TE MS8607 | 0x40 + 0x76 | pressure + temperature + humidity |

**Address collisions (don't co-bus):** PHT uses 0x40 (clashes with Temp&Hum 14)
and 0x76 (clashes with Altitude 2). Telemetry keys are nested per Click, e.g.
`temp_hum_14.temperature_c` → an OBJECT attribute `temp_hum_14` with child
`temperature_c` in the device template.

**Verified live on an N947 Shuttle:** Temp&Hum 14, Altitude 4, Ultra-Low Press,
Air quality 7 — all read and published together.

## C2D commands

`config.cmd_cb` handles `led-on` / `led-off` / `led-toggle` (drive the board
LED), `set-reporting-interval <sec>` (runtime publish cadence), and `reboot`,
each ACKed via `iotcl_mqtt_send_cmd_ack`.

## Build

```sh
python C:/dev/zephyr/creds/gen_creds_header.py   # once (cpu0 path only)

# cpu0 (software TLS, baked-in identity):
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/click_cpu0 \
  C:/dev/zephyr/iotc-zephyr-demos/demos/click-telemetry \
  -- -DZEPHYR_EXTRA_MODULES=C:/dev/zephyr/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=C:/dev/zephyr/iotc-c-lib

# /ns (TF-M, hardware-sealed key) -- needs the module edits in
# iotc-zephyr-sdk/patches/README.md (mbedTLS premaster + the RAM rebalance):
west build -p always -b frdm_mcxn947/mcxn947/cpu0/ns -d build/click_ns \
  C:/dev/zephyr/iotc-zephyr-demos/demos/click-telemetry \
  -- -DZEPHYR_EXTRA_MODULES=C:/dev/zephyr/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=C:/dev/zephyr/iotc-c-lib
west flash -d build/click_ns          # flashes build/click_ns/zephyr/tfm_merged.hex
```

The `/ns` build reuses the identity already sealed in TF-M Protected Storage by
the [quickstart](../quickstart) flow — no baked-in device key. Provision once
there (`iotcprov provision <duid>` → register cert → `iotc config`), then flash
click-telemetry `/ns`; it loads that sealed identity and streams Click data.

**IOTCONNECT display note:** telemetry values show as `null` until the device's
template defines the attributes. Import
[templates/click-demos-device-template.JSON](../../templates/click-demos-device-template.JSON)
(the 9 Clicks as OBJECT attributes) onto the device's template.

The connectivity Clicks in the inventory (LTE IoT 12 / BG95 cellular, EnOcean 4)
are bearers/links, not I2C sensors — they belong in their own demos.

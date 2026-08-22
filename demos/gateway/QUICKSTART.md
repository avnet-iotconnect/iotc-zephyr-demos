# Gateway — quickstart

The condensed path to a three-board fleet on one IOTCONNECT connection, with
offline store-and-forward. Full background: [README.md](README.md) and
[DEMO.md](DEMO.md); board flashing/SD-imaging details:
[FRDM-i.MX93 quickstart](../../boards/frdm-imx93/QUICKSTART.md).

## 1. Hardware

1. **FRDM-i.MX93**: Ethernet (DHCP), boot SD, USB-C console (115200 8N1 —
   the shell is on the same VCom).
2. **Source MCU(s)** flashed with
   [uart-telemetry-source](../uart-telemetry-source): source TX → gateway
   **UART1 RXD** (GPIO1_IO04), GND ↔ GND. (Optional — the gateway demos
   spool/health on its own.)

## 2. Build + flash

```sh
west build -p always -b frdm_imx93/mimx9352/a55 -d build/gateway \
  demos/gateway \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```

Write the image to the boot SD per the board quickstart
(`-DUSE_NXP_SPSDK_IMAGE=y` for `flash.bin`).

## 3. Onboard (once)

1. Import [gateway-template.json](../../templates/gateway-template.json)
   (`zephyrgw`) — a Gateway template with tags `gw` (gateway) and `uartsrc`
   (children); one import covers the fleet.
2. On the gateway console: `iotcprov provision <duid>` → **Create Device**
   (Gateway, Self-Signed, paste cert) → `iotc config` (paste
   `iotcDeviceConfig.json`) → `kernel reboot cold`.
3. Under the gateway device, create children with tag `uartsrc` and Unique
   IDs `frdmmcxe31b01` / `frdmmcxw7201` (or override
   `CONFIG_GATEWAY_LINK*_CHILD_ID`).

## 4. Dashboard

Import [dashboard/imx93-fleet-gateway_dashboard_export.json](dashboard/imx93-fleet-gateway_dashboard_export.json)
via **Dashboards → Create Dashboard → Import dashboard** — after the gateway
**and both children** exist (it binds widgets to all three devices; see
[dashboard/README.md](dashboard/README.md) for re-binding on a fresh tenant).

## 5. The three demo moments

1. **Fleet-on-one-connection:** child tiles populate within ~5 s of wiring a
   source; `links` ACKs the per-link counters.
2. **Pull the Ethernet cable:** console says "spooling"; records queue on
   the eMMC with their original timestamps (`spool-info` after reconnect
   shows the count).
3. **Plug it back in:** reconnect within ~20 s, the spool drains, and the
   dashboard timeline backfills gap-free — `gw.online: 0` marks the records
   that lived through the outage.

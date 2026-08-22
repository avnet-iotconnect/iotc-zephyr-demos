# Telemetry demo (reference)

> **[DEMO.md](DEMO.md)** walks the demo end to end — each step's observable
> behavior and what the device and platform are doing underneath.

Connect to Avnet /IOTCONNECT and publish periodic telemetry over MQTT/TLS, with a
cloud-to-device command callback. This is the portable reference demo: it reuses
the app in `iotc-zephyr-sdk/samples/telemetry` and adds this demo's own
`prj.conf` + per-board overlays under `boards/`.

## Supported boards

| Board | Bearer | Board files |
|---|---|---|
| `frdm_mcxn947/mcxn947/cpu0` | Ethernet (ENET-QoS + LAN8741) | `boards/frdm_mcxn947_mcxn947_cpu0.{conf,overlay}` |
| `mimxrt1170_evk/mimxrt1176/cm7` | Ethernet (100M ENET, onboard PHY) | `boards/mimxrt1170_evk_mimxrt1176_cm7.conf` |
| `frdm_rw612` | Wi-Fi (onboard 802.11ax, runtime `wifi cred` provisioning) | `boards/frdm_rw612.conf` |
| `frdm_imx93/mimx9352/a55` | Gigabit Ethernet (bare-metal A55) | `boards/frdm_imx93_mimx9352_a55.{conf,overlay}` |
| `same54_xpro` | Ethernet (GMAC + KSZ8091) | `boards/same54_xpro.conf` |

Add a board = drop a `boards/<board>.conf` (+ `.overlay` if the bearer needs
devicetree) — no code changes.

## Device identity: two ways

- **Runtime (recommended where enabled)** — on `frdm_rw612` the board config
  enables the NVS identity store and the provisioning shell, so the binary
  carries no credentials: Wi-Fi and device identity are both provisioned at
  the console, exactly as in the [quickstart](../quickstart). A device
  provisioned once runs every demo on this board.
- **Compiled-in** — the other boards bake the identity into the binary:
  1. Create the device in IOTCONNECT (import
     [templates/zephyr-telemetry-template.json](../../templates/zephyr-telemetry-template.json),
     any auth type that gives you a certificate + private key PEM pair —
     e.g. Self-Signed with a locally generated key).
  2. Generate the credentials header from the PEM pair:
     ```sh
     python <path>/iotc-zephyr-sdk/tools/gen_device_credentials.py \
         device-cert.pem device-key.pem
     ```
  3. Set the identity in this demo's `prj.conf` (they ship empty):
     `CONFIG_IOTCONNECT_CPID`, `CONFIG_IOTCONNECT_ENV`,
     `CONFIG_IOTCONNECT_DUID` — CPID and environment are shown under
     Settings → Key Vault in IOTCONNECT, or in the device's
     `iotcDeviceConfig.json`.

## Build & run

From a Zephyr 4.4 workspace, with this repo and the two SDK modules checked
out (`west init -m <this repo>` puts everything in place — then the two `-D`
flags below are unnecessary):

```sh
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/demo_telemetry \
  demos/telemetry \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
west flash -d build/demo_telemetry
```

Open the board's serial console at 115200 8N1 (see the board quickstart in
[boards/](../../boards/) for the port and flashing specifics).

Expected console flow: DHCP lease → DRA discovery/identity → MQTT/TLS connect →
`Telemetry sent: ...` every few seconds, `message delivered` acks, and the
data arriving under the device in IOTCONNECT.

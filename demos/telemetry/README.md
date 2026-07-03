# Telemetry demo (reference)

Connect to Avnet /IOTCONNECT and publish periodic telemetry over MQTT/TLS, with a
cloud-to-device command callback. This is the portable reference demo: it reuses
the app in `iotc-zephyr-sdk/samples/telemetry` and adds this demo's own
`prj.conf` + per-board overlays under `boards/`.

## Supported boards

| Board | Bearer | Overlay |
|---|---|---|
| `frdm_mcxn947/mcxn947/cpu0` | Ethernet (ENET-QoS + LAN8741) | `boards/frdm_mcxn947_mcxn947_cpu0.{conf,overlay}` |

Add a board = drop a `boards/<board>.conf` (+ `.overlay` if the bearer needs
devicetree) — no code changes.

## Build & run

```sh
# 1. Provision device credentials (writes the git-ignored device_credentials.h)
python C:/dev/zephyr/creds/gen_creds_header.py

# 2. Build (against an existing Zephyr 4.4 workspace)
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/demo_telemetry \
  C:/dev/zephyr/iotc-zephyr-demos/demos/telemetry \
  -- -DZEPHYR_EXTRA_MODULES=C:/dev/zephyr/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=C:/dev/zephyr/iotc-c-lib

# 3. Flash + watch serial (LPUART5 @115200)
bash C:/dev/zephyrproject/flash_and_monitor.sh   # adjust the build dir inside
```

Expected console flow: DHCP lease → DRA discovery/identity → MQTT/TLS connect →
`telemetry` publishes every few seconds.

## Configuration

Device identity is in `prj.conf` (`CONFIG_IOTCONNECT_CPID/ENV/DUID`,
`CONFIG_IOTCONNECT_DRA_DISCOVERY_HOST`). Credentials are PEM blobs in the
generated `device_credentials.h` (never committed). See the SDK sample README
for the credential roles (device cert/key, broker CA, DRA CA).

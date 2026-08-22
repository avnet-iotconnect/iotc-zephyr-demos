# c2d-led demo

> **[DEMO.md](DEMO.md)** walks the demo end to end — each step's observable
> behavior and what the device and platform are doing underneath.

Cloud-to-device control: toggle the board LED (`led0`) from Avnet /IOTCONNECT
**commands**, and report the LED state back as **telemetry** (`{"led": 0|1}`).
Demonstrates the C2D command path of the SDK (command callback + ACK).

## Supported boards

| Board | LED | Bearer |
|---|---|---|
| `frdm_mcxn947/mcxn947/cpu0` | `led0` (red) | Ethernet |
| `mimxrt1170_evk/mimxrt1176/cm7` | `led0` | Ethernet |
| `frdm_rw612` | `led0` (green) | Wi-Fi (runtime `wifi cred` + on-device identity, as in quickstart) |
| `same54_xpro` | `led0` (yellow) | Ethernet |

## Commands

Import [templates/c2d-led-template.json](../../templates/c2d-led-template.json)
(code `zephc2dled`: `led` heartbeat attribute, device vitals, and the three
LED commands), or create a command on your own device template; the command
name (text) is matched case-insensitively:

| Contains | Action |
|---|---|
| `toggle` | invert the LED |
| `off` | LED off |
| `on` | LED on |

e.g. command names `led-on`, `led-off`, `led-toggle`. Each command is ACKed
(success, or failure with `"unknown command"`).

## Device identity

On `frdm_rw612`, identity is provisioned at runtime (quickstart flow) — the
binary is credential-free and a prebuilt image is on the
[Releases page](https://github.com/avnet-iotconnect/iotc-zephyr-demos/releases).
On the other boards the identity is compiled in: generate the credentials
header from the device's certificate + key PEM pair and set
`CONFIG_IOTCONNECT_CPID/ENV/DUID` in `prj.conf` — the steps are described in
the [telemetry demo README](../telemetry/README.md#device-identity-two-ways).

## Build & run

```sh
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/demo_c2d_led \
  demos/c2d-led \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
west flash -d build/demo_c2d_led
```

(In a workspace created from this repo's manifest the two `-D` flags are
unnecessary.) Then send a command from the IOTCONNECT console and watch
`led0` change; the device also publishes `{"led": …}` on each change and
every 10 s, visible under the device's live data.

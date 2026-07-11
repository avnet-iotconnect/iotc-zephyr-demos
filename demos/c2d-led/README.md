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

## Commands

Create a command on your IOTCONNECT device template; the command name (text) is
matched case-insensitively:

| Contains | Action |
|---|---|
| `toggle` | invert the LED |
| `off` | LED off |
| `on` | LED on |

e.g. command names `led-on`, `led-off`, `led-toggle`. Each command is ACKed
(success, or failure with `"unknown command"`).

## Build & run

```sh
python C:/dev/zephyr/creds/gen_creds_header.py   # once, provisions creds

west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/demo_c2d_led \
  C:/dev/zephyr/iotc-zephyr-demos/demos/c2d-led \
  -- -DZEPHYR_EXTRA_MODULES=C:/dev/zephyr/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=C:/dev/zephyr/iotc-c-lib
```

Flash, then send a command from the IOTCONNECT console and watch `led0` change;
the device also publishes `{"led": …}` on each change and every 10 s.

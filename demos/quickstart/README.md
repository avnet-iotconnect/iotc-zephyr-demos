# /IOTCONNECT Zephyr Quickstart

> **[DEMO.md](DEMO.md)** walks the demo end to end — each step's observable
> behavior and what the device and platform are doing underneath.

A **flash-and-provision** binary. No build toolchain, no credentials to embed —
flash the prebuilt image, open the serial console, and provision the device to
**your own** IOTCONNECT account entirely from the prompt. The device generates
its **own** key and certificate on-chip; the private key never leaves it, and
only the public CA roots are compiled into the binary.

## Supported boards

| Board | Target | Artifact |
|---|---|---|
| MIMXRT1170-EVKB | `mimxrt1170_evk/mimxrt1176/cm7` | `zephyr.bin` / `.hex` |
| FRDM-MCXN947 | `frdm_mcxn947/mcxn947/cpu0` | `zephyr.hex` |

Both connect over Ethernet (DHCP) — plug in the RJ45.

## Quickstart (no toolchain needed)

1. **Flash** the prebuilt image for your board (LinkServer / MCU-Link, J-Link,
   or drag-and-drop), then open the **serial console** at 115200 8N1.

2. On first boot the device is unprovisioned and prints a guide. **Generate its
   identity on-chip:**
   ```
   iotcprov provision <your-duid>
   ```
   It creates an EC P-256 key + self-signed certificate on the device and prints
   the certificate.

3. **Create the device in IOTCONNECT:** Devices → Create Device, Unique ID =
   `<your-duid>`, **Self-Signed**, and paste the certificate from step 2.

4. **Download `iotcDeviceConfig.json`** from the device's Info panel, then paste
   it into the prompt:
   ```
   iotc config
   { ...paste the whole json block... }
   ```
   This sets cpid / env / duid from the file.

5. **Connect:**
   ```
   kernel reboot cold
   ```
   The board comes up as your device and streams telemetry (`random`, `version`
   — import [templates/zephyr-telemetry-template.json](../../templates/zephyr-telemetry-template.json)).

That's it — the device is on your IOTCONNECT account, with a key it generated
itself.

## Security model

- **The private key is generated on the device** (PSA Crypto + hardware RNG) and
  never touches a PC.
- **No device credentials are compiled into the binary** — only the public AWS
  broker CAs and the discovery-host CA (`src/quickstart_credentials.h`). The
  binary is safe to distribute.
- Credentials live in NVS and survive an application reflash. `iotc cred show`
  inspects them; `iotc cred clear` erases them.
- *Current limitation:* the on-device key is exported into NVS (in the clear).
  The hardening step keeps it **non-exportable in PSA Protected Storage** with
  TLS using the PSA key id directly — see the SDK's
  [docs/provisioning-nvs.md](../../../iotc-zephyr-sdk/docs/provisioning-nvs.md).

## Building it yourself

```sh
west build -p always -b <target> demos/quickstart \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```
The output `build/<name>/zephyr/zephyr.{hex,bin}` is the distributable binary.
Cloud/region are set at build time (`CONFIG_IOTCONNECT_CT_AWS`,
`CONFIG_IOTCONNECT_DRA_DISCOVERY_HOST` in `prj.conf`); everything else is
provisioned at runtime.

## Full command reference

See [docs/provisioning-nvs.md](../../../iotc-zephyr-sdk/docs/provisioning-nvs.md)
for every `iotc` / `iotcprov` command and the manual (PC-generated-key) path.

# /IOTCONNECT Quickstart — Demo Flow and Internals

This document walks through the quickstart demo: the sequence of steps, the
behavior observable at each one, and what the device and platform are doing
underneath. Flash mechanics per board are in the board quickstarts under
[boards/](../../boards/).

## Overview

The quickstart is a **flash-and-provision** binary: a prebuilt image that can
be distributed freely, flashed without a toolchain, and onboarded to any
IOTCONNECT account from a serial prompt in a few minutes. It exists to answer
the first question anyone asks of a device platform — *how fast can I get
this board onto my account?* — without shipping secrets.

The key property is what the binary does **not** contain: no device
certificate, no private key, no account identifiers. Only public CA roots
are compiled in. The device generates its **own** EC P-256 key and
self-signed certificate on-chip, using the hardware RNG through the PSA
Crypto API; the private key never exists anywhere but on the device. The
operator registers the printed certificate, pastes the account's device
config, and reboots.

## System components

| Component | Role |
|---|---|
| A supported board (FRDM-MCXN947, MIMXRT1170-EVKB, FRDM-IMX93, SAM E54 Xplained Pro) | Ethernet-connected Zephyr target |
| Provisioning shell (`iotcprov`, `iotc` commands) | on-device key generation and identity storage over the serial console |
| NVS (or board-appropriate store) | where the generated identity persists — NVS flash on most boards, eMMC disk on i.MX93, TF-M Protected Storage on the MCXN947 `/ns` build |
| quickstart_credentials.h | public CA roots only (Amazon Root CA 1 + Starfield for the broker, Go Daddy G2 for discovery) |
| /IOTCONNECT | device registry (Self-Signed auth), dashboard |

## Demo flow

### Step 1 — First boot: unprovisioned

The device prints its guide and waits at the shell:

```
========================================================
  IOTCONNECT QUICKSTART -- device is not provisioned
  (no identity stored in NVS)
--------------------------------------------------------
  1) Generate this device's identity ON the device:
        iotcprov provision <your-duid>
  2) In IOTCONNECT: Create Device (Self-Signed) and paste
     the certificate it printed.
  3) Paste the downloaded iotcDeviceConfig.json:
        iotc config
        { ...paste the json block... }
  4) Connect:  kernel reboot cold
========================================================
```

Nothing network-related runs yet: with no identity there is nothing to
connect as, so the application parks at the shell.

### Step 2 — Generate the identity on-chip

```
iotcprov provision my-device-01
```

The device generates an EC P-256 key pair (hardware RNG via PSA Crypto),
self-signs an X.509 certificate for the given unique ID, stores the full
identity, and prints the certificate PEM. Registering that certificate on a
**Self-Signed** device in IOTCONNECT is the trust handshake: the platform
learns the public half; the private half never left the chip.

### Step 3 — Bind the account

```
iotc config
{ ...paste iotcDeviceConfig.json... }
```

The pasted JSON (downloaded from the device's info panel) carries the
account coordinates — CPID, environment, unique ID — which are stored
alongside the key. `iotc cred show` displays the stored identity at any
time; it never echoes the private key.

### Step 4 — Reboot and connect

```
kernel reboot cold
...
<inf> Provisioned as duid=my-device-01 -- bringing up network
<inf> Waiting for network connectivity...
```

On this and every subsequent boot the identity loads from persistent
storage, the network comes up (DHCP), the wall clock syncs over SNTP
(certificate validation needs correct time), the SDK runs the two-step
Device REST API bootstrap (discovery → identity, over HTTPS against the
compiled-in public roots), and the device connects to the broker with
mutual-TLS using its self-generated key. It then publishes every 10
seconds: a changing `random` value (0–99), the app `version`, and the
nested `sys` device-vitals object (`sys.cpu_pct`, `sys.heap_used`,
`sys.uptime_s`, `sys.reset_cause`, `sys.fw`, …) so the device immediately
looks fleet-managed on the dashboard.

The identity **survives reflashes**: it lives in a storage region separate
from the application image, so firmware can be updated without
re-onboarding.

## Per-board identity storage

The provisioning flow is identical everywhere; where the identity lives is
board-appropriate:

- **Most boards** — NVS in on-chip flash.
- **FRDM-MCXN947 `/ns` (TF-M build)** — the identity is sealed in **TF-M
  Protected Storage**: crypto runs in the secure world and the key is
  hardware-protected, never readable from application code.
- **FRDM-IMX93** — Zephyr runs bare-metal on the Cortex-A55 with no NOR
  flash, so the identity persists to a dedicated sector of the on-SOM
  **eMMC** and survives power cycles there.

## Implementation notes

- **Template:** the matching device template is
  [zephyr-telemetry-template.json](../../templates/zephyr-telemetry-template.json)
  (`random`, `version`, and the `sys` vitals object).
- **Security posture:** on non-TF-M builds the generated key is stored in
  plain NVS — adequate for evaluation, not deployment. The hardened path
  (non-exportable PSA key, TF-M sealing) is documented in the SDK's
  provisioning notes and demonstrated by the MCXN947 `/ns` build.
- **Shell buffers** are enlarged (3 KB command buffer, 4 KB RX ring) because
  provisioning pastes multi-kilobyte PEM and JSON blocks through the
  console.
- **No C2D handling:** the quickstart deliberately registers no command or
  OTA callbacks — it is the onboarding story only. The other demos build on
  the same identity to add control and ML flows.
- **SAM E54 note:** the software TLS handshake on the 120 MHz M4F needs
  mbedTLS's NIST-curve optimization (set in the board conf) to fit the
  broker's handshake window.

## Troubleshooting quick reference

| Symptom | Meaning | Resolution |
|---|---|---|
| guide banner reappears after reboot | identity not stored (provision step skipped/failed) | rerun `iotcprov provision`, check `iotc cred show` |
| `SNTP sync failed` | no NTP reachability | allow UDP/123; TLS will fail without correct time |
| TLS connect fails after provisioning | certificate not registered, or wrong account config pasted | re-check the Self-Signed cert and `iotcDeviceConfig.json` |
| doubles print as `*float*` | picolibc float printf disabled | `CONFIG_PICOLIBC_IO_FLOAT=y` (set in prj.conf) |

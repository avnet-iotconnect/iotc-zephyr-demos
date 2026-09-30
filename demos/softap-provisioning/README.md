# Soft-AP Provisioning

Out-of-box onboarding with **no serial console and no toolchain**: an
unprovisioned device raises its own Wi-Fi access point and serves a
one-page web portal where a phone or laptop completes the entire
/IOTCONNECT onboarding. Provisioned devices skip the portal and run the
normal quickstart telemetry loop.

This matches the commissioning model of consumer and industrial products
(a meter, a thermostat): the installer never opens a terminal.

## Flow

1. Flash and power the board. With no stored Wi-Fi credentials or
   identity, it starts an **open access point** named `IOTC-RW612-XXXX`
   and prints the same on the console.
2. Connect a phone to that network and browse to **http://192.168.4.1**.
3. The portal walks through the same four steps as the serial quickstart:
   - **Home Wi-Fi** — SSID + passphrase, stored in flash
     (`wifi_credentials`).
   - **Device identity** — pick a Unique ID; the EC P-256 key pair is
     generated **on the device** and never leaves it. The page shows the
     device certificate to paste into /IOTCONNECT (Create Device,
     Self-Signed).
   - **Cloud account** — paste the `iotcDeviceConfig.json` downloaded
     from the device's Info panel. The discovery host in the file is
     stored too, so the same binary works on any /IOTCONNECT instance.
   - **Finish** — the device reboots, joins the home network as a
     station, and connects to /IOTCONNECT.
4. Telemetry appears under the device (template
   [`zephyr-telemetry-template.json`](../../templates/zephyr-telemetry-template.json)).

The serial path (`iotcprov provision`, `iotc config`) remains available
at every point.

## Boards

| Board | Status |
|---|---|
| FRDM-RW612 (`frdm_rw612`) | builds; hardware verification pending |

Requires a Wi-Fi driver with Soft-AP support (`CONFIG_NXP_WIFI_SOFTAP_SUPPORT`
on the RW612).

## Build

```sh
west build -p always -b frdm_rw612 -d build/softap_prov demos/softap-provisioning
west flash -d build/softap_prov
```

## Security notes

- The setup network is open and unencrypted by design (phones join it
  without friction) and exists **only while the device is unprovisioned**;
  everything sensitive that crosses it is either public (certificate,
  cpid/env) or immediately at rest in flash (Wi-Fi passphrase). For
  production, WPA2 on the setup AP with a per-device password printed on
  the label is a small configuration change.
- The device private key is generated on-chip and never transits the
  portal; only the public certificate is shown.

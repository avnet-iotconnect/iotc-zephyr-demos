# Quickstart — Prebuilt Image to /IOTCONNECT

Get a board onto Avnet /IOTCONNECT with a downloaded image — no Zephyr
toolchain, no compiled-in credentials. The image is flashed once; the Wi-Fi
network (where applicable) and the device identity are provisioned from the
board's serial console and persist in flash.

## 1. Prepare the host

You need a flashing tool (NXP LinkServer or SEGGER J-Link) and a serial
terminal. Starting from a bare Windows or Linux machine,
[boards/HOST-SETUP.md](boards/HOST-SETUP.md) walks through choosing and
installing either tool and opening the console.

## 2. Download the image for your board

All prebuilt images are on the
[Releases page](https://github.com/avnet-iotconnect/iotc-zephyr-demos/releases)
(`SHA256SUMS.txt` lists their checksums). Flash addresses are embedded in
each `.hex` — no load address needed.

| Board | Image | Board quickstart |
|---|---|---|
| NXP FRDM-RW612 (Wi-Fi) | `frdm_rw612_quickstart.hex` — plus telemetry, c2d-led and click-telemetry images | [boards/frdm-rw612](boards/frdm-rw612/QUICKSTART.md) |
| NXP FRDM-MCXN947 (Ethernet) | `frdm_mcxn947_quickstart.hex` | [boards/frdm-mcxn947](boards/frdm-mcxn947/QUICKSTART.md) |
| NXP MIMXRT1170-EVKB (Ethernet) | `mimxrt1170_evkb_quickstart.hex` | [boards/mimxrt1170-evkb](boards/mimxrt1170-evkb/QUICKSTART.md) |
| Microchip SAM E54 Xplained Pro (Ethernet) | `same54_xpro_quickstart.hex` | [boards/sam-e54-xpro](boards/sam-e54-xpro/QUICKSTART.md) |
| NXP FRDM-i.MX93 (Ethernet) | build from source (SPSDK SD image) | [boards/frdm-imx93](boards/frdm-imx93/QUICKSTART.md) |
| NXP FRDM-MCXE31B / FRDM-MCXW72 (no IP — gateway children) | build from source | [frdm-mcxe31b](boards/frdm-mcxe31b/QUICKSTART.md) / [frdm-mcxw72](boards/frdm-mcxw72/QUICKSTART.md) |

## 3. Flash

Each board quickstart gives the exact command; the pattern is:

```sh
LinkServer flash <device>:<board> load <image>.hex
# or, with a J-Link, from the J-Link Commander prompt:
loadfile <image>.hex
```

## 4. Provision at the console

Open the board's serial port at 115200 8N1. An unprovisioned device prints
its own provisioning guide. The flow (details and per-board variations in
the board quickstart):

1. **Wi-Fi boards only** — store the network credentials:
   `wifi cred add -s "<ssid>" -k 1 -p "<passphrase>"`
2. Generate the device identity on-chip: `iotcprov provision <your-duid>`
   — the private key never leaves the device; only the certificate is
   printed.
3. In /IOTCONNECT, import the device template for the demonstration you
   flashed (Devices → Templates → Import; the template is fixed at device
   creation — see the table in [DEMOS.md](DEMOS.md)), then Create Device:
   Unique ID = your DUID, Self-Signed authentication, paste the printed
   certificate.
4. Download the device's `iotcDeviceConfig.json` and paste it at the
   prompt after `iotc config`.
5. `kernel reboot cold`

## 5. Expected result

The console shows the network coming up, an SNTP time sync, IOTCONNECT
discovery, and `MQTT connected` — and the device's live data appears in
/IOTCONNECT within seconds. The identity survives reflashes and power
cycles; on boards with multiple prebuilt images, provision once and every
image connects as the same device.

To go beyond the quickstart image, pick a demonstration in
[DEMOS.md](DEMOS.md); to build anything from source, see the
[developer guide](DEVELOPER_GUIDE.md).

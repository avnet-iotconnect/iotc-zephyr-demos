# Kinetis-M Meter Demo — Connection and Walkthrough

How to wire an external metrology board to the FRDM-RW612 and run the
full demo: phone onboarding, meter telemetry, LED commands, and OTA.

## 1. Hardware

| Item | Role |
|---|---|
| NXP FRDM-RW612 | Wi-Fi host, runs this firmware |
| Metrology board (e.g. NXP Kinetis-M reference design) | Measures and streams meter data over UART |
| 3 jumper wires | UART cross-over + ground |
| USB-C | Power (and optional console) for the FRDM-RW612 |

### Wiring

The meter connects to the FRDM-RW612 **mikroBUS** socket UART
(FLEXCOMM0). The console is a separate port (MCU-Link USB), so both can
be used at once.

| FRDM-RW612 mikroBUS pin | Direction | Metrology board |
|---|---|---|
| RX | <-- | UART TX |
| TX | --> | UART RX (optional, unused by this demo) |
| GND | --- | GND |

3.3 V logic levels. Both boards power from their own USB.

### Meter data format

The metrology firmware prints one JSON object per line at **115200 8N1**:

```json
{"va":119.8,"vb":120.1,"ia":0.81,"ib":0.79,"ptot":181.2,"kwh":22.244,"freq":60.01}
```

Keys are optional per line; unknown keys are ignored. Any MCU that can
print this line works — for the Kinetis-M reference design, add one
`printf` of the measured values to its main loop. To try the demo with
no meter at all, connect a USB-UART adapter instead and paste the line
above into a terminal.

## 2. Create the template and device

1. Import [`templates/kinetis-meter-template.json`](../../templates/kinetis-meter-template.json)
   (Devices -> Templates -> Import). If the import reports a duplicate
   message code, edit the `msgCode` value in the file to any unused
   7 characters.
2. No device creation needed up front — the onboarding portal does it.

## 3. Flash and onboard (no console needed)

Flash the firmware (or the FOTA base image from the release for the OTA
exercise):

```sh
west build -p always -b frdm_rw612 -d build/kinetis_meter demos/kinetis-meter
west flash -d build/kinetis_meter
```

1. Power the board. It raises Wi-Fi network `IOTC-RW612-XXXX`.
2. Phone -> join that network -> browse **http://192.168.4.1**.
3. Follow the portal: home Wi-Fi, generate identity, create the device
   in /IOTCONNECT (template: **Kinetis-M Meter**), paste the
   config JSON, Finish.

## 4. What to show

- **Meter data**: `meter.va/vb/ia/ib/ptot/kwh/freq` update every 10 s
  while frames arrive; `meter.online` drops to 0 within 15 s of
  unplugging the meter UART.
- **Onboard temperature**: `temp_c` from the P3T1755 sensor. Touch the
  sensor to move it.
- **LED command**: send `led-on` / `led-off` / `led-toggle` from the
  device's Command panel; the board LED follows and the `led` attribute
  confirms on the next telemetry.
- **Wi-Fi recovery**: take the router down (or change its passphrase);
  after 90 s the setup AP returns with identity intact.
- **OTA**: upload a higher-version signed payload as a Firmware entry
  and push; `sys.fw` reports the new version after the MCUboot swap.
  Full flow: [DEVELOPER_GUIDE — FOTA](../../DEVELOPER_GUIDE.md#firmware-updates-over-the-air-fota).

## 5. OTA images for this demo

Built with sysbuild; three versions let the update be exercised twice:

| Artifact | Role |
|---|---|
| `frdm_rw612_kinetis-meter_fota_base_v1.0.0.hex` | Flash once over the probe (MCUboot + signed v1.0.0) |
| `frdm_rw612_kinetis-meter_fota_v1.0.1.signed.bin` | First OTA payload |
| `frdm_rw612_kinetis-meter_fota_v1.0.2.signed.bin` | Second OTA payload |

Evaluation images are signed with MCUboot's development key; production
uses your own key (see the developer guide's "Signing keys" section).

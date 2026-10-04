# Kinetis-M Meter

The NXP Kinetis-M meter-host architecture in one firmware, on the FRDM-RW612:

- **Soft-AP onboarding** — phone-browser provisioning, no console
- **Meter ingest** — an external metrology board streams readings over
  UART; published as `meter.*` telemetry
- **Onboard temperature** — P3T1755 sensor as `temp_c`
- **Cloud-to-device LED** — `led-on` / `led-off` / `led-toggle`
- **OTA** — MCUboot signed-image updates when built with sysbuild
- **Wi-Fi recovery** — setup AP returns if the stored network is
  unreachable for 90 s

Wiring, the meter data contract, and the full demo walkthrough are in
**[METER-CONNECTION.md](METER-CONNECTION.md)**.

Template: [`kinetis-meter-template.json`](../../templates/kinetis-meter-template.json).

## Build

```sh
west build -p always -b frdm_rw612 -d build/kinetis_meter demos/kinetis-meter
```

OTA chain (see the
[developer guide](../../DEVELOPER_GUIDE.md#firmware-updates-over-the-air-fota)):

```sh
west build --sysbuild -p always -b frdm_rw612 -d build/kinetis_meter_fota demos/kinetis-meter -- \
    -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
    -Dkinetis-meter_CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"1.0.0+0\"
```

| Board | Status |
|---|---|
| FRDM-RW612 (`frdm_rw612`) | builds; hardware verification pending |

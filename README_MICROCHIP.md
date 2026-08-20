# /IOTCONNECT Zephyr Demonstrations for Microchip

## Contents

- [SAM E54 Xplained Pro](#sam-e54-xplained-pro)
- [Planned](#planned)
- [Building](#building)

## Boards

### SAM E54 Xplained Pro

Ethernet via the on-chip GMAC and KSZ8091 PHY, with the factory MAC address
read from the onboard AT24MAC402 EEPROM. The demos target Zephyr's
`same54_xpro` board (the Atmel-tree port, which has working Ethernet);
Microchip's newer `sam_e54_xpro` tree does not carry a GMAC node yet, so
migrate when that lands.

Quickstart: [boards/sam-e54-xpro/QUICKSTART.md](boards/sam-e54-xpro/QUICKSTART.md)

| Demo | Status |
|---|---|
| [ml-model-update](demos/ml-model-update) | hardware-verified end to end: on-device ML on the IO1 sensors, models hot-swapped from the cloud (IOTCONNECT AI Model push and C2D commands) |
| [quickstart](demos/quickstart) | provisioning flow hardware-verified (on-device keygen, identity in NVS) |
| [telemetry](demos/telemetry) | builds |
| [c2d-led](demos/c2d-led) | builds |
| [click-telemetry](demos/click-telemetry) | builds |

Sensor wing: the IO1 Xplained Pro on connector EXT1 provides the temperature
sensor and light sensor used by ml-model-update (click-telemetry also
auto-detects its AT30TSE758). EXT1's I2C is a private bus (SERCOM3); EXT2 and
EXT3 share SERCOM7 with the onboard EEPROM and ATECC508. For MikroE Click
sensors, use a mikroBUS Xplained Pro adapter.

One board-specific constraint: the SERCOM I2C clocking on this board cannot
reach 100 kHz standard mode (the divider bottoms out around 235 kHz), so
I2C devices are driven in fast mode. Devices limited to 100 kHz will not
enumerate.

## Planned

A BLE tier — WBZ451HPE Curiosity beacons feeding a SAMA7D65 Curiosity
gateway — is deferred until Microchip's WBZ45x Zephyr support lands
(tracked in zephyrproject-rtos/zephyr#92168). Zephyr's `sama7d65_curiosity`
port has no networking support yet, so that gateway is currently a Linux
target rather than a Zephyr one.

## Building

```sh
west build -p always -b same54_xpro -d build/mlupd demos/ml-model-update \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```

# face-detect — on-device face detection to /IOTCONNECT over cellular

Real-time face detection on the FRDM-MCXN947: the OV7670 camera streams QVGA
video through the SmartDMA engine, a TFLM model detects faces, detection boxes
render live on the LCD, and the detection count publishes to Avnet /IOTCONNECT
over an LTE IoT 12 Click cellular modem.

This is a port of
[iotc-mcx-zephyr-demos](https://github.com/avnet-iotconnect/iotc-mcx-zephyr-demos)
onto upstream Zephyr and the
[IOTCONNECT Zephyr SDK](https://github.com/avnet-iotconnect/iotc-zephyr-sdk):
the vendored protocol/transport glue is replaced by the SDK, and device
identity uses the quickstart provisioning flow (key generated on-chip, nothing
secret in the binary).

## Hardware

| Item | Notes |
|---|---|
| FRDM-MCXN947 | **Reworked per NXP's camera instructions** (moved solder jumpers). The rework disconnects the onboard Ethernet — hence cellular. |
| OV7670 camera module | On the DVP 20-pin connector (`dvp_20pin_ov7670` shield) |
| LCD-PAR-S035 display | On the parallel LCD connector (`lcd_par_s035_8080` shield) |
| MikroE LTE IoT 12 Click | In the mikroBUS socket, TX/RX pins bent up and wired to **P0_26 / P0_27**; modem power key on **P1_3** (see the original repo's wiring photos) |
| Nano SIM | Set `CONFIG_MODEM_CELLULAR_APN` in [prj.conf](prj.conf) for your carrier |

## Model

The port runs the software TFLM model with CMSIS-NN kernels, built from
source via the upstream `tflite-micro` module. The original NPU-accelerated
variant requires NXP's eIQ Neutron TFLM middleware (prebuilt binaries not
available in upstream Zephyr) and is not built here.

## Build

```sh
west build -p always -b frdm_mcxn947/mcxn947/cpu0 -d build/facedet \
  iotc-zephyr-demos/vendor/nxp/face-detect \
  -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
west flash -d build/facedet
```

## Onboard

Import the device template
([templates/mcxn947-facedet-device-template.JSON](../../../templates/mcxn947-facedet-device-template.JSON)),
then provision from the serial prompt exactly like the
[quickstart](../../../demos/quickstart):

```
iotcprov provision <your-duid>     # key + self-signed cert generated on-chip
# Create Device (Self-Signed) in /IOTCONNECT, paste the printed certificate
iotc config                        # paste iotcDeviceConfig.json
kernel reboot cold
```

## Telemetry

| Attribute | Meaning |
|---|---|
| `faces` | number of faces currently detected |
| `local_timestamp` | device UTC time of the report |

Reports are rate-limited to one per `CONFIG_IOTC_MQTT_DEVICE_REPORT_SEC`
(default 5 s), with a periodic zero heartbeat when nothing is detected.

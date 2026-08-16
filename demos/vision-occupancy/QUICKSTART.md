# Vision occupancy — quickstart

The condensed path from an unopened MIMXRT1170-EVKB to live occupancy
telemetry. Background and the full walkthrough: [README.md](README.md) and
[DEMO.md](DEMO.md); board flashing/provisioning details:
[RT1170-EVKB quickstart](../../boards/mimxrt1170-evkb/QUICKSTART.md).

## 1. Hardware (5 min)

1. Connect the **OV5640 camera module** (in the kit box) to **J2** with the
   bundled 44-pin cable; check the connector is latched flat.
2. Ethernet into the RJ45 (a DHCP network). USB-C into the MCU-Link port.
3. Serial terminal on the MCU-Link VCom, **115200 8N1**.

## 2. Build + flash

From a Zephyr v4.4.1 workspace (tflite-micro module enabled — see README):

```sh
west build -p always -b mimxrt1170_evk/mimxrt1176/cm7 --shield nxp_btb44_ov5640 \
  -d build/vision_occ C:/dev/zephyr/iotc-zephyr-demos/demos/vision-occupancy \
  -- -DZEPHYR_EXTRA_MODULES=C:/dev/zephyr/iotc-zephyr-sdk \
     -DZEPHYR_IOTC_C_LIB_MODULE_DIR=C:/dev/zephyr/iotc-c-lib
west flash -d build/vision_occ
```

**Bench check (no cloud needed):** on reset the console prints the camera
format and a `self-test: person=..% clear=..%` line from a real captured
frame. Stand in front of the camera during reset and watch the person score.

## 3. Onboard to IOTCONNECT (once)

1. Import
   [templates/vision-occupancy-template.json](../../templates/vision-occupancy-template.json)
   (**Devices → Device → Templates → Create Template → Import**).
2. At the device serial prompt:
   ```
   iotcprov provision <your-duid>
   ```
   → **Devices → Create Device**, Unique ID `<your-duid>`, auth
   **Self-Signed**, paste the printed certificate.
3. Download the device's `iotcDeviceConfig.json`, then at the prompt:
   ```
   iotc config
   { ...paste the json... }
   kernel reboot cold
   ```

## 4. Run the demo

- Walk into the frame → `vision.state` flips to `occupied` (LED on), out →
  `clear`. Publishes every 10 s and instantly on a flip.
- **Snapshot:** send `snapshot` from the device's command console; the frame
  appears as a PNG card in the device's **Telemetry Files** panel within a
  few seconds, Classification showing the live occupancy verdict.
- **Model push:** `python tools/pack_model.py <model>.tflite --version 2
  --name person-v2`, upload the produced `.zip` under **Devices → AI
  Models**, push to the device, and watch `model.ver`/`model.src` flip live.
  Power-cycle: the pushed model persists (`model.src: flash`).
- **Tune:** `threshold <pct>`, `interval <sec>`, `model-info`, `model-reset`.

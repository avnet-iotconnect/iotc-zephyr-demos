# Developer Guide

Building the demonstrations from source, adding boards, and working on the
stack. To just run a prebuilt image, use the [quickstart](QUICKSTART.md)
instead.

## Prerequisites

- Python 3 with [west](https://docs.zephyrproject.org/latest/develop/west/index.html)
  (`pip install west`)
- The [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
  (≥ 1.0 for Zephyr 4.4; install at least the `arm-zephyr-eabi` toolchain;
  the i.MX93 A55 target additionally needs `aarch64-zephyr-elf`)
- A flashing tool and serial terminal
  ([boards/HOST-SETUP.md](boards/HOST-SETUP.md))

## Workspace

This repository is a west manifest. The simplest setup is a fresh workspace
with everything pinned by [west.yml](west.yml) — Zephyr v4.4.1, the
[IOTCONNECT Zephyr SDK](https://github.com/avnet-iotconnect/iotc-zephyr-sdk)
and [iotc-c-lib](https://github.com/avnet-iotconnect/iotc-c-lib) as modules:

```sh
west init -m https://github.com/avnet-iotconnect/iotc-zephyr-demos <workspace>
cd <workspace>
west update
west zephyr-export
```

Alternatively, build from an existing Zephyr 4.4 workspace by pointing the
build at checkouts of the two modules:

```sh
west build ... -- -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk \
                  -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib
```

In a manifest-created workspace those two `-D` flags are unnecessary —
everywhere a demo document shows them, they can be dropped.

## Building

```sh
west build -p always -b <board-target> -d build/<name> demos/<demo>
west flash -d build/<name>
```

Board targets: `frdm_rw612`, `frdm_mcxn947/mcxn947/cpu0` (add `/ns` for the
TF-M build), `mimxrt1170_evk/mimxrt1176/cm7`, `same54_xpro`,
`frdm_imx93/mimx9352/a55`, `frdm_mcxe31b`, `frdm_mcxw72/mcxw727c/cpu0`.

Per-board notes:

- **FRDM-RW612 (Wi-Fi)** — fetch the radio firmware blob once per
  workspace: `west blobs fetch hal_nxp`. It links into the image (~700 KB).
- **FRDM-MCXN947 `/ns` (TF-M)** — needs one-time host tooling and module
  patches; see "Hardware-backed key protection" in
  [boards/frdm-mcxn947/QUICKSTART.md](boards/frdm-mcxn947/QUICKSTART.md).
- **FRDM-i.MX93** — bare-metal A55; needs `aarch64-zephyr-elf` and
  [SPSDK](https://github.com/nxp-mcuxpresso/spsdk) to build the bootable SD
  image (`-DUSE_NXP_SPSDK_IMAGE=y`); see
  [boards/frdm-imx93/QUICKSTART.md](boards/frdm-imx93/QUICKSTART.md).
- **npu-benchmark** — additionally needs NXP's eIQ TFLM middleware
  (`-DTFLITE_DIR=...`); see
  [vendor/nxp/npu-benchmark](vendor/nxp/npu-benchmark/README.md).

## Device identity

Two models, chosen per board configuration:

- **Runtime (NVS)** — `CONFIG_IOTCONNECT_IDENTITY_NVS` +
  `CONFIG_IOTCONNECT_ONDEVICE_KEYGEN`: the device generates its key on-chip
  and stores the identity in flash settings; provisioning happens at the
  serial console (`iotcprov` / `iotc` commands). Binaries carry only public
  CA roots and are safe to distribute. Used by the quickstart demo on every
  board, by every demo on the FRDM-RW612, and by the MCXN947 TF-M builds
  (key sealed in PSA Protected Storage).
- **Compiled-in** — the identity is baked into the binary: generate the
  git-ignored credentials header from the device's certificate + private
  key PEM pair and set `CONFIG_IOTCONNECT_CPID/ENV/DUID` in the demo's
  `prj.conf`:
  ```sh
  python <path>/iotc-zephyr-sdk/tools/gen_device_credentials.py \
      device-cert.pem device-key.pem
  ```
  Full steps in the
  [telemetry demo README](demos/telemetry/README.md#device-identity-two-ways).
  Never commit or distribute a binary built this way — it contains the
  private key.

The SDK's key-protection matrix (which boards can seal the key in hardware)
is in the
[SDK documentation](https://github.com/avnet-iotconnect/iotc-zephyr-sdk/blob/main/docs/provisioning-nvs.md#key-protection--tf-m-capability-per-board).

## Adding a board to a demo

Demos are written once; boards join through configuration only:

1. Add `demos/<demo>/boards/<board-target>.conf` with the bearer (Ethernet
   driver or Wi-Fi), DHCP, and any per-board sizing. Add a devicetree
   `.overlay` beside it only if the bearer or peripherals need one — see
   the existing files for the patterns (the RW612's Wi-Fi confs and the
   MCXN947's Ethernet overlay are the two references).
2. Add the board to the demo's `sample.yaml` `platform_allow` list and its
   README table, and to the manufacturer page (`README_NXP.md` /
   `README_MICROCHIP.md`) with an honest verification status — `builds`
   until it has run on hardware.
3. If the board is new to the repository, add a
   `boards/<board>/QUICKSTART.md` (photo, flashing, console, provisioning,
   expected result — [boards/frdm-rw612](boards/frdm-rw612/QUICKSTART.md)
   is the model).

## Firmware updates over the air (FOTA)

The SDK ships an OTA-to-MCUboot module
(`CONFIG_IOTCONNECT_OTA_MCUBOOT`, enabled by default whenever the image is
built under MCUboot). An OTA push from IOTCONNECT is downloaded over HTTPS
into the MCUboot secondary slot, the device reboots into a *test* swap, and
the new firmware confirms itself and reports success once it has
reconnected to the platform. If the new image fails to come up, MCUboot
reverts to the previous firmware on the next boot and the failure is
reported from there. Verified end-to-end on the FRDM-RW612.

Build the demo as an MCUboot chain with sysbuild (the RW612 board files
already carry MCUboot-compatible flash partitions):

```sh
export ZEPHYR_IOTC_C_LIB_MODULE_DIR=<path>/iotc-c-lib   # env var, not -D
west build --sysbuild -p always -b frdm_rw612 -d build/qs_fota demos/quickstart -- \
    -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
    -Dquickstart_CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"1.0.0+0\"
```

Flash both images once over the debug probe (`west flash`, or J-Link:
`mcuboot/zephyr/zephyr.bin` at the flash base plus
`quickstart/zephyr/zephyr.signed.hex`). From then on the board updates
itself.

To publish an update, rebuild with a higher
`CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION`, upload
`build/<name>/quickstart/zephyr/zephyr.signed.bin` as a Firmware entry in
IOTCONNECT (**Firmware** under your device's template; hardware version =
template major), and push it to the device. The console logs each stage:

```
iotc_ota: OTA requested from host ...s3.us-east-1.amazonaws.com
iotc_ota: OTA: downloading (HTTP 200)
iotc_ota: OTA image downloaded: 1002552 bytes
iotc_ota: OTA image staged; rebooting into MCUboot test swap
...
iotc_ota: OTA image confirmed; reporting success
```

Notes:

- The payload is always the **signed application image**
  (`zephyr.signed.bin`), never the hex and never the MCUboot binary.
- `ZEPHYR_IOTC_C_LIB_MODULE_DIR` must be exported as an environment
  variable for sysbuild — custom `-D` cache variables do not forward to
  the sysbuild images.
- The download shares TLS credentials with the broker/DRA connections; no
  extra certificates are needed for the platform's S3 download host.

## Releasing prebuilt images

Release images must be credential-free:

1. Build with the runtime-identity configuration (the quickstart demo, or a
   board config that enables `CONFIG_IOTCONNECT_IDENTITY_NVS`), with
   `CONFIG_BUILD_OUTPUT_HEX=y` so the artifact embeds its flash addresses.
2. Ensure no `device_credentials.h` was compiled in: the samples pick the
   header up automatically whenever it exists locally, so build release
   artifacts with it absent — and verify by scanning the artifact for the
   actual certificate/key base64 (mbedTLS legitimately embeds the
   `PRIVATE KEY` PEM marker string, so search for real key material, not
   the marker).
3. Name artifacts `<board>_<demo>.hex`, regenerate `SHA256SUMS.txt`, and
   attach both to the GitHub release.

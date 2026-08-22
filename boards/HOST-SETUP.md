# Host Setup — Flashing Tool and Serial Console

One-time host preparation for the board quickstarts, starting from a machine
with nothing installed. Covers Windows and Linux (LinkServer also supports
macOS; the Linux notes apply with `.pkg` installers and `/dev/tty.usbmodem*`
device names).

## Flashing tool: NXP LinkServer

LinkServer is NXP's free command-line flash and debug tool, and it supports
the onboard MCU-Link debug probe every quickstart in this repository uses.
Download the installer for your operating system from the
[LinkServer product page](https://www.nxp.com/design/software/development-software/mcuxpresso-software-and-tools-/linkserver-for-microcontrollers:LINKERSERVER)
(a free NXP account is required).

### Windows

1. Run the `.exe` installer. It installs to `C:\NXP\LinkServer_<version>\`
   and also installs the MCU-Link drivers and firmware tools.
2. Open a **new** terminal and verify:
   ```
   LinkServer --version
   ```
   If the command is not found, either re-run the installer and enable the
   add-to-PATH option, or call it by full path
   (`C:\NXP\LinkServer_<version>\LinkServer.exe`).

No separate USB driver step is needed on Windows 10/11 — the MCU-Link
enumerates automatically.

### Linux (Ubuntu/Debian x86-64)

1. Install the dependencies, then run the downloaded installer:
   ```sh
   sudo apt install libusb-1.0-0 dfu-util
   chmod a+x LinkServer_*.x86_64.deb.bin
   sudo ./LinkServer_*.x86_64.deb.bin
   ```
   It installs to `/usr/local/LinkServer_<version>/` (with a `LinkServer`
   symlink on the path) and sets up the udev rules for the MCU-Link.
2. Verify:
   ```sh
   LinkServer --version
   ```

### Check the probe is visible

With the board plugged into its MCU-Link USB port:

```sh
LinkServer probes
```

The probe appears with its serial number. If the list is empty:

- Use the board's **MCU-Link** USB connector (boards with two USB ports also
  have a plain device port — the quickstart names the right one).
- A probe reflashed with J-Link firmware will not appear — flash with
  SEGGER tools instead (`JLink` / `west flash --runner jlink`), or restore
  the MCU-Link CMSIS-DAP firmware with NXP's `MCU-LINK_installer`.

## Serial console

Every quickstart uses the MCU-Link's built-in USB serial port at
**115200 8N1**.

### Windows

The port appears in Device Manager under Ports (COM & LPT) as the highest
new `COM<n>` when you plug the board in. Open it with
[PuTTY](https://www.putty.org/), Tera Term, or any serial terminal at
115200 baud.

### Linux

The port appears as `/dev/ttyACM0` (or the next free number). Add yourself
to the `dialout` group once, then log out and back in:

```sh
sudo usermod -a -G dialout $USER
```

Open the console with any of:

```sh
screen /dev/ttyACM0 115200
# or
minicom -D /dev/ttyACM0 -b 115200
# or
picocom -b 115200 /dev/ttyACM0
```

Press Enter once after connecting — the Zephyr shell prompt (`uart:~$`)
confirms the console is live.

## J-Link alternative

All boards here also flash with a SEGGER J-Link (external, or the MCU-Link
reflashed with J-Link firmware): install the
[J-Link Software Pack](https://www.segger.com/downloads/jlink/), then use
`west flash --runner jlink` from a build tree, or J-Link Commander's
`loadfile` with the released `.hex` (addresses are embedded).

# Host Setup — Flashing Tool and Serial Console

One-time host preparation for the board quickstarts, starting from a machine
with nothing installed. Covers Windows and Linux (both tools also support
macOS; the Linux notes apply with `.pkg` installers and `/dev/tty.usbmodem*`
device names).

Two flashing tools work with the boards in this repository — pick one and
follow its section, then set up the [serial console](#serial-console):

| Tool | Choose it when |
|---|---|
| [NXP LinkServer](#option-a-nxp-linkserver) | The default. Works with every board's onboard MCU-Link debug probe as shipped (CMSIS-DAP firmware). Free download. |
| [SEGGER J-Link](#option-b-segger-j-link) | You already use J-Link tooling, have an external J-Link probe, or the board's MCU-Link has been reflashed with J-Link firmware (a reflashed probe is invisible to LinkServer). |

Either tool flashes the released `.hex` images directly — the flash addresses
are embedded, so no load address is needed.

## Option A: NXP LinkServer

LinkServer is NXP's free command-line flash and debug tool. Download the
installer for your operating system from the
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
- A probe reflashed with J-Link firmware will not appear — use
  [Option B](#option-b-segger-j-link), or restore the MCU-Link CMSIS-DAP
  firmware with NXP's `MCU-LINK_installer`.

Each board quickstart gives its exact flash command, in the form:

```sh
LinkServer flash <device>:<board> load <image>.hex
```

## Option B: SEGGER J-Link

1. Install the
   [J-Link Software Pack](https://www.segger.com/downloads/jlink/)
   (Windows `.exe`, Linux `.deb`/`.rpm`/`.tgz`; the Linux packages install
   the udev rules).
2. Connect either an external J-Link probe to the board's debug header, or
   the onboard MCU-Link running J-Link firmware, and verify the probe is
   seen:
   ```
   JLink -AutoConnect 1 -Device <device> -If SWD -Speed 4000
   ```
   (`<device>` is in the board quickstart, e.g. `RW612`; `exit` leaves the
   J-Link Commander prompt.)
3. Flash a released image from the J-Link Commander prompt with
   `loadfile <image>.hex`, or from a Zephyr build tree with
   `west flash --runner jlink`.

## Serial console

Every quickstart uses the debug probe's built-in USB serial port at
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

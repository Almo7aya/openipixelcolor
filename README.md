# openpanel32

Open-source firmware and a browser control page for 32 × 32 Bluetooth LED pixel panels: the cheap
square RGB panels that advertise as `LED_BLE_…` and are normally driven by the vendor's phone app.
The firmware is written from scratch in C for the panel's MVSilicon P2064AF2 chip (Andes NDS32 V3)
and is installed over Bluetooth through the panel's own update path. No programmer, no soldering.

> **Warning.** Installing firmware on these panels can leave one that will not take another update.
> Bluetooth is the only way in on a closed panel. Read [Safety](#safety) before installing anything.
> This software comes with no warranty (see [LICENSE](LICENSE)).

## What it does

- **20 animations:** plasma, fire, matrix rain, starfield, game of life, fireworks, ripples,
  sparkles, rainbow swirl, tunnel, snake, pong, bouncing balls, equalizer, aurora, lava lamp,
  rainstorm, DNA helix, warp speed, radar. Autoplay with an adjustable interval.
- **10 clock faces:** digital, analog, binary, big digits, plasma, flip, nixie, rings, sky (the
  sun and moon move with the time), pong. 12/24 h, optional date.
- **Text engine:** a playlist of up to 4 messages, 23 entry/exit effects, 11 hold effects,
  9 colour modes, shadow/outline/glow/rails, animated backgrounds, live `{time}` `{date}` tokens.
  Any language: the web UI draws the text with your computer's fonts (Arabic shaping and
  right-to-left included) and sends it as a picture strip.
- **Pictures and drawing:** send any image (resized to 32 × 32) or draw pixel by pixel.
- **Scoreboard, stopwatch, rhythm bars, test patterns.**
- **Remembers everything** across power cycles: settings, the last picture and the text are saved
  to flash a few seconds after the last change, in a wear-friendly log.
- **Compatible with the vendor app's basic commands** (brightness, clock, pixels, …) and with
  pypixelcolor. See [docs/PROTOCOL.md](docs/PROTOCOL.md).

## What's in the repository

```
firmware/        the firmware (C + a little assembly), Makefile, build scripts
webui/           browser control page (Web Bluetooth; Chrome or Edge)
tools/
  ble/           Bluetooth tools: scan, probe, install, safe update check, command-line control
  sim/           PC simulator: runs the firmware's app code and renders frames / runs flash tests
  fwcodec.py     encodes a built image into an update file (and back)
  check_image.py build-time checks of the image
  gen_*.py       generators for the font, gamma and lookup tables
docs/            HARDWARE.md (the board), PROTOCOL.md (the Bluetooth commands)
```

No vendor firmware, vendor code or vendor images are included, and none are needed to build.

## Requirements

- A panel with the P2064AF2 board (check with `tools/ble/ble_probe.py`, below).
- A computer with Bluetooth. Python 3.11+ with `pip install -r tools/requirements.txt`.
- For the web UI: Chrome or Edge (Web Bluetooth).
- To build the firmware: an `nds32le-elf` GCC toolchain (tested with GCC 15.2 and binutils 2.45.1;
  crosstool-NG can build one) and Python 3. On Windows the build runs inside WSL.
- To build the simulator: any host C compiler (gcc or clang).

## Getting started

### 1. Find your panel

```sh
python tools/ble/ble_scan_all.py          # look for LED_BLE_… with svc=fa00
python tools/ble/ble_probe.py <ADDRESS>   # read-only: shows the firmware version
```

The vendor firmware reports MCU version 3.x; openpanel32 reports 99.xx.

### 2. Build

```sh
cd firmware
./build.sh            # Linux / WSL; toolchain on PATH or in $NDS32_TOOLCHAIN
.\build.ps1           # Windows: runs build.sh in WSL ($env:OP32_WSL_DISTRO, default "Arch")
```

This produces `firmware/build/fw.bin` (the raw image) and `firmware/build/fw.ota.bin` (the update
file to install). The build checks the image (`tools/check_image.py`) and fails if the flash driver
leaves its cache window.

### 3. Install

From the command line (dry run first; it only checks the version):

```sh
python tools/ble/ota_flash_guarded.py <ADDRESS> firmware/build/fw.ota.bin --expect <version>
python tools/ble/ota_flash_guarded.py <ADDRESS> firmware/build/fw.ota.bin --expect <version> --go
python tools/ble/ble_probe.py <ADDRESS>   # should now say 99.xx
```

`--expect` is the version the panel reports right now (for example `3.17` for the vendor firmware, or
`99.17` when updating openpanel32). The tool stops if the panel reports anything else, and stops at
the first unexpected reply. Or use the **Firmware** section of the web UI.

After the last chunk the panel restarts and its bootloader installs the image.

### 4. Use it

```sh
python webui/serve.py                     # opens http://localhost:8321 in your browser
```

Click **Connect panel**. The panel accepts one Bluetooth connection at a time, so close other
pages or tools that are connected.

Command line instead:

```sh
python tools/ble/panelctl.py <ADDRESS> effect aurora
python tools/ble/panelctl.py <ADDRESS> clock 8          # sky clock, also syncs the time
python tools/ble/panelctl.py <ADDRESS> text "Hello" --rainbow
python tools/ble/panelctl.py <ADDRESS> image photo.png
python tools/ble/panelctl.py <ADDRESS> status
```

`python tools/ble/panelctl.py -h` lists every command.

## Safety

The panel has no accessible programming port, so the firmware itself is the only way to install the
next firmware. If a build cannot receive updates, the panel stays on that build. openpanel32 is
built so this is hard to get wrong:

- The Bluetooth link and the update receiver start before anything else (display, effects, saved
  settings), so a bug in those cannot block updates.
- **Rescue mode:** hold the panel's button while powering it on. Only Bluetooth and the update
  receiver run. The panel also enters rescue mode by itself when it keeps crashing within 10 s of
  starting (from the 4th such start in a row). Check the mode with `ble_probe.py`.
- Crashes restart the panel instead of hanging; the cause is reported by `ble_probe.py`.
- The flash driver can only write the update staging area and above. The bootloader and the
  running firmware cannot be overwritten.
- An image is only handed to the bootloader after its CRC matches and the staged copy has been read
  back from flash and checked.
- Saved settings are never applied after a crash restart, so a bad saved value cannot cause a
  crash loop.

**Before installing a build you changed**, and again right after installing it, run the safe update
check. It sends one chunk with a deliberately wrong overall CRC, so the panel erases and writes the
staging area, then rejects the update and installs nothing (it answers with status `00`, which is
expected, and restarts about 10 s later):

```sh
python tools/ble/ota_flash_probe.py <ADDRESS> --bytes 12288
```

Then prove the new build can update itself: install the same file once more with
`--expect <new version>`. This succeeds only if the staged copy reads back correctly, so a build that
cannot write flash fails here, not later. If the check or the reinstall fails, install a known-good
build at once, while the link still works.
Changes to `firmware/src/drv/iflash.c`, `firmware/src/app/ota.c`, `firmware/src/start.S`,
`firmware/link.ld` or the start-up order in `firmware/src/main.c` are the risky ones; test them most
carefully.

Going back to the vendor firmware works through the same update path, but needs a vendor update
file, which this project does not provide.

## Simulator

The app code (`firmware/src/app`) does not touch hardware, so it also runs on a PC:

```sh
gcc -O2 -std=gnu11 -Ifirmware/src/app tools/sim/sim.c \
    firmware/src/app/{gfx,effects,fx_more,modes,clockface,tables,font5x7,settings,textfx}.c -o sim
./sim settings                      # power-cycle tests of the settings storage on emulated flash
./sim                               # renders every effect, clock face and text effect to frames.bin
python tools/sim/make_sheet.py frames.bin sheet.png
```

## Contributing

Issues and pull requests are welcome, especially hardware findings (see the guesses in
[docs/HARDWARE.md](docs/HARDWARE.md)), new effects and clock faces. New effects go in
`firmware/src/app/fx_more.c` and the table in `effects.c`; add the name to the web UI's `EFFECTS`
list and `tools/ble/panelctl.py`. Please run the simulator tests and check the frames before sending
a change, and say whether you tested it on a panel.

## License

MIT, see [LICENSE](LICENSE). The update protocol and the bootloader's image-header format are
implemented only so that openpanel32 can be installed on, and removed from, panels people own.

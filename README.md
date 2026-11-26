# openipixelcolor

[![Firmware](https://github.com/Almo7aya/openipixelcolor/actions/workflows/firmware.yml/badge.svg)](https://github.com/Almo7aya/openipixelcolor/actions/workflows/firmware.yml)

Open-source firmware and a browser control page for 32 × 32 Bluetooth LED pixel panels: the cheap
square RGB panels that advertise as `LED_BLE_…` and are normally driven by the iPixel Color phone app.
The firmware is written from scratch in C for the panel's MVSilicon P2064AF2 chip (Andes NDS32 V3)
and is installed over Bluetooth through the panel's own update path. No programmer, no soldering.

**Try it: [almo7aya.github.io/openipixelcolor](https://almo7aya.github.io/openipixelcolor/)** in Chrome or
Edge on a computer with Bluetooth. Connect the panel, open **Settings**, click **Use the latest release**,
then **Install firmware** twice. Nothing to install on the computer.

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
- **Your own animations, stored on the panel:** bring a GIF, an animated WebP or PNG, a video or a
  series of pictures; draw one frame by frame; or generate one (animated emoji, or a formula for every
  LED). The web UI shrinks the frames to 32 × 32, reduces the colours and packs them. The panel keeps up to
  20 animations (640 KiB) and plays them by itself, one or all in turn, also after a power cycle.
- **Pictures and drawing:** send any image (resized to 32 × 32) or draw pixel by pixel.
- **Scoreboard, stopwatch, rhythm bars, test patterns.**
- **Remembers everything** across power cycles: settings, the last picture and the text are saved
  to flash a few seconds after the last change, in a wear-friendly log.
- **Compatible with the vendor app's basic commands** (brightness, clock, pixels, …) and with
  pypixelcolor. See [docs/PROTOCOL.md](docs/PROTOCOL.md).

## What's in the repository

```
firmware/        the firmware (C + a little assembly), Makefile, build scripts
webui/           browser control page (Web Bluetooth; Chrome or Edge): controls, animations, firmware install
  anim.js        animation file encoder/decoder (the decoder mirrors the firmware's player)
tools/
  ble/           Bluetooth tools: scan, probe, install, safe update check, command-line control
  sim/           PC simulator: runs the firmware's app code, renders frames, runs flash and animation tests
  ci/            checks the CI runs (web UI scripts)
  fwcodec.py     encodes a built image into an update file (and back)
  check_image.py build-time checks of the image
  gen_*.py       generators for the font, gamma and lookup tables
docs/            HARDWARE.md (the board), PROTOCOL.md (the Bluetooth commands)
.github/         CI: builds and tests every push; releases firmware changes; publishes the web page
```

Branch `gh-pages` holds what GitHub Pages serves: the web UI and the latest release's update file. The CI
writes it; don't edit it by hand.

No vendor firmware, vendor code or vendor images are included, and none are needed to build.

## Requirements

- A panel with the P2064AF2 board (check with `tools/ble/ble_probe.py`, below).
- A computer with Bluetooth and Chrome or Edge (Web Bluetooth). That's all for using the
  [hosted page](https://almo7aya.github.io/openipixelcolor/).
- For the command-line tools: Python 3.11+ with `pip install -r tools/requirements.txt`.
- To build the firmware: an `nds32le-elf` GCC toolchain (tested with GCC 15.2 and binutils 2.45.1) and
  Python 3. Arch Linux packages it: `pacman -S nds32le-elf-gcc nds32le-elf-binutils`; elsewhere
  crosstool-NG can build one. On Windows the build runs inside WSL. Or skip building: see below.
- To build the simulator: any host C compiler (gcc or clang).

## Getting started

### 1. Find your panel

```sh
python tools/ble/ble_scan_all.py          # look for LED_BLE_… with svc=fa00
python tools/ble/ble_probe.py <ADDRESS>   # read-only: shows the firmware version
```

The vendor firmware reports MCU version 3.x; openipixelcolor reports 99.xx.

### 2. Build

```sh
cd firmware
./build.sh            # Linux / WSL; toolchain on PATH or in $NDS32_TOOLCHAIN
.\build.ps1           # Windows: runs build.sh in WSL ($env:OIPC_WSL_DISTRO, default "Arch")
```

This produces `firmware/build/fw.bin` (the raw image) and `firmware/build/fw.ota.bin` (the update
file to install). The build checks the image (`tools/check_image.py`) and fails if the flash driver
leaves its cache window.

No toolchain? You don't need one. Every push to `main` that changes `firmware/` is built by
[GitHub Actions](https://github.com/Almo7aya/openipixelcolor/actions/workflows/firmware.yml) and
published on the [Releases](https://github.com/Almo7aya/openipixelcolor/releases) page (tag
`v<version>-<commit>`); the hosted web page always offers the newest one. Builds of other pushes and of
pull requests are kept as artifacts of their run.

### 3. Install

With the web UI (step 4 shows how to open it): connect the panel, open the **Settings** tab, click
**Use the latest release** (on the hosted page) or **Use the build on this computer** (with
`serve.py`), or drop `fw.bin` or `fw.ota.bin` on the page, then click **Install firmware** twice. The page sends the update, waits for the panel to restart, connects again by
itself and checks that the panel reports the new version. Then it installs the same build once more,
which proves the new firmware can install updates itself (see [Safety](#safety); you can turn this off
under **More options**). This works the same on a panel that still runs the vendor firmware.

From the command line instead (dry run first; it only checks the version):

```sh
python tools/ble/ota_flash_guarded.py <ADDRESS> firmware/build/fw.ota.bin --expect <version>
python tools/ble/ota_flash_guarded.py <ADDRESS> firmware/build/fw.ota.bin --expect <version> --go
python tools/ble/ble_probe.py <ADDRESS>   # should now say 99.xx
```

`--expect` is the version the panel reports right now (for example `3.17` for the vendor firmware, or
`99.17` when updating openipixelcolor). The tool stops if the panel reports anything else, and stops at
the first unexpected reply.

After the last chunk the panel restarts and its bootloader installs the image.

### 4. Use it

Open **[almo7aya.github.io/openipixelcolor](https://almo7aya.github.io/openipixelcolor/)**, or run the page
from your copy of the repository (needed to test changes to the page or to install your own build):

```sh
python webui/serve.py                     # opens http://localhost:8321 in your browser
```

Click **Connect panel**. The panel accepts one Bluetooth connection at a time, so close other
pages or tools that are connected. The tabs are Effects, Clock, Text, Animations, Pictures (pictures and
drawing), Extras (equalizer, scoreboard, stopwatch, test patterns) and Settings (firmware, factory
settings). `serve.py` only serves the page (Web Bluetooth needs `localhost` or HTTPS) and offers your
latest firmware build to it; everything else happens in the browser.

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
next firmware. If a build cannot receive updates, the panel stays on that build. openipixelcolor is
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
check (**Settings → More options → Check flash writing** in the web UI, or the tool below). It sends one chunk with a deliberately
wrong overall CRC, so the panel erases and writes the staging area, then rejects the update and installs
nothing (it answers with status `00`, which is expected, and restarts about 10 s later):

```sh
python tools/ble/ota_flash_probe.py <ADDRESS> --bytes 12288
```

Then prove the new build can update itself: install the same file once more (the web UI does this by
default, or `--expect <new version>` on the command line). This succeeds only if the staged copy reads
back correctly, so a build that cannot write flash fails here, not later. If the check or the reinstall fails, install a known-good
build at once, while the link still works.
Changes to `firmware/src/drv/iflash.c`, `firmware/src/app/ota.c`, `firmware/src/start.S`,
`firmware/link.ld` or the start-up order in `firmware/src/main.c` are the risky ones; test them most
carefully.

Going back to the vendor firmware works through the same update path (tested: openipixelcolor →
vendor 3.17 → openipixelcolor, all from the web UI), but needs a vendor update file, which this
project does not provide.

## Simulator

The app code (`firmware/src/app`) does not touch hardware, so it also runs on a PC:

```sh
gcc -O2 -std=gnu11 -Ifirmware/src/app tools/sim/sim.c \
    firmware/src/app/{gfx,effects,fx_more,modes,clockface,tables,font5x7,settings,textfx,anim}.c -o sim
./sim settings                      # power-cycle tests of the settings storage on emulated flash
node tools/sim/anim_vectors.js vec  # test animations made by the web UI's encoder (needs Node.js)
./sim anim vec                      # upload, store, play (pixel-exact against the web UI's decoder), damage
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
implemented only so that openipixelcolor can be installed on, and removed from, panels people own.

openipixelcolor is an independent project. It is not affiliated with or endorsed by the makers of the
iPixel Color app or of the panels.

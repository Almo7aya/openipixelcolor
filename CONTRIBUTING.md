# Contributing to openipixelcolor

Thanks for helping. Bug reports, hardware findings, new effects and clock faces, and ports to more iPixel Color
boards are all welcome.

## Reporting a problem

Open an issue with:

- what you did, what you expected and what happened;
- the panel's model and firmware: in the web app, **Device → Panel** (matrix size, type, firmware), or
  `python tools/ble/ble_probe.py <ADDRESS>`;
- your browser and operating system, for web app problems.

A panel that won't take updates any more is the serious case: say so in the title, and don't install anything
else on it before reading [Safety](README.md#safety).

## Making a change

1. Fork the repository and make a branch.
2. Build and test what you touched:
   - **Firmware:** `firmware/build.sh` (or `build.ps1` on Windows) must build without warnings
     (the CI builds with `-Werror`).
   - **App code** (`firmware/src/app`): run the simulator tests, `./sim settings` and `./sim anim vec`, and
     look at the frames (`./sim` and `tools/sim/make_sheet.py`). See [Building from source](README.md#building-from-source).
   - **Web app:** `node tools/ci/check_webui.js`, then try it with `python webui/serve.py`.
3. If your change touches the flash driver, the update path, the start-up code or the linker script, run the
   flash check before and after installing it on a panel, and install it twice ([Safety](README.md#safety)).
4. Open a pull request. Say what you changed and whether you tried it on a panel (which model).

Every push and pull request runs the CI: the firmware build, the simulator tests and the web app check. A
merge to `main` that changes `firmware/` publishes the next release.

## Where things go

- **A new effect:** `firmware/src/app/fx_more.c` and the table in `effects.c`; its name in `EFFECTS` in
  `webui/js/catalog.js` and in `tools/ble/panelctl.py`. Then regenerate the previews (`./sim previews`,
  `tools/sim/make_previews.py`) and `webui/panel.wasm` (`tools/wasm/build.py`).
- **A new clock face:** `firmware/src/app/clockface.c`, `CLOCKS` in `webui/js/catalog.js`, the previews.
- **A Bluetooth command:** handle it in `firmware/src/app/modes.c` (or `proto.c`), document it in
  [docs/PROTOCOL.md](docs/PROTOCOL.md), and add it to `webui/js/protocol.js`.
- **The web app:** `webui/js/main.js` lists every module and what it does.

## Porting to another iPixel Color board

iPixel Color panels come in many sizes (see [Matrix size](docs/PROTOCOL.md#matrix-size)), and each size may have
a different board. A port needs:

1. The board's hardware: the chip, the LED driver, the Bluetooth link and the update path (see
   [docs/HARDWARE.md](docs/HARDWARE.md) for how the 32 × 32 board was worked out, and
   [docs/OTHER-PANELS.md](docs/OTHER-PANELS.md) for what is known about other panels). Never write to a
   panel's flash before you know the update path works.
2. A `firmware/src/app/board.h` entry for it: size, board id, the model's type byte.
3. Layouts for its size: the screens, effects and clock faces are laid out for 32 × 32 today, and the build
   stops (`screens.c`) until a size has its own.

The web app already detects the size and makes pictures, drawings and animations at that size.

## Code style

- C11 for the firmware, no C library, no floating point in `firmware/src/app`; 4 spaces, lines up to about
  120 characters; a comment at the top of each file saying what it is for.
- The web app: plain ES modules, no build step, no npm packages; 2 spaces.
- Keep comments and messages short and plain.

## License

openipixelcolor is licensed under the [GNU General Public License, version 2](LICENSE). By contributing you agree
that your contribution is licensed the same way.

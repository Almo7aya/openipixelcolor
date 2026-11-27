# AGENTS.md

Notes for AI coding agents (and people) working in this repository. [README.md](README.md) explains the project
and [CONTRIBUTING.md](CONTRIBUTING.md) the basics of a change; this file adds what you need to work safely and
to finish a change completely.

## What this is

- **Firmware** in C for iPixel Color LED panels (32 × 32 today), on the panel's own Bluetooth chip. It is
  installed over Bluetooth only: the panel has no programming port, so a build that can't receive updates
  leaves the panel stuck on that build.
- **A web app** (`webui/`): plain HTML, CSS and ES modules, Web Bluetooth, hosted on GitHub Pages.
- **Tools:** a PC simulator of the app code, a WebAssembly build of it for the web app's live preview, and
  Python Bluetooth tools.

## Ground rules

1. **Never break the update path.** The risky files are `firmware/src/drv/iflash.c`, `firmware/src/app/ota.c`,
   `firmware/src/start.S`, `firmware/link.ld` and the start-up order in `firmware/src/main.c`. A change there
   needs the flash check before and after, and installing twice ([Safety](README.md#safety)).
2. **Don't install anything on a panel**, or send it commands, unless the person you work for asked you to.
3. **Numbers the panel saves never change.** Effects, clock faces, modes and command ids: add new ones at the
   end, never reorder or remove.
4. **No personal data in the repository:** no Bluetooth addresses, names, e-mail addresses or local network
   hosts. Docs write `<ADDRESS>`.
5. **No vendor code, stock firmware or vendor images.** Nothing here needs them.
6. **Ask before pushing.** A push to `main` that changes `firmware/` publishes the next release by itself
   (see Releases). Never force-push, and never delete tags or releases unless asked.
7. **No warnings.** The CI builds the firmware and the simulator with `-Werror` (`-Wall -Wextra`).

## Layout

```
firmware/src/app/   hardware-independent app code: modes, effects (fx_*.c), clock faces (clockface*.c),
                    text engine, animations, screens, settings; also built into the simulator and panel.wasm
firmware/src/drv/   internal and external flash, the LED display, the button, the Bluetooth link
firmware/src/hal/   the chip (timers, DMA, interrupts); src/lib/ small libraries; src/main.c, src/start.S start-up
webui/js/           the web app; main.js lists every module and what it is for
tools/sim/          simulator, its tests, preview and picture generators
tools/wasm/         builds webui/panel.wasm from firmware/src/app
tools/ble/          Bluetooth tools; tools/ci/ checks the CI runs
docs/               PROTOCOL.md (every command), HARDWARE.md (the board)
```

## Build and test

```sh
cd firmware && ./build.sh                  # or .\build.ps1 on Windows (WSL); -> build/fw.bin, build/fw.ota.bin

gcc -O2 -std=gnu11 -Wall -Wextra -Werror -Ifirmware/src/app tools/sim/sim.c \
    firmware/src/app/{gfx,effects,fx_more,fx_demo,fx_sim,fx_finals,fx_synth,emblem,modes,clockface,clockface2,clockface3,tables,font5x7,settings,textfx,anim,screens}.c -o sim
./sim settings                             # saved settings on emulated flash
node tools/sim/anim_vectors.js vec && ./sim anim vec   # stored animations, both flash chips
./sim previews && python tools/sim/make_previews.py previews.bin webui/previews
python tools/wasm/build.py                 # webui/panel.wasm (clang + wasm-ld)
node tools/ci/check_webui.js               # modules, imports, element ids, files
python webui/serve.py                      # the web app on localhost, with your local firmware build
```

Run `git fetch --tags` before a local firmware build: the version is one more than the newest tag `vN`
(without tags it comes out as 100).

Look at what you drew: `./sim previews` (or `./sim`) and `tools/sim/make_sheet.py` give frames you can view.
Most mistakes in effects and clock faces only show up there.

## Writing firmware app code

- Integer maths only, no floating point, no C library. C11 (`gnu11`), 4 spaces.
- RAM is tight (a few KB free). No large static or global arrays: an effect keeps its state in
  `fx_scratch()` (`FX_SCRATCH_BYTES`, shared, one effect at a time; `init()` resets it). Clock faces use the
  upper half of it. Better still, draw each frame from the time alone.
- `frame(t)` gets the time in ms and is called about 40 times a second; redrawing all 32 × 32 pixels is fine.
- Helpers: `gfx.h` (pixels, lines, text, 7-segment digits, clipping), `fx_util.h` (`SN`/`CS` sine and cosine
  with 256 steps a turn, `fx_mix`, `fx_isqrt`, `fx_hash`), `tables.h` (`sin8`, `ang32`, `dist32`, `font5x7`).
- The 3 × 5 font has only `0-9 A-Z space : . - ! ? ' / % + $`. Anything else draws nothing.
- With `-Wextra`: no unused parameters, watch signed/unsigned comparisons, one statement per `if` line.

## Checklists

**A new effect**
1. The code in a `firmware/src/app/fx_*.c` file, and an entry at the end of `effects[]` in `effects.c`.
2. A new `.c` file also goes in the simulator source list in `.github/workflows/firmware.yml`, the `gcc` line
   in README.md and `SOURCES` in `tools/wasm/build.py`. The Makefile finds it by itself.
3. Its name at the end of `EFFECTS` in `webui/js/catalog.js` and in `tools/ble/panelctl.py`.
4. `SINCE` in `webui/js/play.js`: the first new number and the release that brings it, so older panels get
   an update notice instead of a different effect.
5. The count and list in README.md (Features) and the numbered list in docs/PROTOCOL.md.
6. Simulator tests, previews (look at them), `panel.wasm`, `check_webui.js`, the firmware build.

The clock previews are drawn after the effects, so adding effects changes some clock preview files too. That
is expected: commit them.

**A new clock face:** `firmware/src/app/clockface3.c` (`CLOCK3_STYLES`), `CLOCK_STYLES` and the list in
`clockface.h`, `CLOCKS` in `catalog.js`, `SINCE`, README.md and PROTOCOL.md, previews (each face in four
ways: 24 or 12 hours, with or without the date). A face that only moves when the time changes should build
itself up a moment after it is picked, so its preview shows something.

**A Bluetooth command:** handle it in `firmware/src/app/modes.c` or `proto.c`, document it in
docs/PROTOCOL.md and add it to `webui/js/protocol.js`. Some 16-byte replies arrive twice: match replies by
their command bytes, not by order.

**The web app:** plain ES modules, no build step, no npm packages, 2 spaces. A new module goes in the list in
`js/main.js`. The CI stamps `?v=` versions on the files and writes the service worker's file list
(`tools/ci/stamp_site.py`); on localhost there is no service worker.

## Releases

One version number: 100, 101, … The CI publishes a release `vN` when `firmware/` differs from the newest
release, then rebuilds the `gh-pages` branch (the hosted web app and `firmware/latest.json`). Don't edit
`gh-pages` by hand. A push that only changes the web app or the docs updates the site without a release.

## Trying it on a panel

Only with the person's go-ahead (rule 2). Run `python webui/serve.py`, connect the panel (Web Bluetooth
needs a click from the person: an agent can't pick the device for them), open **Device**, choose **Use the
build on this computer** and install. The install button asks for a second click within 8 seconds. After
the install, check the version the panel reports and that its stored animations are still there.

## Writing

Plain and short, in code comments, docs and the web app alike. Interface text names things the way people
think of them, in sentence case, with the same word for an action from button to message.

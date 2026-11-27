# Other panels

What is known about iPixel Color panels that openipixelcolor doesn't run on yet, collected for a future port
(see [Porting to another iPixel Color board](../CONTRIBUTING.md#porting-to-another-ipixel-color-board)).
openipixelcolor doesn't use any of this yet. Sources are listed at the end; facts from other projects are
described in our own words, no code is copied.

## Boards

| Board | Seen in | Firmware lives in | Recovery if an install goes wrong |
|---|---|---|---|
| MVSilicon P2064AF2 (NDS32) | 32 × 32 (this project) | the chip's own 2 MiB flash | none: no programming port, so a build that can't take updates stays |
| JieLi AC6951C (BR23 family) | a 16 × 32 and a 32 × 16 panel; probably also the 32-wide board mapped in iPixel-CFW #3 | probably an external SPI flash chip (ZBIT ZB25VQ32, 4 MiB) | the flash chip can be read and written with a clip (below) |
| unknown | 48 × 24, 192 × 32 | unknown | unknown |

The model byte names the size, not the board: two panels of the same size may have different boards. That is
why the web app checks the stock firmware's version (3.x) as well as the model byte before it installs.

## Firmware families

The stock firmware reports its version in the `0x8005` reply (bytes 4-5 the main chip, 6-7 the Bluetooth
module, each shown as major.minor).

| Panel | Model byte | Stock firmware | Takes the update path openipixelcolor uses (`0xC000`) | Source |
|---|---|---|---|---|
| 32 × 32, P2064AF2 | `81` | 3.x (3.17) | Yes: it is that firmware's own update path | this project |
| 48 × 24 | `89` | 23.09 (2025, Bluetooth module 1.15), later 23.11 | No: the panel never answered `0xC000`, nothing was installed | [issue #1](https://github.com/Almo7aya/openipixelcolor/issues/1), pypixelcolor #12 |
| 192 × 32 | `8F` | 29.12 (Bluetooth module 1.22) | Not tried | ipixel-ble |

The larger panels run another firmware line, probably on another chip with another bootloader. The 48 × 24
went from 23.09 to 23.11, so the iPixel Color app does update that family over Bluetooth: a recording of such
an update (an Android Bluetooth log, see pypixelcolor's guide below) would show how. Nothing should be
installed on these panels before their update path is known (the web app installs only on supported panels).

## JieLi boards

What others found on boards with the JieLi AC6951C (iPixel-CFW and pypixelcolor #13; the chip's markings are
sanded off, it was identified by its logo and pin-out):

- **Flash chip:** the pictures, and probably the firmware, live on the external SPI flash: disturbing its lines
  makes the panel show "Flash ERR". It can be read with a CH341A programmer and an 8-pin SOIC clip; hold the
  chip's `KEY` pin to ground first, which keeps the chip in reset and the LEDs off (iPixel-CFW's guide;
  skipping it damaged a panel). If the firmware is on it, a backup of that chip means a port to these boards
  can always be undone.
- **USB:** the USB pads show nothing on their own. JieLi chips enter a USB download mode when they see a
  special key (`0x16EF`) on the USB lines right at power-up (kagaimiq's jl-uboot-tool).
- **Button and pads:** `KEY` to ground while starting gives a production test mode. The button: short press
  rotates the picture, long press turns the panel off, double press shows demo scenes.
- **LEDs:** not quite HUB75, as far as was measured. Six FM6124 drivers take one red, one green and one blue
  data line from the chip for all 32 columns, and four TC7262 decoders switch the lines on in turn. A pin
  table, measured with a multimeter and maybe not exact, is in iPixel-CFW issue #3.
- **Tools:** JieLi's AC695N SDK, a partial AC6951 SDK with a Linux download tool, JieLi's tool pages, and
  kagaimiq's jielie notes on the chip family (iPixel-CFW issue #1). JieLi also has an Android app for
  Bluetooth updates; whether these panels accept it is not known.

## Model byte

Byte 4 of the `0x8001` reply names the model; the table is in [PROTOCOL.md](PROTOCOL.md#matrix-size). Seen on
real panels so far: `81` 32 × 32, `83` 64 × 16, `85` 64 × 20, `89` 48 × 24 and `8F` 192 × 32 (the last two
from ipixel-atom-rhythm).

## The reply to the time command (`0x8001`)

- Byte 4 is the model byte.
- Byte 10 is a password flag: `FF` means no password. Only the iPixel Color app checks the password: the
  panel still takes commands, the password travels in plain text, and the clear command removes it
  (pypixelcolor #63).
- Some panels don't always answer it. ipixel-atom-rhythm asks again every 2 s, for up to 6 s, before it sends
  anything else. The web app asks once and waits 4 s.

## How the stock firmware takes content

For reference, as ipixel-ble and ipixel-atom-rhythm describe it for panels of any size. Values are
little-endian, and every command starts with its 2-byte length.

- **Files** (text, pictures, GIFs, saved on the panel): the payload is split into frames of at most 12288
  bytes. Each frame starts with a 15-byte header: frame length (2), data type (2: `00 01` text, `02 00`
  picture, `03 00` GIF), option (1: 0 first frame, 2 the next ones), total payload length (4), CRC32 of the
  whole payload (4), a marker (1: 2 for a GIF, else 0) and the save slot (1). Pictures are PNG files and GIFs
  are GIF files at the panel's size. The panel answers each frame with 5 bytes starting `05`; the last byte is
  0 invalid command, 1 next frame, 2 no space, 3 complete.
- **Live frames** (shown, not saved): first `05 00 04 01 01` (live drawing on; `… 00` turns it off), then raw
  RGB, 3 bytes a pixel, row by row from the top, in blocks of at most 12288 bytes:
  `<length> 00 00 <0 first block, 2 the next ones> <width × height × 3, 4 bytes> <RGB>`. The panel answers
  `05 00 00 00 01` for the next block and `05 00 00 00 02` at the end of a picture. A 192 × 32 panel takes
  about 2.4 pictures a second. Without live drawing on, the panel shows its Bluetooth sign over each picture.
- **Pixels:** `<length> 05 01 00 R G B x0 y0 x1 y1 …`. Coordinates are single bytes, so panels wider than 255
  pixels can't be drawn this way.
- **Music bars:** `10 00 01 02 <style 0-4> <11 levels 0-15>`, drawn by the panel in its own layout (on 48 × 24:
  8 bars shown twice).
- **Writes:** large panels drop writes without response; write with response and wait for each answer (the
  web app already does).

Brightness (`0x8004`), power (`0x0107`) and the time are the same commands openipixelcolor accepts (see
[Commands compatible with the vendor app](PROTOCOL.md#commands-compatible-with-the-vendor-app)).

## What a port still needs

- Photos of the board with the chip markings readable (or, when they are sanded off, the chip's logo and
  pin-out).
- The update path of that firmware family: a recording of the iPixel Color app updating the panel, or, on a
  JieLi board, a backup of the flash chip and a way to write it.

Reports come in through the **Support for another panel** issue form, which the web app opens with the
panel's details filled in.

## Sources

- [Issue #1](https://github.com/Almo7aya/openipixelcolor/issues/1): a 48 × 24 panel on stock firmware 23.11.
- [RigsbyPaul/ipixel-ble](https://github.com/RigsbyPaul/ipixel-ble) (MIT): a Home Assistant integration for
  these panels, tested on 192 × 32 panels.
- [RigsbyPaul/ipixel-atom-rhythm](https://github.com/RigsbyPaul/ipixel-atom-rhythm): music bars on these panels
  from an ESP32-S3, tested on 48 × 24 and 192 × 32 panels. No licence file: only facts are taken from it.
- [lucagoc/pypixelcolor](https://github.com/lucagoc/pypixelcolor) (MIT): the model byte table; issues
  [#12](https://github.com/lucagoc/pypixelcolor/issues/12) (48 × 24 versions),
  [#13](https://github.com/lucagoc/pypixelcolor/issues/13) (the JieLi chip),
  [#63](https://github.com/lucagoc/pypixelcolor/issues/63) (the password); the
  [guide to Android Bluetooth logs](https://github.com/lucagoc/pypixelcolor/blob/main/docs/guides/android_ble_logs.md).
- [lucagoc/iPixel-CFW](https://github.com/lucagoc/iPixel-CFW): a custom firmware attempt for the JieLi boards,
  with the [flash chip guide](https://github.com/lucagoc/iPixel-CFW/blob/main/docs/dumping_spi_flash.md) and
  issues [#1](https://github.com/lucagoc/iPixel-CFW/issues/1) (tools),
  [#2](https://github.com/lucagoc/iPixel-CFW/issues/2) (USB) and
  [#3](https://github.com/lucagoc/iPixel-CFW/issues/3) (the LED wiring). No licence file: only facts.
- [kagaimiq/jielie](https://github.com/kagaimiq/jielie) and
  [kagaimiq/jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool): notes and tools for JieLi chips.

As of November 2026.

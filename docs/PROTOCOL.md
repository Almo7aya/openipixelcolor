# Bluetooth protocol

openpanel32 accepts the commands the vendor's phone app uses (so that app, and the open-source
pypixelcolor library, keep working for the basics), plus its own commands in the `0xF0xx` / `0xF1xx`
range for everything else.

## Transport

| | |
|---|---|
| Service | `0000fa00-0000-1000-8000-00805f9b34fb` |
| Write (with response) | `…fa02…` |
| Notify (replies) | `…fa03…` |

Only one connection at a time. A packet longer than one write (the tools use 244 bytes) is sent as
consecutive writes; the module puts it back together.

**Packet:** `u16 length` (whole packet, little-endian) · `u16 command` (little-endian) · payload.

**Reply:** `u16 length` · `u16 command` · `u8 status` (1 = done, 0 = refused) · data. A reply is at
most 16 bytes, and a 16-byte reply can arrive twice. Match replies to requests by bytes 2–3 (the
command) and ignore anything else.

Example: version query `04 00 05 80` → reply `08 00 05 80 63 11 xx yy` (MCU 99.17, module xx.yy).

## Commands compatible with the vendor app

| Cmd | Payload | Does |
|---|---|---|
| `0x8001` | `hh mm ss 00` | Sets the time; replies with the device type (`0x81` = 32×32) |
| `0x8003` | – | Clear: back to effects with autoplay |
| `0x8004` | `percent` | Brightness 0–100 |
| `0x8005` | – | Versions: reply `mcu_major mcu_minor module_major module_minor`. openpanel32 reports **99.xx**, the vendor firmware 3.x |
| `0x8006` | `0–3` | Orientation |
| `0x8008` | `x x n` | Show effect `n` (third byte) |
| `0x8009` | `0 stop / 1 start / 2 pause` | Stopwatch |
| `0x800A` | `p1 x p2` | Scoreboard |
| `0x0104` | `on` | Drawing mode on (clears the canvas) / off |
| `0x0105` | `0 r g b x y` | Set one pixel |
| `0x0106` | `style 24h show_date [yy mm dd weekday]` | Clock face `style` (0–9), optional date |
| `0x0107` | `on` | Power (display on/off) |
| `0x0200` | `t style` | Rhythm bars, animated |
| `0x0201` | `style l1 … l11` | Rhythm bars, levels 0–15 |
| `0xC000`–`0xC006` | | Firmware update and restart, see below |

## openpanel32 commands

| Cmd | Payload | Does |
|---|---|---|
| `0xF000` | – | Ping |
| `0xF0F1` | – | Diagnostics: reply `mode crashes exc_type exc_address(4) uptime_s(4)`; mode 0 normal, 1 rescue (key), 2 rescue (crash loop) |
| `0xF101` | `mode arg` | Switch mode: 0 effect, 1 clock, 2 text, 3 canvas, 4 rhythm, 5 score, 6 timer, 7 test pattern, 8 splash |
| `0xF102` | `r g b speed flags text…` | Scrolling text in the panel font (ASCII); flags bit 0 = rainbow |
| `0xF103` | 3072 bytes | Whole picture, 32×32 RGB, row by row |
| `0xF104` | `on secs_lo secs_hi` | Effect autoplay and its interval (minimum 3 s) |
| `0xF105` | n × `x y r g b` | Set many pixels |
| `0xF106` | – | Status, see below |
| `0xF107` | `1 short / 2 long` | Press the key remotely |
| `0xF108` | `A5` | Factory settings (the `A5` key avoids accidents) |
| `0xF109` | – | Saved-settings status, see below |
| `0xF10A` | `r g b speed flags h w_lo w_hi columns…` | One text strip (older single-message form) |
| `0xF10B` | see below | Text playlist with effects |

### 0xF106 status reply

`mode effect brightness power hh mm ss flags orientation clock_style autoplay_secs`

flags: bit 0 time synced, bit 1 autoplay, bit 2 24-hour clock, bit 3 show date.

### 0xF109 saved-settings reply

`sequence(4) next_slot saved flags last_result`

saved: bit 0 picture saved, bit 1 text saved. flags: bit 0 loaded at boot, bit 1 settings waiting
to be saved, bit 2 picture waiting, bit 3 writing stopped (during an update).

Settings, the picture and the text are saved about 3 s (picture about 8 s) after the last change,
so dragging a slider writes once.

### 0xF10B text playlist

Up to 4 messages, one per packet, sent in order. `idx 0` clears the old playlist; the last message
(`idx = count − 1`) starts it.

```
idx count kind style[16] then
  kind 0 (strip): h w_lo w_hi columns...   (ceil(h/8) bytes per column, bit 0 = top row)
  kind 1 (font):  scale text...            (scale 0 = 7 px, 1 = 14 px; ASCII with tokens)
```

A strip is text the app has drawn itself with any font and script (the web UI uses the browser,
which handles Arabic shaping and right-to-left order). Font messages use the panel's own font and may
contain the live tokens `{time}` `{secs}` `{date}` `{day}` `{year}`. All messages share a 3584-byte pool.

Style bytes:

| Byte | Meaning |
|---|---|
| 0 | Entry effect: 0 none, 1–4 scroll left/right/up/down, 5 typewriter, 6 wipe, 7 opening, 8 closing, 9 blinds, 10 dissolve, 11 mesh, 12 slice, 13 drop, 14 grow up, 15 grow down, 16 zoom, 17 fade, 18 rain, 19 explode, 20 laser, 21 flip, 22 matrix, 23 random |
| 1 | Hold effect: 0 still, 1 blink, 2 breathe, 3 wave, 4 bounce, 5 shake, 6 shine, 7 sparkle, 8 neon flicker, 9 jelly, 10 glitch |
| 2 | Exit effect (same list as entry) |
| 3 | Colour mode: 0 one colour, 1 rainbow, 2 gradient left→right, 3 gradient top→bottom, 4 rainbow top→bottom, 5 fire, 6 neon, 7 candy stripes, 8 a colour per word |
| 4–6 | Colour 1 (r g b) |
| 7–9 | Colour 2 (r g b) |
| 10 | Speed 1–10 |
| 11 | How long to stay, in ¼ s (0 = 2 s) |
| 12 | Align (0 middle, 1 top, 2 bottom) · right-to-left << 2 · decorations << 4 (1 shadow, 2 outline, 4 glow, 8 rails) |
| 13 | Background: 0 black, 1 colour 2, 2 + n = effect n |
| 14 | Background brightness |
| 15 | Repeat count |

The web UI's preview is a line-by-line port of `firmware/src/app/textfx.c`, so what it shows is what
the panel shows.

## Effects and clock faces

Effects (`0x8008`, `0xF101 0 n`): 0 plasma, 1 fire, 2 matrix, 3 starfield, 4 game of life,
5 fireworks, 6 ripples, 7 sparkles, 8 rainbow swirl, 9 tunnel, 10 snake, 11 pong, 12 bouncing balls,
13 equalizer, 14 aurora, 15 lava lamp, 16 rainstorm, 17 DNA helix, 18 warp speed, 19 radar.

Clock faces (`0x0106`): 0 digital, 1 analog, 2 binary, 3 big digits, 4 plasma, 5 flip, 6 nixie,
7 rings, 8 sky (sun and moon follow the time), 9 pong.

## Firmware update

Byte-compatible with the vendor firmware's update path, so the same tools install openpanel32 over the
vendor firmware, update openpanel32, or put a vendor image back.

1. **Encoding.** Update files are byte-substituted: `encoded = ((raw ^ 0x4A) + 0x8A) & 0xFF`.
   `tools/fwcodec.py` encodes and decodes; the build makes `firmware/build/fw.ota.bin`.
2. **Announce** `0xC000`: `count crc32(encoded image) size` (u8, u32, u32). Reply `05 00 00 C0 01`.
3. **Chunks** `0xC001`: `idx crc32(chunk) chunk_length data` with 12 KiB (0x3000) chunks. Chunk `idx`
   is decoded and written to `0x108000 + idx × 0x3000`. Reply `05 00 01 C0 01` for each chunk.
4. After the last chunk the panel checks the CRC of the whole image. Only if it matches does it write
   the update record at `0x1FF000` and restart; the bootloader then installs the image. With a wrong
   CRC nothing is installed.
5. During an update the panel restarts after 10 s without update traffic.

`0xC006` restarts the panel. `0xC002`/`0xC003` are acknowledged (module update mode), `0xC004` replies
as `0xC003` and restarts, `0xC005` is the module reporting its own version.

openpanel32 is stricter than required: the chunk count must match the size, chunk indexes must
arrive in order, every chunk's CRC is checked before it is written, and before writing the update
record the staged image is read back from flash and checked again. A firmware whose flash writes
silently fail therefore never asks the bootloader to install a broken image.

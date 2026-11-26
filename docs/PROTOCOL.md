# Bluetooth protocol

openipixelcolor accepts the commands the vendor's phone app uses (so that app, and the open-source
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

Example: version query `04 00 05 80` → reply `08 00 05 80 65 00 xx yy` (openipixelcolor 101, module xx.yy).

## Commands compatible with the vendor app

| Cmd | Payload | Does |
|---|---|---|
| `0x8001` | `hh mm ss 00` | Sets the time; replies with the device type in byte 4, which names the matrix size (see below). Every app sends it right after connecting: the first one after 8 s of silence shows the "connected" screen |
| `0x8003` | – | Clear: back to effects with autoplay |
| `0x8004` | `percent` | Brightness 0–100 |
| `0x8005` | – | Versions: reply `mcu_major mcu_minor module_major module_minor`. openipixelcolor reports its version number N (100 and up) as `N 0` (from 256 on: `255 N−255`); the vendor firmware reports 3.x, openipixelcolor builds before 100 reported 99.xx |
| `0x8006` | `0–3` | Orientation |
| `0x8008` | `x x n` | Show effect `n` (third byte) |
| `0x8009` | `0 stop / 1 start / 2 pause` | Stopwatch |
| `0x800A` | `p1 x p2` | Scoreboard |
| `0x0104` | `on` | Drawing mode on (clears the canvas) / off |
| `0x0105` | `0 r g b x y` | Set one pixel |
| `0x0106` | `style 24h show_date [yy mm dd weekday]` | Clock face `style` (0–25), optional date |
| `0x0107` | `on` | Power (display on/off) |
| `0x0200` | `t style` | Rhythm bars, animated |
| `0x0201` | `style l1 … l11` | Rhythm bars, levels 0–15 |
| `0xC000`–`0xC006` | | Firmware update and restart, see below |

## openipixelcolor commands

| Cmd | Payload | Does |
|---|---|---|
| `0xF000` | – | Ping |
| `0xF0F1` | – | Diagnostics: reply `mode crashes exc_type exc_address(4) uptime_s(4)`; mode 0 normal, 1 rescue (key), 2 rescue (crash loop) |
| `0xF101` | `mode arg` | Switch mode: 0 effect, 1 clock, 2 text, 3 canvas, 4 rhythm, 5 score, 6 timer, 7 test pattern, 8 boot screen, 9 animation (arg = slot, `FF` = all in turn), 10 welcome screen (shown on the first start) |
| `0xF102` | `r g b speed flags text…` | Scrolling text in the panel font (ASCII); flags bit 0 = rainbow |
| `0xF103` | 3072 bytes | Whole picture, 32×32 RGB, row by row |
| `0xF104` | `on secs_lo secs_hi` | Effect autoplay and its interval (minimum 3 s) |
| `0xF105` | n × `x y r g b` | Set many pixels |
| `0xF106` | – | Status, see below |
| `0xF107` | `1 short / 2 long` | Press the key remotely |
| `0xF108` | `A5` | Factory settings (the `A5` key avoids accidents) |
| `0xF109` | – | Saved-settings status, see below |
| `0xF113` | – | Panel info (101+): reply `width(2) height(2) board device_type` |
| `0xF114` | `sub [addr(3)]` | Extra memory chip, read only (104+). sub 0: `id(3) status_as_found status_now ok kib(2) ms(2)` (JEDEC id; a read-speed test); sub 2: `addr_hi` and the 10 bytes at `addr`. Status 0 = no chip |
| `0xF10A` | `r g b speed flags h w_lo w_hi columns…` | One text strip (older single-message form) |
| `0xF10B` | see below | Text playlist with effects |
| `0xF10C` | `slot length(4) flags` | Animation upload: begin. flags bit 0 = play it when stored. See [Animations](#animations) |
| `0xF10D` | `offset(4) data` | Animation upload: one piece of up to 4096 bytes |
| `0xF10E` | – | Animation store: what's in use |
| `0xF10F` | `slot part` | Animation details: size and checksum (part 0) or name (parts 1–3) |
| `0xF110` | `slot` | Delete the animation that starts in `slot` (`FF` = all) |
| `0xF111` | `slot speed` | Play an animation (`FF` = all in turn, `FE` = keep what plays); speed in tenths, 2–40 (10 = as made, 0 = unchanged) |
| `0xF112` | `slot offset(4)` | Read 10 bytes of a stored animation back (firmware 101+) |

### 0xF106 status reply

`mode effect brightness power hh mm ss flags orientation clock_style autoplay_secs`

flags: bit 0 time synced, bit 1 autoplay, bit 2 24-hour clock, bit 3 show date, bit 4 playing all animations.
In animation mode (9) `effect` is the slot on screen (`FF` = none stored).

### Matrix size

iPixel Color panels come in many sizes. Each one names its model in byte 4 of the `0x8001` reply, with
the vendor firmware and with openipixelcolor alike. The iPixel Color app's table, as open-source clients
read it (pypixelcolor `lib/device_info.py` and others; the web UI's copy is `webui/js/size.js`):

| type | size | type | size | type | size |
|---|---|---|---|---|---|
| `00`, `80` | 64×64 | `8A` | 64×32 | `94` | 16×16 |
| `81` | 32×32 | `8B`, `8D` | 96×32 | `95` | 64×64 |
| `82` | 32×16 | `8C` | 128×32 | `96` | 96×64 |
| `83` | 64×16 | `8E` | 160×32 | `97` | 128×64 |
| `84` | 96×16 | `8F` | 192×32 | `98` | 160×64 |
| `85` | 64×20 | `90` | 256×32 | `99` | 192×64 |
| `86` | 128×32 | `91` | 320×32 | `9A`–`9F` | 256–576×64 |
| `87` | 144×16 | `92` | 384×32 | `A0` | 640×64 |
| `88` | 192×16 | `93` | 448×32 | | |
| `89` | 48×24 | | | | |

Seen on real panels: `81`, `83`, `85`. openipixelcolor 101 and later also answer `0xF113` with the exact
`width(2) height(2)`, the board (1 = P2064AF2) and the type byte (`firmware/src/app/board.h`). Animation files
carry their size in the header (`w`, `h`); the panel takes only files made for its own size.

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

## Animations

Animations are files made by the app (the web UI makes them from GIFs, videos, pictures, its frame editor
or its generators: `webui/js/create/anim-codec.js`). The panel stores them in flash and plays them by itself.

**Storage.** Slots of 32 KiB; a bigger file uses the following slots as well. From openipixelcolor 104 on,
a panel with the extra memory chip (docs/HARDWARE.md) has 127 slots (4064 KiB) there; otherwise, and
before 104, 20 slots (640 KiB) in the main flash. On the first start of 104 the chip is erased (10–40 s,
in the background: the store answers "not ready" with flags bit 2 meanwhile), and animations from the
main flash are copied to the same slots, checked, and then removed there. The app picks free slots from the
`0xF10E` reply. An upload into slots that an older animation uses deletes that animation.

**Upload.**

1. `0xF10C slot length(4) flags`: reply status 1, or 0 and a reason byte.
2. `0xF10D offset(4) data`, the file in 4096-byte pieces, in order (the last one shorter). The panel
   writes each piece at once (in the main flash the picture goes dark for about half a second per piece;
   on the extra chip there is no pause; a progress bar shows in between). Reply `status offset(4) reason`: status 1 = more expected, 2 = stored, 0 = refused. The
   offset echo tells replies apart.
3. After the last piece the panel reads the file back from flash and checks its CRC. Only then does it
   mark it complete. A file that was not completely sent or did not check out is never used. If no piece
   arrives for 15 s, the upload is dropped.

Reasons: 3 bad request, 4 not ready (rescue mode, or the extra memory chip still being prepared), 5 no upload running, 6 wrong offset, 7 wrong piece
length, 8 bad header, 9 flash write failed, 10 read-back check failed.

**`0xF10E [page]` reply:** `slots slot_kb valid(4) used(4) flags`. `valid` has a bit for each slot where an
animation starts, `used` for each slot in use, for slots `32 × page` to `32 × page + 31` (page 0 without
the byte; ask for pages 1, 2, 3 when there are more than 32 slots). flags bit 0 = an upload is running,
bit 1 = the store is on the extra memory chip, bit 2 = the chip is still being prepared (status 0 until it's
done), bits 4–7 = the page (so a late duplicate of another page's reply can be told apart).

**`0xF10F slot part` reply:** `slot part` and then, for part 0, `frames(2) length(3) crc(4)`; for parts 1–3,
nine bytes of the name each. Status 0 = no animation starts there.

**`0xF112 slot offset(4)`** (firmware 101 and later) reads a stored file back. Reply: `offset_lo` and the
next 10 bytes of the file from `offset` (fewer at its end; the state word reads `DONE`). Status 0 = no
animation starts in `slot`, or `offset` is past its end. `offset_lo` (the offset's low byte) tells the answer
from a late duplicate of the previous reply. Replies are at most 16 bytes, so this is for small reads: the
web UI reads the colours and the first frame of animations it didn't send, for a preview (whole files up to
2 KB).

**File format** (little-endian):

```
 0  "OPA1"            4  w, h (32, 32)        6  frames (u16)        8  colours 1-256 (u16)
10  flags (0), 0     12  file length (u32)   16  CRC-32 of bytes 64 .. length-1
20  0 (8 bytes)      28  state: FF FF FF FF as sent; the panel writes "DONE" once it is stored
32  name, UTF-8, padded with zeros (32 bytes; the panel reports the first 27)
64  palette: colours x (r g b)
    frames, each: delay_ms (u16), n (u16), n bytes of drawing operations
```

Operations work on a 32 × 32 grid of palette indexes, from pixel 0 (top left) on, row by row:

| Byte | Meaning |
|---|---|
| `00cccccc` | Skip c+1 pixels (they keep the previous frame) |
| `01cccccc v` | c+1 pixels of colour v |
| `10cccccc v1 … vc+1` | c+1 pixels, one colour each |

Pixels after the last operation keep the previous frame. The grid is all colour 0 before the first frame
and again whenever the animation starts over. The shortest frame time the panel plays is 20 ms.

## Effects and clock faces

Effects (`0x8008`, `0xF101 0 n`): 0 plasma, 1 fire, 2 matrix, 3 starfield, 4 game of life,
5 fireworks, 6 ripples, 7 sparkles, 8 rainbow swirl, 9 tunnel, 10 snake, 11 pong, 12 bouncing balls,
13 equalizer, 14 aurora, 15 lava lamp, 16 rainstorm, 17 DNA helix, 18 warp speed, 19 radar,
20 rotozoom, 21 twister, 22 copper bars, 23 shadebobs, 24 kaleidoscope, 25 Julia fractal, 26 moiré,
27 sunburst, 28 spirograph, 29 3D cube, 30 synthwave, 31 terrain flyover, 32 plasma globe, 33 tartan,
34 ocean, 35 falling sand, 36 Langton's ants, 37 flock, 38 galaxy, 39 snowfall, 40 aquarium, 41 fireflies,
42 maze, 43 sorting, 44 pendulum wave, 45 brick breaker, 46 meadow (20 on: openipixelcolor 103 and later).

Clock faces (`0x0106`): 0 digital, 1 analog, 2 binary, 3 big digits, 4 plasma, 5 flip, 6 nixie,
7 rings, 8 sky (sun and moon follow the time), 9 pong, 10 words, 11 Berlin clock, 12 Fibonacci,
13 morphing digits, 14 hourglass, 15 falling bricks, 16 matrix digits, 17 fire digits, 18 LCD watch,
19 neon sign, 20 dice, 21 moon phase, 22 orbit, 23 progress, 24 colour code, 25 Roman numerals
(10 on: openipixelcolor 103 and later; older versions show face `style % 10`).

## Firmware update

Byte-compatible with the vendor firmware's update path, so the same tools install openipixelcolor over the
vendor firmware, update openipixelcolor, or put a vendor image back.

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

openipixelcolor is stricter than required: the chunk count must match the size, chunk indexes must
arrive in order, every chunk's CRC is checked before it is written, and before writing the update
record the staged image is read back from flash and checked again. A firmware whose flash writes
silently fail therefore never asks the bootloader to install a broken image.

# Hardware notes

What openipixelcolor knows about the panel, learned by measuring the hardware and by watching how it
behaves. Facts marked *(guess)* are not confirmed. Corrections are welcome.

## The board

| Part | What it is |
|---|---|
| Main chip | MVSilicon **P2064AF2**: Andes **NDS32 V3** core (AndeStar V3, data little-endian, instruction words big-endian), 2 MiB internal flash, on-chip RAM |
| Bluetooth | A separate BLE module. It talks to the main chip over SPI (module → chip) and UART (chip → module) and does all the Bluetooth work |
| Display | 32 × 32 RGB LED matrix, driven directly by the chip's LED controller |
| Button | One key on P2.6, active low, external pull-up |
| Extra flash | An 8-pin SPI NOR next to the chip, 4 MiB (seen: Winbond W25Q32, JEDEC id `EF 40 16`). The vendor firmware keeps its pictures there; openipixelcolor 104 and later keep the animations there (below) |

Panels with this board advertise as `LED_BLE_…` and offer the Bluetooth service `0xFA00`.

## Internal flash (2 MiB, memory-mapped: CPU address = flash offset)

| Range | Contents |
|---|---|
| `0x000000–0x00FFFF` | Bootloader. Never written by openipixelcolor |
| `0x010000–…` | Application (openipixelcolor or the vendor firmware). Runs in place |
| `0x108000–0x1FEFFF` | Update staging area: a new image is received here, the bootloader copies it to `0x10000` |
| `0x150000–0x1EFFFF` | openipixelcolor: stored animations (20 slots of 32 KiB) when the panel has no extra flash, and before 104 |
| `0x1FB000` | openipixelcolor: saved text playlist (one 4 KiB sector) |
| `0x1FC000` | openipixelcolor: saved picture |
| `0x1FD000` | openipixelcolor: settings log (16 × 256-byte records) |
| `0x1FF000` | Update record: tells the bootloader that a complete, checked image waits in the staging area |

The openipixelcolor data sits at the top of the staging area. An update reaches the animations only with an
image larger than 0x48000 bytes, and the settings only beyond about 0xF3000 bytes (openipixelcolor is about
45 KiB, the vendor firmware about 255 KiB). If one ever does, the damaged records fail their checks:
the panel leaves those animations out, or starts with factory settings.

The flash driver (`firmware/src/drv/iflash.c`) refuses any write below `0x108000`, so the bootloader
and the running application cannot be overwritten by mistake.

### Writing flash while running from it

The code runs from the same flash it writes. The chip can lock a 4 KiB window (`0x10000–0x10FFF`)
into cache; every flash routine is linked into that window (`.text.flashdrv`) and runs with
interrupts off. `tools/check_image.py` checks after every build that these routines stay inside the
window and call nothing outside it.

Interrupts stay off for each minimum wait (an erased sector takes about 0.6 s), so the 1 ms tick misses
that time; `app_port.c` adds it back.

### The flash cache keeps old bytes

Flash reads go through a cache, and erasing or programming does not update it. Data that was read before
a write can come back unchanged afterwards. This was seen when an animation that had been playing was
overwritten: the read-back check failed. Reading 256 KiB of other flash first pushes the old lines out
(`app_flash_forget()`). Settings avoid the problem by never reading back in the same boot.

## Extra flash (4 MiB, SPI)

On the chip's SPI master at `0x40003000`: SCK/MOSI/MISO on P2.0–P2.2 (pin-mux `0x400100F0` bit 3), chip
select P2.3. A micro-SD socket shares the bus with its select on P2.7 (kept high). Registers, as the vendor
firmware uses them: `+0x00` received byte, `+0x04` byte to send, `+0x08` write 1 to start, `+0x0C` control
(16 bit: [3:0] clock divider, 0 used; [5:4] SPI mode, 0; [10] master; [12] receive; [13] DMA; [15] enable),
`+0x10` write 1 / 4 to clear the received / done flags, `+0x14` status ([3] done, [4] busy). No clock enable
is needed. `firmware/src/drv/xflash.c` moves one byte at a time (the vendor firmware uses DMA channels 6 and
7): reads run at about 680 KB/s; a 4 KiB sector erases in about 26 ms and programs in about 17 ms. The CPU
keeps running meanwhile (it runs from the internal flash), so the picture doesn't stop.

| Range | Contents (openipixelcolor 104 and later) |
|---|---|
| `0x000000` | Left erased. The vendor firmware keeps its list of pictures here: erased reads as "no pictures", so going back to the vendor firmware is safe |
| `0x001000` | Marker: `"OPXS"` and the layout. Without it, the first start erases the whole chip, then moves the animations from the internal flash over |
| `0x008000–0x3FFFFF` | Stored animations: 127 slots of 32 KiB |

The vendor firmware's layout, for reference: its picture list in sector 0, the pictures in 12 KiB blocks
from `0x1000` (up to 341).

## Application image

The image is linked at `0x10000`. The first bytes are the reset vector and interrupt vectors; at
`+0xB0` sits a small header in the bootloader's format (layout fields, and the magic `"FHD\x03"` at
`+0xD4`). The bootloader checks it, so `start.S` must keep it.

## RAM

RAM starts at `0x20000000`. At least `0x20000000–0x2001921C` works; the real size is not known, so
the linker script stays inside that range. The stack (8 KiB) is at the bottom of RAM, so an overflow
faults instead of corrupting data.

## Clocks

A PLL runs at 480 MHz; the internal flash is clocked at 80 MHz (`sys_flash_clkdiv()`). A 1 ms tick
comes from timer 1 (IRQ 8). The flash driver's minimum waits are calibrated against that tick at boot.

## Bluetooth module link

| Direction | Peripheral | Pins | Framing |
|---|---|---|---|
| module → chip | SPI slave `0x40004000`, DMA channel 0 ring, IRQ 30 | P1.26 (CS), P1.27–P1.29 | `u16 length, u16 command, payload` |
| chip → module | UART2 `0x40007000`, 3 Mbaud 8N1 | P0.2 / P0.3 | `u16 length, u16 command, u8 status, data` |

The module forwards at most 16 bytes of a reply as one notification, and a 16-byte reply sometimes
arrives twice. Only one Bluetooth connection at a time is possible. The protocol is in
[PROTOCOL.md](PROTOCOL.md).

## Display

| Block | Address | Use |
|---|---|---|
| LED controller | `0x40012000` | Shifts out one row of pixel data; IRQ 31 after each row |
| LED data DMA | `0x4000A000` | Streams one row (0x180 bytes) into the controller |
| General DMA ch1 | `0x40008000` + 0x20·n | Writes the on-time of each slot to the controller (paced by timer 3) |
| General DMA ch2 | | Writes the row address of each slot to P1.8–P1.12 (paced by timer 6) |

- Pixel data leaves on P1.0–P1.7; within a pixel byte, R is bit 5, G bit 4, B bit 3 (P1.5 / P1.4 / P1.3).
- Each row is shown in 12 time slots with binary-weighted on-times (bit-angle modulation), which
  gives the colour depth.
- Brightness has 16 levels; the percentage from the apps maps to `level = min(percent / 6, 15)`.
- The panel has 4 orientations (90° steps). On the tested panel orientation 2 shows the picture
  upright, so that is the default.
- During an update the display keeps running and shows the update screen; the picture is blanked while
  each block is written to flash (the vendor firmware stops the display instead). In rescue mode the
  display is not started at all.

## Other pins

| Pin | Use |
|---|---|
| P2.6 | Key (active low) |
| P2.0–P2.3 | Extra flash: SPI SCK, MOSI, MISO, chip select |
| P2.7 / P2.5 | Micro-SD socket: chip select (kept high), card detect *(no card seen in any panel)* |
| P0.30 | "Busy" output *(guess: tells the module the chip is working)* |
| P2.11 | Output, driven high after start-up *(guess: power enable)* |
| P1.30 / P1.31 | Only pull-ups configured; use unknown |
| UART1 | Debug console, 1 Mbaud 8N1 (boot banner and a once-per-second status line) |

## Peripheral addresses used

See `firmware/src/hal/regs.h`. Main blocks: system/power `0x40020000`, clock control
`0x40021000`, reset `0x40022000`, PLL `0x40026000`, timers `0x4002A000` (8 × 0x800), UART0/1
`0x40005000` / `0x40006000`, UART2 `0x40007000`, GPIO and pin mux `0x40010000`, internal flash
controller `0x40011000`, SPI master (extra flash) `0x40003000`.

No watchdog is fed; it is switched off at boot.

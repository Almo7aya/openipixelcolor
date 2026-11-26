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
| Extra flash | An 8-pin 25Q32 SPI NOR (4 MiB) next to the chip. The vendor firmware keeps content there; openipixelcolor does not use it |

Panels with this board advertise as `LED_BLE_…` and offer the Bluetooth service `0xFA00`.

## Internal flash (2 MiB, memory-mapped: CPU address = flash offset)

| Range | Contents |
|---|---|
| `0x000000–0x00FFFF` | Bootloader. Never written by openipixelcolor |
| `0x010000–…` | Application (openipixelcolor or the vendor firmware). Runs in place |
| `0x108000–0x1FEFFF` | Update staging area: a new image is received here, the bootloader copies it to `0x10000` |
| `0x150000–0x1EFFFF` | openipixelcolor: stored animations (20 slots of 32 KiB) |
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
- Before writing flash during an update, the display pipeline is stopped (the panel goes dark).
  A restart brings it back.

## Other pins

| Pin | Use |
|---|---|
| P2.6 | Key (active low) |
| P0.30 | "Busy" output *(guess: tells the module the chip is working)* |
| P2.11 | Output, driven high after start-up *(guess: power enable)* |
| P1.30 / P1.31 | Only pull-ups configured; use unknown |
| UART1 | Debug console, 1 Mbaud 8N1 (boot banner and a once-per-second status line) |

## Peripheral addresses used

See `firmware/src/hal/regs.h`. Main blocks: system/power `0x40020000`, clock control
`0x40021000`, reset `0x40022000`, PLL `0x40026000`, timers `0x4002A000` (8 × 0x800), UART0/1
`0x40005000` / `0x40006000`, UART2 `0x40007000`, GPIO and pin mux `0x40010000`, internal flash
controller `0x40011000`.

No watchdog is fed; it is switched off at boot.

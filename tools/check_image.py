"""Sanity-check a raw openipixelcolor image (linked at 0x10000) before flashing.

Checks: 41 NDS32 `j` vectors at +0x00, all targets inside the image and 2-byte aligned,
the bootloader's "FHD" magic at +0xD4, the OTA size limit, and (with --listing) that the
flash driver stays inside its cache-locked window.

usage: python check_image.py build/fw.bin [--listing build/fw.lst]
"""
import argparse
import struct
import sys
from pathlib import Path

MAGIC = (0xD4, b"FHD\x03")      # see firmware/src/start.S
WINDOW = (0x10000, 0x11000)      # cache-locked window (sys_memctl_config)
FLASHDRV_FUNCS = ('iflash_init', 'iflash_write', 'spin_while_set', 'fmc_cmd', 'fmc_exit_crm',
                  'flash_status', 'flash_wait_idle', 'erase_4k', 'program_page', 'iflash_delay_loops')


def check_flashdrv(listing, fail):
    """Every flash-driver function must lie in the window, and every direct branch/call it makes
    must land in the window too (it runs while the flash itself is busy)."""
    import re
    func = None
    seen = set()
    for line in listing.read_text().splitlines():
        m = re.match(r'^([0-9a-f]{8}) <([^>]+)>:', line)
        if m:
            func = m.group(2)
            if func in FLASHDRV_FUNCS:
                seen.add(func)
                addr = int(m.group(1), 16)
                if not WINDOW[0] <= addr < WINDOW[1]:
                    fail(f'{func} at 0x{addr:X} is outside the cache window')
            continue
        if func not in FLASHDRV_FUNCS:
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S+)\s+(.*)$', line)
        if not m:
            continue
        ins_addr, mnem, ops = int(m.group(1), 16), m.group(2), m.group(3)
        if not WINDOW[0] <= ins_addr < WINDOW[1]:
            fail(f'{func}: instruction at 0x{ins_addr:X} outside the window')
        t = re.search(r'\b([0-9a-f]{5,8}) <', ops)
        if t and (mnem.startswith('j') or mnem.startswith('b')):
            tgt = int(t.group(1), 16)
            if not WINDOW[0] <= tgt < WINDOW[1]:
                fail(f'{func}: {mnem} to 0x{tgt:X} leaves the window')
        if mnem in ('jral', 'jral5', 'jr5') and 'lp' not in ops:
            fail(f'{func}: indirect call {mnem} {ops} (cannot be verified)')
    missing = set(FLASHDRV_FUNCS) - seen - {'flash_wait_idle', 'spin_while_set'}   # may be inlined
    for f in sorted(missing):
        if f in ('iflash_init', 'iflash_write'):
            fail(f'{f} not found in listing')
    print(f'flash driver: {len(seen)} functions checked inside 0x{WINDOW[0]:X}-0x{WINDOW[1]-1:X}')


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('image', type=Path)
    ap.add_argument('--listing', type=Path, help='objdump -d output: check the flash driver stays in its window')
    a = ap.parse_args()
    img = a.image.read_bytes()
    ok = True

    def fail(msg):
        nonlocal ok
        ok = False
        print('FAIL', msg)

    if len(img) < 0x100:
        fail(f'image too small ({len(img)} bytes)')
        return 1
    for i in range(41):
        w = struct.unpack('>I', img[i * 4:i * 4 + 4])[0]
        if w >> 24 != 0x48:
            fail(f'vector {i} @+0x{i*4:02X} is 0x{w:08X}, not a 32-bit `j`')
            continue
        imm = w & 0xFFFFFF
        if imm & 0x800000:
            imm -= 1 << 24
        tgt = i * 4 + imm * 2
        if not (0x100 <= tgt < len(img)):
            fail(f'vector {i} target +0x{tgt:X} outside code')
    if img[MAGIC[0]:MAGIC[0] + len(MAGIC[1])] != MAGIC[1]:
        fail('image header magic "FHD" missing at +0xD4')
    if len(img) > 0xF7800:
        fail(f'image {len(img)} bytes exceeds the OTA limit 0xF7800')
    if a.listing:
        check_flashdrv(a.listing, fail)
    print(('OK' if ok else 'BAD') + f': {a.image} {len(img)} bytes')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())

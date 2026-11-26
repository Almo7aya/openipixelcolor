"""SAFE check of the update path (never installs anything). Only runs against openipixelcolor.

Announces an update whose overall CRC is deliberately wrong, sends ONE chunk of N bytes with a correct
chunk CRC. The receiver erases + programs the staging area (0x108000), then fails the final CRC check,
so no update record is written and the bootloader never installs anything. The panel restarts about
10 s later. Run it before and after every install (--bytes 12288 = one full chunk) to prove the
update path still works.

usage: python ota_flash_probe.py <addr> [--bytes 16] [--timeout 300]
"""
import argparse
import asyncio
import os
import struct
import time
import zlib
from bleak import BleakClient

WRITE = "0000fa02-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000fa03-0000-1000-8000-00805f9b34fb"
FRAG = 244


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("--bytes", type=int, default=16)
    ap.add_argument("--timeout", type=float, default=300)
    ap.add_argument("--bad-chunk-crc", action="store_true",
                    help="corrupt the chunk CRC: the receiver must reject it BEFORE any flash access")
    a = ap.parse_args()

    data = bytes((i * 37 + 11) & 0xFF for i in range(a.bytes))
    good = zlib.crc32(data) & 0xFFFFFFFF
    announce = bytes([0x0D, 0x00, 0x00, 0xC0, 1]) + struct.pack("<II", good ^ 0xFFFFFFFF, len(data))  # wrong total crc
    ccrc = good ^ 0x1 if a.bad_chunk_crc else good
    frame = struct.pack("<H", len(data) + 13) + bytes([0x01, 0xC0, 0]) + struct.pack("<II", ccrc, len(data)) + data

    q = asyncio.Queue()
    async with BleakClient(a.addr, timeout=20.0) as c:
        await c.start_notify(NOTIFY, lambda _h, d: q.put_nowait((time.monotonic(), bytes(d))))

        async def send(f):
            for off in range(0, len(f), FRAG):
                await c.write_gatt_char(WRITE, f[off:off + FRAG], response=True)

        await send(bytes([4, 0, 5, 0x80]))
        _, v = await asyncio.wait_for(q.get(), 5)
        print(f"version reply {v.hex(' ')}")
        if v[4] != 99:
            raise SystemExit("not running openipixelcolor - refusing")

        await send(announce)
        _, r = await asyncio.wait_for(q.get(), 10)
        print(f"announce {announce.hex(' ')} -> {r.hex(' ')}")
        t0 = time.monotonic()
        await send(frame)
        t_sent = time.monotonic()
        print(f"chunk of {len(data)} B sent in {t_sent - t0:.2f}s, waiting up to {a.timeout:.0f}s for the reply ...")
        try:
            t1, r = await asyncio.wait_for(q.get(), a.timeout)
            print(f"reply {r.hex(' ')} after {t1 - t_sent:.2f}s  (expected status 00: final CRC check fails on purpose)")
        except asyncio.TimeoutError:
            print(f"NO reply within {a.timeout:.0f}s")

asyncio.run(main())

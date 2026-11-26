"""Install a firmware image over Bluetooth, with guards (update commands 0xC000 / 0xC001).

Guards: encoded image only; the panel must report the version you pass with --expect; every reply must be
exactly 05 00 00 C0 01 (announce) / 05 00 01 C0 01 (chunk), otherwise it stops. Without --go it only
connects and checks the version (dry run).

usage: python ota_flash_guarded.py <addr> <encoded.bin> --expect <version shown by the panel> [--go]
       e.g. --expect 99.17 when updating openipixelcolor, or the vendor version (3.x) for the first install
"""
import argparse
import asyncio
import struct
import zlib
from bleak import BleakClient

WRITE = "0000fa02-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000fa03-0000-1000-8000-00805f9b34fb"
CHUNK = 0x3000
FRAG = 244
ACK_ANNOUNCE = bytes.fromhex("050000c001")
ACK_CHUNK = bytes.fromhex("050001c001")


def check_image(data: bytes) -> None:
    if not data or len(data) > 0xF7800:
        raise SystemExit(f"bad size {len(data)}")
    if data[0] == 0x48:
        raise SystemExit("image looks RAW (first byte 0x48) - the device expects the encoded form")
    if (((data[0] - 0x8A) & 0xFF) ^ 0x4A) != 0x48:
        raise SystemExit("decoded first byte is not an NDS32 `j` (0x48) - not a valid encoded image")


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("image")
    ap.add_argument("--expect", required=True, help="version the panel must report, e.g. 99.17")
    ap.add_argument("--go", action="store_true")
    a = ap.parse_args()

    data = open(a.image, "rb").read()
    check_image(data)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    n = (len(data) + CHUNK - 1) // CHUNK
    announce = bytes([0x0D, 0x00, 0x00, 0xC0, n]) + struct.pack("<II", crc, len(data))
    frames = []
    for i in range(n):
        ch = data[i * CHUNK:(i + 1) * CHUNK]
        frames.append(struct.pack("<H", len(ch) + 13) + bytes([0x01, 0xC0, i])
                      + struct.pack("<II", zlib.crc32(ch) & 0xFFFFFFFF, len(ch)) + ch)
    print(f"image {len(data)} B, crc32 0x{crc:08X}, {n} chunks; announce {announce.hex(' ')}")

    q: asyncio.Queue = asyncio.Queue()

    async def reply(timeout=20.0) -> bytes:
        return await asyncio.wait_for(q.get(), timeout)

    async with BleakClient(a.addr, timeout=20.0) as c:
        await c.start_notify(NOTIFY, lambda _h, d: q.put_nowait(bytes(d)))

        async def send(frame: bytes) -> None:
            while not q.empty():
                print(f"  (stale notify {q.get_nowait().hex(' ')})")
            for off in range(0, len(frame), FRAG):
                await c.write_gatt_char(WRITE, frame[off:off + FRAG], response=True)

        await send(bytes([4, 0, 5, 0x80]))
        v = await reply(5)
        ver = f"{v[4]}.{v[5]:02d}" if len(v) >= 8 and v[2:4] == b"\x05\x80" else "?"
        print(f"device version reply {v.hex(' ')} -> MCU {ver}")
        if ver != a.expect:
            raise SystemExit(f"device reports MCU {ver}, expected {a.expect} - not flashing")
        if not a.go:
            print("dry run OK (add --go to flash)")
            return

        await send(announce)
        r = await reply()
        print(f"announce ack {r.hex(' ')}")
        if r != ACK_ANNOUNCE:
            raise SystemExit("unexpected announce reply - aborting before any data is written")

        for i, fr in enumerate(frames):
            await send(fr)
            r = await reply()
            if r != ACK_CHUNK:
                raise SystemExit(f"chunk {i}: unexpected reply {r.hex(' ')} - aborting "
                                 "(device leaves update mode; nothing is applied)")
            print(f"  chunk {i + 1}/{n} ok")
        print("all chunks acknowledged - device now saves the update record and reboots")
        try:
            await c.stop_notify(NOTIFY)
        except Exception:
            pass


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except Exception as e:  # disconnect on reboot after the last chunk is expected
        print(f"(connection ended: {type(e).__name__}: {e})")

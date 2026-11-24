"""Read-only probe: connect, list the fa00 service, send device-info (0x8001) and version (0x8005) queries."""
import asyncio
import sys
from datetime import datetime
from bleak import BleakClient

ADDR = sys.argv[1]
WRITE = "0000fa02-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000fa03-0000-1000-8000-00805f9b34fb"


async def main():
    q = asyncio.Queue()
    async with BleakClient(ADDR, timeout=20.0) as c:
        print(f"connected, mtu={c.mtu_size}")
        for s in c.services:
            if "fa00" in s.uuid:
                for ch in s.characteristics:
                    print(f"  char {ch.uuid[4:8]} {','.join(ch.properties)}")
        await c.start_notify(NOTIFY, lambda _h, d: q.put_nowait(bytes(d)))
        now = datetime.now()
        for name, pkt in (("device info 0x8001", bytes([8, 0, 1, 0x80, now.hour, now.minute, now.second, 0])),
                          ("version 0x8005", bytes([4, 0, 5, 0x80])),
                          ("diagnostics 0xF0F1", bytes([4, 0, 0xF1, 0xF0]))):
            await c.write_gatt_char(WRITE, pkt, response=True)
            try:
                r = await asyncio.wait_for(q.get(), 5)
                print(f"{name}: -> {pkt.hex(' ')}  <- {r.hex(' ')}")
                if pkt[2:4] == b"\xf1\xf0" and len(r) >= 16:
                    mode = {0: "normal", 1: "rescue (key)", 2: "rescue (crash loop)"}.get(r[5], r[5])
                    print(f"    mode={mode} boots_since_stable={r[6]} last_exc={r[7]}@0x{int.from_bytes(r[8:12], 'little'):08x}"
                          f" uptime={int.from_bytes(r[12:16], 'little')}s")
            except asyncio.TimeoutError:
                print(f"{name}: -> {pkt.hex(' ')}  <- (no reply)")
        await c.stop_notify(NOTIFY)

asyncio.run(main())

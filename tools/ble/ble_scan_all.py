"""List every BLE advertiser seen in 25 s: address, RSSI, name, service UUIDs (the panel offers service fa00)."""
import asyncio
from bleak import BleakScanner


async def main():
    found = await BleakScanner.discover(timeout=25.0, return_adv=True)
    rows = sorted(found.values(), key=lambda t: -(t[1].rssi or -999))
    for dev, adv in rows:
        name = adv.local_name or dev.name or ''
        uu = ','.join(u[4:8] for u in adv.service_uuids)
        print(f'{dev.address}  {adv.rssi:4d} dBm  {name!r:28}  svc={uu}  mfr={ {k: v.hex() for k, v in adv.manufacturer_data.items()} }')
    print(f'{len(rows)} devices')

asyncio.run(main())


"""Control a panel running openipixelcolor over Bluetooth.

  python panelctl.py list                          # every effect and clock face, by number and name
  python panelctl.py <MAC> status
  python panelctl.py <MAC> effect <n|name>         # e.g. 14 or aurora
  python panelctl.py <MAC> autoplay on|off [seconds]
  python panelctl.py <MAC> clock [n|name] [--12h] [--nodate]   # e.g. 8 or sky; also syncs time + date from this PC
  python panelctl.py <MAC> text "Hello" [--color FF8800] [--speed 1-10] [--rainbow]
  python panelctl.py <MAC> image picture.png       # any image, resized to 32x32
  python panelctl.py <MAC> brightness 0-100
  python panelctl.py <MAC> power on|off
  python panelctl.py <MAC> timer start|pause|stop
  python panelctl.py <MAC> score <p1> <p2>
  python panelctl.py <MAC> test <0-6>              # test patterns
  python panelctl.py <MAC> key short|long          # press the button remotely
  python panelctl.py <MAC> orientation 0-3
  python panelctl.py <MAC> saved                   # are settings / picture / text saved on the panel?
  python panelctl.py <MAC> defaults                # back to factory settings (saved a few s later)
  python panelctl.py <MAC> restart                 # restart the panel's processor (like a power cycle)

Settings, the last picture and the text survive power cycles; they are saved ~3 s (picture ~8 s)
after the last change. Text with effects, other scripts and playlists: use the web UI.
"""
import argparse
import asyncio
import datetime
import struct
import sys
from bleak import BleakClient

WRITE = "0000fa02-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000fa03-0000-1000-8000-00805f9b34fb"
FRAG = 244
# the firmware's own order (firmware/src/app/effects.c, clockface.h): new ones go at the end
EFFECTS = ["plasma", "fire", "matrix", "starfield", "life", "fireworks", "ripples", "sparkles",
           "swirl", "tunnel", "snake", "pong", "balls", "equalizer",
           "aurora", "lava", "rain", "dna", "warp", "radar",
           "rotozoom", "twister", "copper", "shadebobs", "kaleido", "julia", "moire", "sunburst",
           "spiro", "cube", "synthwave", "terrain", "globe", "tartan",
           "ocean", "sand", "ants", "flock", "galaxy", "snow", "aquarium", "fireflies",
           "maze", "sorting", "pendulums", "bricks", "meadow",
           "finals", "finals-live", "finals-cash", "finals-spin", "finals-chrome", "finals-crt", "finals-neon",
           "finals-shatter",
           "neon-mountains", "neon-drive", "palm-beach", "neon-city", "laser-tunnel", "vector-mtns", "vaporwave",
           "vhs", "laser-show", "cassette"]
CLOCKS = ["digital", "analog", "binary", "big", "plasma", "flip", "nixie", "rings", "sky", "pong",
          "words", "berlin", "fibonacci", "morph", "hourglass", "bricks", "matrix", "fire", "lcd", "neon",
          "dice", "moon", "orbit", "progress", "colour", "roman",
          "blocks", "squares", "abacus", "scope", "clocks", "station", "cuckoo", "vfd", "flipdot", "terminal",
          "odometer", "candle", "decimal", "radar", "ticker", "melting", "3d"]
MODES = ["effect", "clock", "text", "canvas", "rhythm", "score", "timer", "test", "splash", "anim", "welcome"]


def pkt(cmd: int, payload: bytes = b"") -> bytes:
    return struct.pack("<HH", 4 + len(payload), cmd) + payload


def build(a) -> list:
    now = datetime.datetime.now()
    c = a.cmd
    if c == "status":
        return [pkt(0xF106)]
    if c == "effect":
        n = EFFECTS.index(a.args[0]) if a.args[0] in EFFECTS else int(a.args[0])
        return [pkt(0xF101, bytes([0, n]))]
    if c == "autoplay":
        secs = int(a.args[1]) if len(a.args) > 1 else 20
        return [pkt(0xF104, bytes([1 if a.args[0] == "on" else 0]) + struct.pack("<H", secs))]
    if c == "clock":
        style = (CLOCKS.index(a.args[0]) if a.args[0] in CLOCKS else int(a.args[0])) if a.args else 0
        return [pkt(0x8001, bytes([now.hour, now.minute, now.second, 0])),
                pkt(0x0106, bytes([style, 0 if a.h12 else 1, 0 if a.nodate else 1,
                                   now.year % 100, now.month, now.day, now.isoweekday()]))]
    if c == "text":
        col = bytes.fromhex(a.color)
        if any(ord(ch) > 126 for ch in a.args[0]):
            raise SystemExit("panelctl sends English text only; use the web UI for Arabic and other scripts "
                             "(it joins the letters and sends them as dots, cmd 0xF10A)")
        body = a.args[0].encode("ascii", "replace")[:120]
        return [pkt(0xF102, col + bytes([a.speed, 1 if a.rainbow else 0]) + body)]
    if c == "image":
        from PIL import Image
        img = Image.open(a.args[0]).convert("RGB")
        w, h = img.size
        s = min(w, h)
        img = img.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s)).resize((32, 32), Image.LANCZOS)
        return [pkt(0xF103, img.tobytes())]
    if c == "brightness":
        return [pkt(0x8004, bytes([max(0, min(100, int(a.args[0])))]))]
    if c == "power":
        return [pkt(0x0107, bytes([1 if a.args[0] == "on" else 0]))]
    if c == "timer":
        return [pkt(0x8009, bytes([{"stop": 0, "start": 1, "pause": 2}[a.args[0]]]))]
    if c == "score":
        return [pkt(0x800A, bytes([int(a.args[0]), 0, int(a.args[1])]))]
    if c == "test":
        return [pkt(0xF101, bytes([7, int(a.args[0])]))]
    if c == "key":
        return [pkt(0xF107, bytes([1 if a.args[0] == "short" else 2]))]
    if c == "orientation":
        return [pkt(0x8006, bytes([int(a.args[0]) & 3]))]
    if c == "saved":
        return [pkt(0xF109)]
    if c == "defaults":
        return [pkt(0xF108, bytes([0xA5]))]
    if c == "restart":
        return [pkt(0xC006)]
    raise SystemExit(f"unknown command {c}")


async def main():
    if sys.argv[1:2] == ["list"]:
        for title, names in (("effects", EFFECTS), ("clock faces", CLOCKS)):
            print(f"{title}:")
            for i in range(0, len(names), 6):
                print("  " + "  ".join(f"{i + k:2d} {n:<15}" for k, n in enumerate(names[i:i + 6])).rstrip())
        return
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("addr")
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--color", default="FFC850")
    ap.add_argument("--speed", type=int, default=4)
    ap.add_argument("--rainbow", action="store_true")
    ap.add_argument("--12h", dest="h12", action="store_true")
    ap.add_argument("--nodate", action="store_true")
    a = ap.parse_args()
    packets = build(a)

    q = asyncio.Queue()
    async with BleakClient(a.addr, timeout=20.0) as cl:
        await cl.start_notify(NOTIFY, lambda _h, d: q.put_nowait(bytes(d)))
        for p in packets:
            for off in range(0, len(p), FRAG):
                await cl.write_gatt_char(WRITE, p[off:off + FRAG], response=True)
            try:
                # a reply repeats its command in bytes 2-3; the BLE module sends 16-byte replies twice,
                # so skip anything that does not answer this packet
                async with asyncio.timeout(5):
                    while (r := await q.get())[2:4] != p[2:4]:
                        pass
            except TimeoutError:
                print(f"{p[2:4][::-1].hex()}: no reply")
                continue
            if r[2:4] == b"\x06\xf1" and len(r) >= 13:
                mode = MODES[r[5]] if r[5] < len(MODES) else r[5]
                print(f"mode={mode} effect={EFFECTS[r[6]] if r[6] < len(EFFECTS) else r[6]} brightness={r[7]}% "
                      f"power={'on' if r[8] else 'off'} time={r[9]:02d}:{r[10]:02d}:{r[11]:02d} "
                      f"synced={'yes' if r[12] & 1 else 'no'} autoplay={'on' if r[12] & 2 else 'off'}"
                      + (f" every={r[15]}s orientation={r[13]} clock_style={CLOCKS[r[14]] if r[14] < len(CLOCKS) else r[14]} "
                         f"24h={'yes' if r[12] & 4 else 'no'} date={'yes' if r[12] & 8 else 'no'}" if len(r) >= 16 else ""))
            elif r[2:4] == b"\x09\xf1" and len(r) >= 13:
                seq = int.from_bytes(r[5:9], "little")
                f = r[11]
                print(f"saved records={seq} next_slot={r[9]} picture_saved={'yes' if r[10] & 1 else 'no'} "
                      f"text_strip_saved={'yes' if r[10] & 2 else 'no'} "
                      f"loaded={'yes' if f & 1 else 'no'} settings_pending={'yes' if f & 2 else 'no'} "
                      f"picture_pending={'yes' if f & 4 else 'no'} writing_stopped={'yes' if f & 8 else 'no'} "
                      f"last_rc={int.from_bytes(r[12:13], 'little', signed=True)}")
            else:
                print(f"{p[2:4][::-1].hex()}: {'ok' if len(r) >= 5 and r[4] == 1 else 'reply ' + r.hex(' ')}")

asyncio.run(main())

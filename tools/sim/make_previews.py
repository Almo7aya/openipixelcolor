"""Animated previews for the web UI's effect and clock tiles, drawn by the firmware's own code:

    ./sim previews                                        # writes previews.bin
    python tools/sim/make_previews.py previews.bin webui/previews

Each preview is a small 32 x 32 GIF (the page scales it up with crisp pixels) plus a still PNG of its first
frame for people who turn animations off. Needs Pillow.
"""
import os
import sys

from PIL import Image

FRAME = 32 + 32 * 32 * 3


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    data = open(src, "rb").read()
    groups = {}
    for i in range(len(data) // FRAME):
        rec = data[i * FRAME:(i + 1) * FRAME]
        groups.setdefault(rec[:32].split(b"\0")[0].decode(), []).append(rec[32:])
    for label, frames in groups.items():
        kind, num = label.split()
        name = f"{kind}-{num}"
        imgs = [Image.frombytes("RGB", (32, 32), px) for px in frames]
        delay = 75 if kind == "effect" else 100
        pal = [im.convert("P", palette=Image.ADAPTIVE, colors=255) for im in imgs]
        pal[0].save(os.path.join(out, name + ".gif"), save_all=True, append_images=pal[1:], duration=delay,
                    loop=0, optimize=True, disposal=1)
        imgs[0].save(os.path.join(out, name + ".png"), optimize=True)
    total = sum(os.path.getsize(os.path.join(out, f)) for f in os.listdir(out))
    print(f"{len(groups)} previews, {total // 1024} KB in {out}")


if __name__ == "__main__":
    main()

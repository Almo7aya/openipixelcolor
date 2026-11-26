"""Animated previews for the web UI's effect and clock tiles, drawn by the firmware's own code:

    ./sim previews                                        # writes previews.bin
    python tools/sim/make_previews.py previews.bin webui/previews

Each preview is a small 32 x 32 GIF (the page scales it up with crisp pixels) plus a still PNG of its first
frame for people who turn animations off. Needs Pillow.

Clock faces come in four versions (24-hour on/off, date on/off: "clock 03", "clock 03 12h", "clock 03 nodate",
"clock 03 12h nodate"). A version that looks the same as one already written (the face ignores that setting)
isn't written again: clocks.json tells the page which file each face uses for each combination, in the order
[24-hour + date, 12-hour + date, 24-hour, 12-hour].
"""
import json
import os
import sys

from PIL import Image

FRAME = 32 + 32 * 32 * 3
DELAY = {"effect": 75, "clock": 100, "welcome": 64}     # ms a frame, as the simulator took them
VARIANTS = ["", "-12h", "-nodate", "-12h-nodate"]


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    data = open(src, "rb").read()
    groups = {}
    for i in range(len(data) // FRAME):
        rec = data[i * FRAME:(i + 1) * FRAME]
        groups.setdefault(rec[:32].split(b"\0")[0].decode(), []).append(rec[32:])

    for f in os.listdir(out):                         # versions no longer made (a face changed) go
        if f.startswith("clock-"):
            os.remove(os.path.join(out, f))

    written = {}                                      # frames -> file name, to skip repeats
    clocks = {}
    for label, frames in groups.items():
        kind = label.split()[0]
        name = label.replace(" ", "-")
        key = b"".join(frames)
        if kind == "clock":
            face = "-".join(name.split("-")[:2])
            clocks.setdefault(face, {})[name[len(face):]] = written.get(key, name)
            if key in written:
                continue
        written[key] = name
        imgs = [Image.frombytes("RGB", (32, 32), px) for px in frames]
        pal = [im.convert("P", palette=Image.ADAPTIVE, colors=255) for im in imgs]
        pal[0].save(os.path.join(out, name + ".gif"), save_all=True, append_images=pal[1:], duration=DELAY[kind],
                    loop=0, optimize=True, disposal=1)
        imgs[0].save(os.path.join(out, name + ".png"), optimize=True)

    index = [[clocks[face][v] for v in VARIANTS] for face in sorted(clocks)]
    with open(os.path.join(out, "clocks.json"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("[\n" + ",\n".join("  " + json.dumps(row) for row in index) + "\n]\n")
    total = sum(os.path.getsize(os.path.join(out, f)) for f in os.listdir(out))
    clock_files = len({f for row in index for f in row})
    print(f"{len(written)} previews ({clock_files} files for {len(index) * 4} clock versions), {total // 1024} KB in {out}")


if __name__ == "__main__":
    main()

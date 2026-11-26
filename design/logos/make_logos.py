"""logos.bin (from logos.c) -> one folder per set with boot.gif, first-start.gif, firmware-update.gif,
bluetooth.gif and sheet.png, plus compare.png with all sets. Needs Pillow.

    python make_logos.py logos.bin <out-dir>
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "sim"))
from make_readme_images import font, led_panel  # noqa: E402

FRAME = 32 + 32 * 32 * 3
MS = 40
SETS = [("R", "ring", "The ring")]
SCREENS = [("boot", "boot.gif"), ("welcome", "first-start.gif"), ("update", "firmware-update.gif"), ("bluetooth", "bluetooth.gif"), ("saving", "saving.gif")]
BG = (13, 15, 18, 255)
INK, MUTED = (232, 230, 225, 255), (142, 150, 163, 255)
# still frames for the sheet: (screen, ms into it, caption)
PICKS = {
    "R": [("boot", 440, "Boot: the ring draws itself"), ("boot", 1300, "Boot: the logo"), ("boot", 2500, "Boot: the name"),
          ("boot", 3500, "Boot: the version"), ("welcome", 1700, "First start"), ("welcome", 4400, "First start: connect"),
          ("update", 3100, "Firmware update"), ("update", 5000 + 1500, "Update done"), ("bluetooth", 620, "Connected"), ("saving", 1500, "Saving"), ("saving", 3200 + 1000, "Saved")],
}


def frames(path):
    data = open(path, "rb").read()
    out = {}
    for i in range(len(data) // FRAME):
        rec = data[i * FRAME:(i + 1) * FRAME]
        out.setdefault(rec[:32].split(b"\0")[0].decode(), []).append(rec[32:])
    return out


def gif(frame_list, path):
    imgs = [led_panel(px, 6, 10, BG, glow=False).convert("RGB") for px in frame_list]
    imgs[0].save(path, save_all=True, append_images=imgs[1:], duration=[MS] * len(imgs), loop=0, optimize=True)


def sheet(f, key, title):
    picks = [(cap, f[f"{key} {scr}"][min(ms // MS, len(f[f'{key} {scr}']) - 1)]) for scr, ms, cap in PICKS[key]]
    tiles = [led_panel(px, 7, 12) for _, px in picks]
    w, gap, cap_h, top = tiles[0].width, 22, 44, 70
    img = Image.new("RGBA", (len(tiles) * w + (len(tiles) - 1) * gap + 40, w + cap_h + top + 20), BG)
    d = ImageDraw.Draw(img)
    d.text((20, 18), title, font=font(32, bold=True), fill=INK)
    for k, (tile, (label, _)) in enumerate(zip(tiles, picks)):
        x = 20 + k * (w + gap)
        img.alpha_composite(tile, (x, top))
        tw = d.textlength(label, font=font(17))
        d.text((x + (w - tw) / 2, top + w + 10), label, font=font(17), fill=MUTED)
    return img


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    f = frames(src)
    rows = []
    for key, folder, title in SETS:
        d = os.path.join(out, folder)
        os.makedirs(d, exist_ok=True)
        for screen, name in SCREENS:
            gif(f[f"{key} {screen}"], os.path.join(d, name))
        s = sheet(f, key, title)
        s.save(os.path.join(d, "sheet.png"), optimize=True)
        rows.append(s)
    cmp_img = Image.new("RGBA", (max(r.width for r in rows), sum(r.height for r in rows) + 20 * (len(rows) - 1)), BG)
    y = 0
    for r in rows:
        cmp_img.alpha_composite(r, (0, y))
        y += r.height + 20
    cmp_img.save(os.path.join(out, "compare.png"), optimize=True)
    print("written to", out)


if __name__ == "__main__":
    main()

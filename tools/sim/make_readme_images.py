"""Pictures for the README, drawn from the firmware's own screens (as the simulator renders them):

    ./sim screens                                  # writes screens.bin
    python tools/sim/make_readme_images.py screens.bin docs/images
    python tools/make_brand.py screens.bin logo     # the banner, the web UI's logo and the favicons

screens.png  starting up, first start, installing firmware and saving side by side, with captions
boot.gif     the boot animation
Needs Pillow (pip install pillow).
"""
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

FRAME = 32 + 32 * 32 * 3


def read_frames(path):
    data = open(path, "rb").read()
    out = {}
    for i in range(len(data) // FRAME):
        rec = data[i * FRAME:(i + 1) * FRAME]
        out.setdefault(rec[:32].split(b"\0")[0].decode(), []).append(rec[32:])
    return out


def led_panel(rgb, pitch, pad, bg=None, glow=True):
    """32 x 32 RGB bytes -> an LED panel picture: round dots, dark unlit LEDs, a soft glow, a bezel."""
    ss = 4                                              # draw 4x larger, then shrink: smooth circles
    p, size = pitch * ss, (32 * pitch + 2 * pad) * ss
    base = Image.new("RGBA", (size, size), (0, 0, 0, 0) if bg is None else bg)
    d = ImageDraw.Draw(base)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=pad * ss * 0.9, fill=(6, 7, 9, 255))
    lit = Image.new("RGB", (size, size), (0, 0, 0))
    dl = ImageDraw.Draw(lit)
    o = pad * ss
    for y in range(32):
        for x in range(32):
            i = (y * 32 + x) * 3
            r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
            cx, cy = o + x * p + p / 2, o + y * p + p / 2
            if r + g + b > 24:
                rad = p * 0.43
                dl.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=(r, g, b))
            else:
                rad = p * 0.36
                d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=(24, 26, 31, 255))
    lit_mask = lit.convert("L").point(lambda v: 255 if v else 0)
    rgb_img = base.convert("RGB")
    rgb_img.paste(lit, (0, 0), lit_mask)                 # the lit LEDs replace their dark dots
    if glow:                                            # and light up the bezel around them
        halo = lit.filter(ImageFilter.GaussianBlur(p * 0.9)).point(lambda v: int(v * 0.45))
        rgb_img = ImageChops.add(rgb_img, halo)
    out = rgb_img.convert("RGBA")
    out.putalpha(base.getchannel("A"))
    return out.resize((size // ss, size // ss), Image.LANCZOS)


def font(size, bold=False):
    names = ["segoeuib.ttf" if bold else "segoeui.ttf", "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"]
    for n in names:
        for d in ("C:/Windows/Fonts", "/usr/share/fonts/TTF", "/usr/share/fonts/truetype/dejavu", ""):
            try:
                return ImageFont.truetype(os.path.join(d, n) if d else n, size)
            except OSError:
                pass
    return ImageFont.load_default(size=size)


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    f = read_frames(src)

    # frames are 40 ms apart: the logo complete, the name scrolling, 62 % of an update, 45 % of a save
    shots = [(f["boot"][33], "Starting up"), (f["welcome"][38], "First start"), (f["update"][77], "Installing firmware"),
             (f["saving"][36], "Saving an animation")]
    tiles = [led_panel(px, 9, 16) for px, _ in shots]
    w, gap, cap = tiles[0].width, 40, 56
    sheet = Image.new("RGBA", (len(tiles) * w + (len(tiles) - 1) * gap, w + cap), (0, 0, 0, 0))
    dr = ImageDraw.Draw(sheet)
    fnt = font(26, bold=True)
    for k, (tile, (_, label)) in enumerate(zip(tiles, shots)):
        x = k * (w + gap)
        sheet.alpha_composite(tile, (x, 0))
        tw = dr.textlength(label, font=fnt)
        dr.text((x + (w - tw) / 2, w + 14), label, font=fnt, fill=(142, 150, 163, 255))
    sheet.save(os.path.join(out, "screens.png"), optimize=True)

    bg = (13, 15, 18, 255)                             # GIFs have no soft transparency: a dark page
    frames = [led_panel(px, 7, 12, bg, glow=False).convert("RGB") for px in f["boot"]]   # no glow: a small GIF
    durations = [40] * (len(frames) - 1) + [1600]
    frames[0].save(os.path.join(out, "boot.gif"), save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True)
    print("wrote", ", ".join(sorted(os.listdir(out))))


if __name__ == "__main__":
    main()

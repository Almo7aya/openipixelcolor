"""Pictures for the README, drawn from the firmware's own screens (as the simulator renders them):

    ./sim screens                                  # writes screens.bin
    python tools/sim/make_readme_images.py screens.bin docs/images
    python tools/make_brand.py screens.bin logo     # the banner, the web UI's logo and the favicons

boot.gif, first-start.gif, update.gif, saving.gif   the panel's own screens, animated (under the README's
             warning, side by side)
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

    # each screen as a GIF, frames 40 ms apart as the simulator drew them; the last frame of the ones that
    # end (boot, update, saving) stays a moment before it loops. No glow, on a dark page: small files.
    # The caption is drawn into the GIF, so the README can show the four as plain images (24 % wide each, in
    # one row at any width) instead of a table, which GitHub makes scroll sideways when it's too wide.
    bg = (13, 15, 18, 255)
    fnt = font(17, bold=True)
    for label, name, step, hold, caption in (("boot", "boot.gif", 1, 1600, "Starting up"),
                                             ("welcome", "first-start.gif", 1, 40, "First start"),
                                             ("update", "update.gif", 2, 1800, "Installing firmware"),
                                             ("saving", "saving.gif", 1, 1200, "Saving an animation")):
        src = f[label][::step]
        if src[-1] is not f[label][-1]:
            src.append(f[label][-1])                    # always end on the last frame (the tick, "saved")
        frames = []
        for px in src:
            panel = led_panel(px, 6, 10, bg, glow=False).convert("RGB")      # 212 px wide
            tile = Image.new("RGB", (panel.width, panel.height + 34), bg[:3])
            tile.paste(panel, (0, 0))
            d = ImageDraw.Draw(tile)
            tw = d.textlength(caption, font=fnt)
            d.text(((tile.width - tw) / 2, panel.height + 6), caption, font=fnt, fill=(170, 176, 186))
            frames.append(tile)
        durations = [40 * step] * (len(frames) - 1) + [hold]
        frames[0].save(os.path.join(out, name), save_all=True, append_images=frames[1:],
                       duration=durations, loop=0, optimize=True)
    print("wrote", ", ".join(sorted(os.listdir(out))))


if __name__ == "__main__":
    main()

"""The logo everywhere outside the panel, from the panel's own logo frame (32 x 32 RGB, as the firmware or
the design prototype draws it):

    python tools/make_brand.py <frames.bin> <label>       # e.g. screens.bin logo (./sim screens)

docs/images/banner.png          the README's wide header: the logo on an LED panel, the name in LED dots
webui/icons/logo.svg            the mark for the web UI: one round LED per lit pixel, no background
webui/icons/favicon.svg         the browser tab icon: square pixels on a dark rounded square
webui/favicon.ico               16, 32 and 48 px, for browsers that want an .ico
webui/icons/apple-touch-icon.png, icon-192.png, icon-512.png   home screens and the web app manifest
Needs Pillow.
"""
import colorsys
import os
import re
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
from make_readme_images import font, led_panel, read_frames  # noqa: E402

BG = (13, 15, 18)                       # the page and the panel surround
INK = (240, 244, 255)                   # the "i", and the name's white
MUTED = (142, 150, 163)


def font5x7():
    """the panel font from the web UI's copy (firmware/src/app/font5x7.c): 95 glyphs, column bytes"""
    src = open(os.path.join(ROOT, "webui", "js", "font5x7.js"), encoding="utf-8").read()
    return [[int(v, 16) for v in g.split(",")] for g in re.findall(r"\[(0x[0-9A-Fa-f]{2}(?:,0x[0-9A-Fa-f]{2}){4})\]", src)]


def text_columns(text, glyphs):
    cols = []
    for ch in text:
        if ch == " ":
            cols += [0, 0, 0]
            continue
        g = glyphs[ord(ch) - 32]
        a, b = 0, 4
        while a < 4 and not g[a]:
            a += 1
        while b > a and not g[b]:
            b -= 1
        cols += g[a:b + 1] + [0]
    return cols[:-1]


def ring_hue(i, n=5):
    """the five colours the panel uses for "color": taken round the ring (gfx_hsv(i * 48, 220, 255))"""
    r, g, b = colorsys.hsv_to_rgb((i * 48 % 256) / 256, 220 / 255, 1.0)
    return int(r * 255), int(g * 255), int(b * 255)


def led_word(text, colours, pitch):
    """a word in LED dots (the panel font), each letter its own colour, with a soft glow"""
    glyphs = font5x7()
    ss = 4
    p = pitch * ss
    cols, letter_of, k = [], [], 0
    for ch in text:                                     # which letter each column belongs to
        c = text_columns(ch, glyphs) if ch != " " else [0, 0]
        cols += c + [0]
        letter_of += [k] * (len(c) + 1)
        k += 1
    w, h = len(cols) * p + 4 * p, 7 * p + 4 * p
    lit = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(lit)
    for x, bits in enumerate(cols):
        for y in range(7):
            if bits >> y & 1:
                cx, cy, rad = 2 * p + x * p + p / 2, 2 * p + y * p + p / 2, p * 0.42
                d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], fill=colours[letter_of[x] % len(colours)])
    halo = lit.filter(ImageFilter.GaussianBlur(p * 0.9)).point(lambda v: int(v * 0.5))
    out = ImageChops.add(lit, halo)
    alpha = out.convert("L").point(lambda v: min(255, v * 3))
    out = out.convert("RGBA")
    out.putalpha(alpha)
    return out.resize((w // ss, h // ss), Image.LANCZOS)


def banner(logo, path):
    W, H = 1760, 560
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=40, fill=BG + (255,))
    panel = led_panel(logo, 12, 22)
    py = (H - panel.height) // 2
    img.alpha_composite(panel, (py, py))
    x = py + panel.width + 70
    name = "openipixelcolor"
    colours = [INK] * 10 + [ring_hue(i) for i in range(5)]
    word = led_word(name, colours, 13)
    img.alpha_composite(word, (x - 26, 150))
    d.text((x, 318), "Open firmware and a web app for", font=font(40), fill=MUTED + (255,))
    d.text((x, 368), "Bluetooth LED pixel panels", font=font(40), fill=MUTED + (255,))
    img.save(path, optimize=True)


def svg_pixels(logo, round_leds, background):
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"' + ('' if round_leds else ' shape-rendering="crispEdges"') + '>']
    if background:
        parts.append('<rect width="32" height="32" rx="6" fill="#0d0f12"/>')
    for y in range(32):
        for x in range(32):
            i = (y * 32 + x) * 3
            r, g, b = logo[i], logo[i + 1], logo[i + 2]
            if r + g + b <= 24:
                continue
            c = f"#{r:02x}{g:02x}{b:02x}"
            if round_leds:
                parts.append(f'<circle cx="{x + .5}" cy="{y + .5}" r=".44" fill="{c}"/>')
            else:
                parts.append(f'<rect x="{x}" y="{y}" width="1" height="1" fill="{c}"/>')
    parts.append("</svg>")
    return "".join(parts)


def pixel_icon(logo, size, margin, rounded):
    """square pixels, scaled by a whole number, on the dark background"""
    k = (size - 2 * margin) // 32
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if rounded:
        d.rounded_rectangle([0, 0, size - 1, size - 1], radius=size * 6 // 32, fill=BG + (255,))
    else:
        d.rectangle([0, 0, size - 1, size - 1], fill=BG + (255,))
    o = (size - 32 * k) // 2
    for y in range(32):
        for x in range(32):
            i = (y * 32 + x) * 3
            if logo[i] + logo[i + 1] + logo[i + 2] > 24:
                d.rectangle([o + x * k, o + y * k, o + x * k + k - 1, o + y * k + k - 1], fill=tuple(logo[i:i + 3]) + (255,))
    return img


def main():
    src, label = sys.argv[1], sys.argv[2]
    logo = read_frames(src)[label][0]
    icons = os.path.join(ROOT, "webui", "icons")
    os.makedirs(icons, exist_ok=True)

    banner(logo, os.path.join(ROOT, "docs", "images", "banner.png"))
    open(os.path.join(icons, "logo.svg"), "w", encoding="utf-8").write(svg_pixels(logo, True, False))
    open(os.path.join(icons, "favicon.svg"), "w", encoding="utf-8").write(svg_pixels(logo, False, True))
    big = pixel_icon(logo, 256, 0, True)                 # 8 px a pixel: shrinks cleanly to 16, 32 and 48
    big.save(os.path.join(ROOT, "webui", "favicon.ico"), sizes=[(16, 16), (32, 32), (48, 48)])
    pixel_icon(logo, 180, 10, False).save(os.path.join(icons, "apple-touch-icon.png"), optimize=True)
    for size, pitch in ((512, 14), (192, 5)):            # LED dots on a full dark square (home screens mask it)
        tile = Image.new("RGBA", (size, size), BG + (255,))
        panel = led_panel(logo, pitch, (size - 32 * pitch) // 2)
        tile.alpha_composite(panel, ((size - panel.width) // 2, (size - panel.height) // 2))
        tile.save(os.path.join(icons, f"icon-{size}.png"), optimize=True)
    print("wrote docs/images/banner.png, webui/favicon.ico and webui/icons/:", ", ".join(sorted(os.listdir(icons))))


if __name__ == "__main__":
    main()

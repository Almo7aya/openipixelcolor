"""Turn tools/sim/frames.bin into a labelled contact sheet PNG (rows = scenes, 4x scaled frames).
usage: python make_sheet.py frames.bin sheet.png
"""
import sys
from collections import OrderedDict
from PIL import Image, ImageDraw

data = open(sys.argv[1], 'rb').read()
rec = 32 + 32 * 32 * 3
rows = OrderedDict()
for off in range(0, len(data) - rec + 1, rec):
    name = data[off:off + 32].split(b'\0')[0].decode()
    rows.setdefault(name, []).append(data[off + 32:off + rec])

S, GAP, LABEL = 4, 4, 92
cols = max(len(v) for v in rows.values())
W = LABEL + cols * (32 * S + GAP)
H = len(rows) * (32 * S + GAP)
sheet = Image.new('RGB', (W, H), (40, 40, 48))
draw = ImageDraw.Draw(sheet)
for r, (name, frames) in enumerate(rows.items()):
    y = r * (32 * S + GAP)
    draw.text((6, y + 56), name, fill=(230, 230, 230))
    for c, fr in enumerate(frames):
        img = Image.frombytes('RGB', (32, 32), fr).resize((32 * S, 32 * S), Image.NEAREST)
        sheet.paste(img, (LABEL + c * (32 * S + GAP), y))
sheet.save(sys.argv[2])
print(f'{len(rows)} scenes -> {sys.argv[2]} ({W}x{H})')

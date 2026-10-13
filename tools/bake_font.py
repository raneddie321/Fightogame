#!/usr/bin/env python3
"""Bake the FIGHTO UI font atlas.

Renders ASCII glyphs from the Inter typeface (SIL Open Font License 1.1) into a
single 8-bit atlas and writes assets/fonts/fonts.bin in a small custom format
read by src/ui/font.cpp:

  char[4]  magic "FFNT"
  u32      version (1)
  u32      atlas width, atlas height
  u32      font count
  per font:
     char[16] name
     f32 pixel size, ascent, descent, line height
     u32 glyph count
     per glyph: u32 codepoint; i16 x, y, w, h, xoff, yoff; f32 advance
  u8[w*h]  atlas pixels (row 0 = top). A 8x8 solid white block sits at (0,0).

Usage: python3 tools/bake_font.py [font_dir] [output]
"""
import struct
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_DIR = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/opentype/inter"
OUT = sys.argv[2] if len(sys.argv) > 2 else "assets/fonts/fonts.bin"

FONTS = [
    ("ui", "Inter-SemiBold.otf", 34),
    ("bold", "Inter-ExtraBold.otf", 44),
    ("title", "InterDisplay-Black.otf", 112),
]
ATLAS_W, ATLAS_H = 2048, 1024
PAD = 3

atlas = Image.new("L", (ATLAS_W, ATLAS_H), 0)
draw = ImageDraw.Draw(atlas)
draw.rectangle([0, 0, 7, 7], fill=255)

x, y, row_h = 12, 0, 12
fonts_out = []
for name, file, size in FONTS:
    font = ImageFont.truetype(f"{FONT_DIR}/{file}", size)
    ascent, descent = font.getmetrics()
    glyphs = []
    for cp in range(32, 127):
        ch = chr(cp)
        bbox = font.getbbox(ch)  # relative to origin at ascent baseline-top
        advance = font.getlength(ch)
        w = max(bbox[2] - bbox[0], 0)
        h = max(bbox[3] - bbox[1], 0)
        if x + w + PAD >= ATLAS_W:
            x = 0
            y += row_h + PAD
            row_h = 0
        if y + h + PAD >= ATLAS_H:
            raise SystemExit("atlas too small")
        if w > 0 and h > 0:
            draw.text((x - bbox[0], y - bbox[1]), ch, font=font, fill=255)
        glyphs.append((cp, x, y, w, h, bbox[0], bbox[1], advance))
        x += w + PAD
        row_h = max(row_h, h)
    fonts_out.append((name, float(size), float(ascent), float(descent), float(ascent + descent), glyphs))
    x = 0
    y += row_h + PAD
    row_h = 0

with open(OUT, "wb") as f:
    f.write(b"FFNT")
    f.write(struct.pack("<IIII", 1, ATLAS_W, ATLAS_H, len(fonts_out)))
    for name, size, asc, desc, lh, glyphs in fonts_out:
        f.write(name.encode().ljust(16, b"\0"))
        f.write(struct.pack("<ffff", size, asc, desc, lh))
        f.write(struct.pack("<I", len(glyphs)))
        for cp, gx, gy, w, h, xo, yo, adv in glyphs:
            f.write(struct.pack("<Ihhhhhhf", cp, gx, gy, w, h, xo, yo, adv))
    f.write(atlas.tobytes())
print(f"wrote {OUT}: {ATLAS_W}x{ATLAS_H}, used height {y}")

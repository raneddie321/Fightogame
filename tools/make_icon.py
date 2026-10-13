#!/usr/bin/env python3
"""Generates the FIGHTO application icon (original artwork, drawn procedurally).

Outputs:
  windows/fighto.ico                          (16..256 px, Windows executable icon)
  android/res/mipmap-*/ic_launcher.png        (Android launcher icons)
Usage: python3 tools/make_icon.py
"""
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def draw_icon(size):
    s = size * 4  # supersample for smooth edges
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Rounded dark plate with a red rim
    r = s * 0.18
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=r, fill=(14, 12, 14, 255))
    d.rounded_rectangle([s * 0.03, s * 0.03, s * 0.97, s * 0.97], radius=r * 0.85, outline=(150, 8, 10, 255), width=max(2, int(s * 0.025)))
    # Bold slanted "F"
    def P(x, y):
        return (s * x + (0.62 - y) * s * 0.10, s * y)
    blood = (196, 12, 14, 255)
    d.polygon([P(0.27, 0.18), P(0.76, 0.18), P(0.72, 0.33), P(0.43, 0.33), P(0.415, 0.44), P(0.64, 0.44),
               P(0.60, 0.58), P(0.395, 0.58), P(0.36, 0.80), P(0.20, 0.80)], fill=blood)
    # Drips running off the letter
    for (x, y0, length, w) in [(0.30, 0.78, 0.12, 0.045), (0.50, 0.32, 0.10, 0.035), (0.68, 0.32, 0.07, 0.03), (0.56, 0.57, 0.16, 0.04)]:
        cx = s * x
        d.rectangle([cx - s * w / 2, s * y0, cx + s * w / 2, s * (y0 + length)], fill=blood)
        d.ellipse([cx - s * w * 0.75, s * (y0 + length) - s * w * 0.75, cx + s * w * 0.75, s * (y0 + length) + s * w * 0.75], fill=blood)
    # Glossy highlight on the letter
    hl = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    hd = ImageDraw.Draw(hl)
    hd.polygon([P(0.29, 0.20), P(0.73, 0.20), P(0.725, 0.23), P(0.285, 0.23)], fill=(255, 140, 140, 110))
    img = Image.alpha_composite(img, hl.filter(ImageFilter.GaussianBlur(s * 0.004)))
    return img.resize((size, size), Image.LANCZOS)


def main():
    win = os.path.join(ROOT, "windows")
    os.makedirs(win, exist_ok=True)
    big = draw_icon(256)
    big.save(os.path.join(win, "fighto.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    dens = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
    for name, px in dens.items():
        out = os.path.join(ROOT, "android", "res", "mipmap-" + name)
        os.makedirs(out, exist_ok=True)
        draw_icon(px).save(os.path.join(out, "ic_launcher.png"))
    print("icons written")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Draws the 32x32 cartridge icons (original pixel art, no logos). The menu writes the system
name (GBA, NES, ...) onto the empty label at run time with the UI font, so it stays sharp at any size.

Writes source/cart_icons.c. Run:  python3 tools/gen_cart_icons.py [preview.png]
"""
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
T = (0, 0, 0, 0)


def rgb(h, a=255):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) + (a,)


def mix(c1, c2, t):
    return tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3)) + (255,)


def new():
    return Image.new("RGBA", (32, 32), T)


class Pal:
    """Shell colours: outline, dark, base, light, highlight."""
    def __init__(self, base, outline=None):
        b = rgb(base)
        self.base = b
        self.dark = mix(b, (0, 0, 0), 0.28)
        self.deep = mix(b, (0, 0, 0), 0.45)
        self.light = mix(b, (255, 255, 255), 0.22)
        self.hi = mix(b, (255, 255, 255), 0.45)
        self.out = rgb(outline) if outline else mix(b, (0, 0, 0), 0.72)


def body(im, x0, y0, x1, y1, p, cut=(), radius=1):
    """Shaded shell: vertical gradient, light top/left bevel, dark bottom/right bevel."""
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((x0, y0, x1, y1), radius=radius, fill=p.out)
    h = y1 - y0 - 1
    for y in range(y0 + 1, y1):
        t = (y - y0 - 1) / max(1, h - 1)
        col = mix(p.light, p.dark, t * 0.85)
        d.line((x0 + 1, y, x1 - 1, y), fill=col)
    d.line((x0 + 2, y0 + 1, x1 - 2, y0 + 1), fill=p.hi)
    d.line((x0 + 1, y0 + 2, x0 + 1, y1 - 2), fill=p.light)
    d.line((x0 + 2, y1 - 1, x1 - 1, y1 - 1), fill=p.deep)
    d.line((x1 - 1, y0 + 2, x1 - 1, y1 - 1), fill=p.dark)
    for (cx, cy, n) in cut:  # diagonal corner cuts: (corner x, corner y, size)
        sx = -1 if cx == x1 else 1
        sy = -1 if cy == y1 else 1
        for i in range(n):
            for j in range(n - i):
                im.putpixel((cx + sx * j, cy + sy * i), T)
        for i in range(n + 1):
            im.putpixel((cx + sx * (n - i), cy + sy * i), p.out)


def blank_label(im, x0, y0, x1, y1, paper="f4f1ea", radius=1):
    """Empty label: paper with a soft inner shade and a glossy top edge - no picture, no text."""
    d = ImageDraw.Draw(im)
    pc = rgb(paper)
    d.rounded_rectangle((x0, y0, x1, y1), radius=radius, fill=mix(pc, (0, 0, 0), 0.55))
    for y in range(y0 + 1, y1):
        t = (y - y0) / max(1, (y1 - y0))
        d.line((x0 + 1, y, x1 - 1, y), fill=mix(pc, mix(pc, (0, 0, 0), 0.12), t))
    d.line((x0 + 1, y0 + 1, x1 - 1, y0 + 1), fill=(255, 255, 255, 255))
    d.line((x1 - 1, y0 + 2, x1 - 1, y1 - 1), fill=mix(pc, (0, 0, 0), 0.18))
    d.line((x0 + 2, y1 - 1, x1 - 1, y1 - 1), fill=mix(pc, (0, 0, 0), 0.22))


def recess(im, x0, y0, x1, y1, p):
    """Label well pressed into the shell."""
    d = ImageDraw.Draw(im)
    d.rectangle((x0, y0, x1, y1), fill=p.dark)
    d.line((x0, y0, x1, y0), fill=p.deep)
    d.line((x0, y0, x0, y1), fill=p.deep)
    d.line((x0 + 1, y1, x1, y1), fill=p.light)
    d.line((x1, y0 + 1, x1, y1), fill=p.light)


def ridges(im, x0, x1, y, n, p, step=2):
    d = ImageDraw.Draw(im)
    for i in range(n):
        d.line((x0, y + i * step, x1, y + i * step), fill=p.deep)
        d.line((x0, y + i * step + 1, x1, y + i * step + 1), fill=p.light)


def arrow(im, cx, y, p):
    d = ImageDraw.Draw(im)
    for i, w in enumerate((2, 1, 0)):                       # small "insert" triangle
        d.line((cx - w, y + i, cx + w, y + i), fill=p.deep)


def gb(colour=False):
    im = new()
    p = Pal("3d3f4c") if colour else Pal("b4b4bd")
    body(im, 6, 1, 25, 30, p, cut=((25, 1, 3),))
    ridges(im, 9, 21, 3, 2, p)
    recess(im, 8, 8, 23, 25, p)
    blank_label(im, 9, 9, 22, 24, "f3f1e8" if not colour else "f6f3ea")
    arrow(im, 15, 27, p)
    return im


def gba():
    im = new()
    p = Pal("4a4960")
    d = ImageDraw.Draw(im)
    body(im, 2, 8, 29, 26, p, radius=2)
    d.rounded_rectangle((9, 4, 22, 9), radius=1, fill=p.out)          # top tab
    d.rectangle((10, 5, 21, 8), fill=p.light)
    d.line((10, 5, 21, 5), fill=p.hi)
    recess(im, 6, 11, 25, 23, p)
    blank_label(im, 7, 12, 24, 22)
    return im


def nes():
    im = new()
    p = Pal("9d9da4")
    body(im, 4, 1, 27, 30, p)
    recess(im, 7, 3, 24, 17, p)
    blank_label(im, 8, 4, 23, 16)
    ridges(im, 7, 24, 20, 4, p, step=2)
    arrow(im, 15, 28, p)
    return im


def snes():
    im = new()
    p = Pal("bdbdc6")
    d = ImageDraw.Draw(im)
    body(im, 2, 3, 29, 28, p, radius=4)
    for x in (2, 29):                                                  # side grips
        for y in range(11, 22, 2):
            d.point((x, y), fill=p.out)
            d.point((x + (1 if x == 2 else -1), y), fill=p.deep)
    recess(im, 5, 7, 26, 23, p)
    blank_label(im, 6, 8, 25, 22)
    d.line((10, 25, 21, 25), fill=p.deep)
    return im


def md():
    im = new()
    p = Pal("2c2c33")
    body(im, 3, 2, 28, 29, p, cut=((3, 2, 4),))
    recess(im, 7, 6, 24, 24, p)
    blank_label(im, 8, 7, 23, 23)
    ridges(im, 10, 21, 26, 1, p)
    return im


def sms():
    im = new()
    p = Pal("27272c")
    body(im, 3, 5, 28, 27, p, radius=1)
    recess(im, 6, 8, 25, 22, p)
    blank_label(im, 7, 9, 24, 21)
    arrow(im, 15, 24, p)
    return im


def gg():
    im = new()
    p = Pal("303038")
    body(im, 5, 5, 26, 27, p, radius=3)
    recess(im, 8, 8, 23, 22, p)
    blank_label(im, 9, 9, 22, 21)
    arrow(im, 15, 24, p)
    return im


def ds_card():
    """A DS game card: dark shell with a chamfered corner, a big label and grip lines."""
    im = new()
    p = Pal("44474f")
    d = ImageDraw.Draw(im)
    body(im, 5, 2, 26, 29, p, cut=((26, 2, 3),), radius=2)
    recess(im, 8, 5, 23, 20, p)
    blank_label(im, 9, 6, 22, 19)
    for x in range(9, 23, 2):                       # grip lines at the bottom
        d.line((x, 23, x, 27), fill=p.deep)
        d.line((x + 1, 23, x + 1, 27), fill=p.light)
    return im


# label rectangles (inner paper area) in icon pixels, used by the menu for the system name
LABELS = {"TYPE_GBA": (7, 12, 24, 22), "TYPE_GB": (9, 9, 22, 24), "TYPE_GBC": (9, 9, 22, 24),
          "TYPE_NES": (8, 4, 23, 16), "TYPE_SNES": (6, 8, 25, 22), "TYPE_GENESIS": (8, 7, 23, 23),
          "TYPE_SMS": (7, 9, 24, 21), "TYPE_GG": (9, 9, 22, 21), "TYPE_NDS": (9, 6, 22, 19), "TYPE_SLOT1": (9, 6, 22, 19)}
NAMES = {"TYPE_GBA": "GBA", "TYPE_GB": "GB", "TYPE_GBC": "GBC", "TYPE_NES": "NES", "TYPE_SNES": "SNES",
         "TYPE_GENESIS": "MD", "TYPE_SMS": "SMS", "TYPE_GG": "GG", "TYPE_NDS": "DS", "TYPE_SLOT1": "DS"}

ICONS = [("TYPE_NDS", ds_card), ("TYPE_GBA", gba), ("TYPE_GB", lambda: gb(False)), ("TYPE_GBC", lambda: gb(True)), ("TYPE_NES", nes),
         ("TYPE_SNES", snes), ("TYPE_GENESIS", md), ("TYPE_SMS", sms), ("TYPE_GG", gg)]


def to555(px):
    r, g, b, a = px
    if a < 128:
        return 0
    return 0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def main():
    out = ['// Generated by tools/gen_cart_icons.py - original 32x32 cartridge art for non-DS systems',
           '#include "cart_icons.h"', '#include <stddef.h>', '']
    images = []
    for name, fn in ICONS:
        im = fn()
        images.append(im)
        vals = [to555(im.getpixel((x, y))) for y in range(32) for x in range(32)]
        out.append(f"static const Color s_{name.lower()}[1024] = {{")
        for i in range(0, 1024, 16):
            out.append("    " + ", ".join(f"0x{v:04X}" for v in vals[i:i + 16]) + ",")
        out.append("};")
    out.append("")
    out.append("const Color *cart_icon(ItemType type) {")
    out.append("    switch (type) {")
    for name, _ in ICONS:
        out.append(f"        case {name}: return s_{name.lower()};")
    out.append("        case TYPE_SLOT1: return s_type_nds;")
    out.append("        default: return NULL;")
    out.append("    }")
    out.append("}")
    out.append("")
    out.append("bool cart_label(ItemType type, int *x0, int *y0, int *x1, int *y1, const char **name) {")
    out.append("    switch (type) {")
    for name, (a, b, c, d) in LABELS.items():
        out.append(f'        case {name}: *x0 = {a}; *y0 = {b}; *x1 = {c}; *y1 = {d}; *name = "{NAMES[name]}"; return true;')
    out.append("        default: return false;")
    out.append("    }")
    out.append("}")
    with open(os.path.join(ROOT, "source", "cart_icons.c"), "w") as f:
        f.write("\n".join(out) + "\n")
    if len(sys.argv) > 1:
        sheet = Image.new("RGBA", (len(images) * 104, 104), (40, 40, 40, 255))
        for i, im in enumerate(images):
            sheet.alpha_composite(im.resize((96, 96), Image.NEAREST), (i * 104 + 4, 4))
        sheet.save(sys.argv[1])


if __name__ == "__main__":
    main()

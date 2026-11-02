#!/usr/bin/env python3
"""
DSplay - vector icon generator.

Draws the UI's monochrome icons with Pillow at 8x resolution, downsamples
them with a box filter (anti-aliasing) and writes 4-bit alpha masks to
source/icon_data.c + include/icon_data.h. Icons are tinted at runtime.

Usage: python3 tools/gen_icons.py
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
BOLD = "/usr/share/fonts/opentype/inter/Inter-Bold.otf"
SS = 8  # supersampling factor


def canvas(size):
    s = size * SS
    img = Image.new("L", (s, s), 0)
    return img, ImageDraw.Draw(img), s


def U(v):
    return v * SS


def finish(img, size):
    return img.resize((size, size), Image.BOX)


def rrect(d, box, r, fill=255, outline=None, width=0):
    d.rounded_rectangle([U(box[0]), U(box[1]), U(box[2]), U(box[3])], radius=U(r), fill=fill,
                        outline=outline, width=int(U(width)) if width else 0)


def circle(d, cx, cy, r, fill=255, outline=None, width=0):
    d.ellipse([U(cx - r), U(cy - r), U(cx + r), U(cy + r)], fill=fill, outline=outline,
              width=int(U(width)) if width else 0)


def line(d, pts, w, fill=255):
    d.line([(U(x), U(y)) for x, y in pts], fill=fill, width=int(U(w)), joint="curve")
    for x, y in (pts[0], pts[-1]):
        circle(d, x, y, w / 2, fill=fill)


def arc(d, cx, cy, r, start, end, w, fill=255):
    d.arc([U(cx - r), U(cy - r), U(cx + r), U(cy + r)], start, end, fill=fill, width=int(U(w)))


def icon_grid(n):
    img, d, s = canvas(n)
    g = n / 16.0
    for iy in range(2):
        for ix in range(2):
            x0 = (2 + ix * 6.5) * g
            y0 = (2 + iy * 6.5) * g
            rrect(d, (x0, y0, x0 + 5.5 * g, y0 + 5.5 * g), 1.4 * g)
    return finish(img, n)


def icon_cart(n):
    img, d, s = canvas(n)
    g = n / 16.0
    pts = [(3 * g, 1.5 * g), (11 * g, 1.5 * g), (13 * g, 3.5 * g), (13 * g, 14.5 * g), (3 * g, 14.5 * g)]
    d.polygon([(U(x), U(y)) for x, y in pts], fill=255)
    rrect(d, (5 * g, 4 * g, 11 * g, 9 * g), 0.8 * g, fill=0)
    for i in range(4):
        x = 5 * g + i * 1.7 * g
        d.rectangle([U(x), U(11.2 * g), U(x + 0.9 * g), U(14.5 * g)], fill=0)
    return finish(img, n)


def icon_search(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 6.8 * g, 6.8 * g, 4.6 * g, fill=None, outline=255, width=1.9 * g)
    line(d, [(10.3 * g, 10.3 * g), (14 * g, 14 * g)], 2.2 * g)
    return finish(img, n)


def icon_gear(n):
    img, d, s = canvas(n)
    g = n / 16.0
    cx = cy = 8 * g
    teeth = 8
    poly = []
    for i in range(teeth * 4):
        a = (i / (teeth * 4.0)) * 2 * math.pi
        phase = i % 4
        r = 7.6 * g if phase in (0, 1) else 5.9 * g
        poly.append((U(cx + math.cos(a) * r), U(cy + math.sin(a) * r)))
    d.polygon(poly, fill=255)
    circle(d, cx, cy, 2.4 * g, fill=0)
    return finish(img, n)


def icon_power(n):
    img, d, s = canvas(n)
    g = n / 16.0
    arc(d, 8 * g, 8.6 * g, 5.6 * g, -60 - 0, 240, 1.9 * g)
    line(d, [(8 * g, 1.6 * g), (8 * g, 7.6 * g)], 1.9 * g)
    return finish(img, n)


def icon_star(n):
    img, d, s = canvas(n)
    g = n / 16.0
    cx, cy = 8 * g, 8.4 * g
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        r = 7.4 * g if i % 2 == 0 else 3.1 * g
        pts.append((U(cx + math.cos(a) * r), U(cy + math.sin(a) * r)))
    d.polygon(pts, fill=255)
    return finish(img, n)


def icon_play(n):
    img, d, s = canvas(n)
    g = n / 16.0
    d.polygon([(U(4.5 * g), U(2.5 * g)), (U(13.5 * g), U(8 * g)), (U(4.5 * g), U(13.5 * g))], fill=255)
    return finish(img, n)


def icon_save(n):
    img, d, s = canvas(n)
    g = n / 16.0
    rrect(d, (2 * g, 2 * g, 14 * g, 14 * g), 2 * g)
    rrect(d, (4.5 * g, 2 * g, 11.5 * g, 6.5 * g), 0.6 * g, fill=0)
    rrect(d, (4 * g, 9 * g, 12 * g, 14.5 * g), 0.8 * g, fill=0)
    return finish(img, n)


def icon_bolt(n):
    img, d, s = canvas(n)
    g = n / 16.0
    pts = [(9.5, 1), (3.5, 9), (7.5, 9), (6.2, 15), (12.5, 6.5), (8.5, 6.5), (9.5, 1)]
    d.polygon([(U(x * g), U(y * g)) for x, y in pts], fill=255)
    return finish(img, n)


def icon_note(n):
    img, d, s = canvas(n)
    g = n / 16.0
    rrect(d, (3 * g, 1.5 * g, 13 * g, 14.5 * g), 1.8 * g)
    for i, w in enumerate((7, 7, 4.5)):
        y = (5 + i * 3) * g
        rrect(d, (5.2 * g, y, (5.2 + w) * g, y + 1.3 * g), 0.6 * g, fill=0)
    return finish(img, n)


def icon_trash(n):
    img, d, s = canvas(n)
    g = n / 16.0
    rrect(d, (2 * g, 3 * g, 14 * g, 4.8 * g), 0.8 * g)
    rrect(d, (6 * g, 1.2 * g, 10 * g, 3.5 * g), 0.8 * g)
    d.polygon([(U(3.4 * g), U(5.5 * g)), (U(12.6 * g), U(5.5 * g)), (U(11.6 * g), U(15 * g)), (U(4.4 * g), U(15 * g))], fill=255)
    for x in (6.2, 9.0):
        rrect(d, (x * g, 7.5 * g, (x + 0.9) * g, 13 * g), 0.4 * g, fill=0)
    return finish(img, n)


def icon_eye_off(n):
    img, d, s = canvas(n)
    g = n / 16.0
    d.ellipse([U(1 * g), U(3.5 * g), U(15 * g), U(12.5 * g)], fill=255)
    circle(d, 8 * g, 8 * g, 3.2 * g, fill=0)
    circle(d, 8 * g, 8 * g, 1.6 * g, fill=255)
    line(d, [(2.5 * g, 14 * g), (13.5 * g, 2 * g)], 2.6 * g, fill=0)
    line(d, [(2.5 * g, 14 * g), (13.5 * g, 2 * g)], 1.3 * g, fill=255)
    return finish(img, n)


def icon_restore(n):
    img, d, s = canvas(n)
    g = n / 16.0
    arc(d, 8 * g, 8.5 * g, 5.5 * g, 200, 500, 1.9 * g)
    d.polygon([(U(0.8 * g), U(6.2 * g)), (U(6.2 * g), U(5.8 * g)), (U(3.2 * g), U(10.8 * g))], fill=255)
    return finish(img, n)


def icon_sort(n):
    img, d, s = canvas(n)
    g = n / 16.0
    for i, w in enumerate((12, 9, 6)):
        y = (3 + i * 4) * g
        rrect(d, (2 * g, y, (2 + w) * g, y + 2 * g), 1 * g)
    return finish(img, n)


def icon_sun(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 8 * g, 8 * g, 3.3 * g)
    for i in range(8):
        a = i * math.pi / 4
        line(d, [(8 * g + math.cos(a) * 5.4 * g, 8 * g + math.sin(a) * 5.4 * g),
                 (8 * g + math.cos(a) * 6.9 * g, 8 * g + math.sin(a) * 6.9 * g)], 1.5 * g)
    return finish(img, n)


def icon_speaker(n):
    img, d, s = canvas(n)
    g = n / 16.0
    d.polygon([(U(1.5 * g), U(6 * g)), (U(4.5 * g), U(6 * g)), (U(8.5 * g), U(2.5 * g)), (U(8.5 * g), U(13.5 * g)),
               (U(4.5 * g), U(10 * g)), (U(1.5 * g), U(10 * g))], fill=255)
    arc(d, 8.5 * g, 8 * g, 3.4 * g, -50, 50, 1.4 * g)
    arc(d, 8.5 * g, 8 * g, 6.2 * g, -50, 50, 1.4 * g)
    return finish(img, n)


def icon_globe(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 8 * g, 8 * g, 6.6 * g, fill=None, outline=255, width=1.5 * g)
    d.ellipse([U(5 * g), U(1.4 * g), U(11 * g), U(14.6 * g)], outline=255, width=int(U(1.3 * g)))
    d.line([U(1.6 * g), U(8 * g), U(14.4 * g), U(8 * g)], fill=255, width=int(U(1.3 * g)))
    return finish(img, n)


def icon_theme(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 8 * g, 8 * g, 6.6 * g, fill=None, outline=255, width=1.5 * g)
    d.pieslice([U(1.4 * g), U(1.4 * g), U(14.6 * g), U(14.6 * g)], 90, 270, fill=255)
    return finish(img, n)


def icon_user(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 8 * g, 5.2 * g, 3.4 * g)
    d.pieslice([U(1.8 * g), U(9.5 * g), U(14.2 * g), U(21.5 * g)], 180, 360, fill=255)
    return finish(img, n)


def icon_info(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 8 * g, 8 * g, 7 * g)
    circle(d, 8 * g, 4.6 * g, 1.2 * g, fill=0)
    rrect(d, (7 * g, 6.8 * g, 9 * g, 12.4 * g), 0.8 * g, fill=0)
    return finish(img, n)


def icon_check(n):
    img, d, s = canvas(n)
    g = n / 16.0
    line(d, [(2.8 * g, 8.4 * g), (6.4 * g, 12 * g), (13.4 * g, 4.2 * g)], 2.2 * g)
    return finish(img, n)


def icon_home(n):
    img, d, s = canvas(n)
    g = n / 16.0
    d.polygon([(U(8 * g), U(1.5 * g)), (U(15 * g), U(8 * g)), (U(13 * g), U(8 * g)), (U(13 * g), U(14.5 * g)),
               (U(3 * g), U(14.5 * g)), (U(3 * g), U(8 * g)), (U(1 * g), U(8 * g))], fill=255)
    rrect(d, (6.5 * g, 9.5 * g, 9.5 * g, 14.5 * g), 0.6 * g, fill=0)
    return finish(img, n)


def icon_chev(n, right=True):
    img, d, s = canvas(n)
    g = n / 16.0
    if right:
        line(d, [(6 * g, 3 * g), (11 * g, 8 * g), (6 * g, 13 * g)], 2 * g)
    else:
        line(d, [(10 * g, 3 * g), (5 * g, 8 * g), (10 * g, 13 * g)], 2 * g)
    return finish(img, n)


def icon_gamepad(n):
    img, d, s = canvas(n)
    g = n / 16.0
    rrect(d, (1 * g, 4 * g, 15 * g, 12.5 * g), 4 * g)
    rrect(d, (3.6 * g, 7.6 * g, 7.4 * g, 8.9 * g), 0.4 * g, fill=0)
    rrect(d, (4.85 * g, 6.3 * g, 6.15 * g, 10.2 * g), 0.4 * g, fill=0)
    circle(d, 10.6 * g, 7.4 * g, 0.95 * g, fill=0)
    circle(d, 12.2 * g, 9.2 * g, 0.95 * g, fill=0)
    return finish(img, n)


def icon_folder(n):
    img, d, s = canvas(n)
    g = n / 16.0
    rrect(d, (1.5 * g, 2.5 * g, 7.5 * g, 6 * g), 1 * g)
    rrect(d, (1.5 * g, 4.2 * g, 14.5 * g, 13.5 * g), 1.6 * g)
    return finish(img, n)


def icon_clock(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 8 * g, 8 * g, 6.6 * g, fill=None, outline=255, width=1.6 * g)
    line(d, [(8 * g, 4.2 * g), (8 * g, 8 * g), (11 * g, 9.8 * g)], 1.5 * g)
    return finish(img, n)


def icon_warning(n):
    img, d, s = canvas(n)
    g = n / 16.0
    d.polygon([(U(8 * g), U(1 * g)), (U(15.4 * g), U(14.6 * g)), (U(0.6 * g), U(14.6 * g))], fill=255)
    rrect(d, (7.1 * g, 5.4 * g, 8.9 * g, 10.6 * g), 0.8 * g, fill=0)
    circle(d, 8 * g, 12.5 * g, 1.05 * g, fill=0)
    return finish(img, n)


def icon_heart(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 5.2 * g, 6 * g, 3.6 * g)
    circle(d, 10.8 * g, 6 * g, 3.6 * g)
    d.polygon([(U(1.75 * g), U(7.2 * g)), (U(14.25 * g), U(7.2 * g)), (U(8 * g), U(14.5 * g))], fill=255)
    return finish(img, n)


def icon_lock(n):
    img, d, s = canvas(n)
    g = n / 16.0
    arc(d, 8 * g, 6.6 * g, 3.6 * g, 180, 360, 1.8 * g)
    d.rectangle([U(4.4 * g), U(6.6 * g), U(5.3 * g), U(8.5 * g)], fill=255)
    d.rectangle([U(10.7 * g), U(6.6 * g), U(11.6 * g), U(8.5 * g)], fill=255)
    rrect(d, (2.6 * g, 7.8 * g, 13.4 * g, 15 * g), 1.6 * g)
    circle(d, 8 * g, 11.2 * g, 1.3 * g, fill=0)
    return finish(img, n)


def icon_chart(n):
    img, d, s = canvas(n)
    g = n / 16.0
    for i, h in enumerate((5, 9, 7, 12)):
        x0 = (1.6 + i * 3.4) * g
        rrect(d, (x0, (14.5 - h) * g, x0 + 2.4 * g, 14.5 * g), 0.8 * g)
    return finish(img, n)


def icon_palette(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 8 * g, 8 * g, 7 * g)
    for cx, cy in ((5, 6), (8.3, 4.2), (11.3, 6.3), (10.8, 9.8)):
        circle(d, cx * g, cy * g, 1.35 * g, fill=0)
    circle(d, 5.6 * g, 10.6 * g, 1.8 * g, fill=0)
    return finish(img, n)


def icon_users(n):
    img, d, s = canvas(n)
    g = n / 16.0
    circle(d, 5.6 * g, 5.4 * g, 2.7 * g)
    d.pieslice([U(0.6 * g), U(9.4 * g), U(10.6 * g), U(19.4 * g)], 180, 360, fill=255)
    circle(d, 11.4 * g, 4.4 * g, 2.3 * g)
    d.pieslice([U(8.2 * g), U(8.6 * g), U(15.4 * g), U(16.6 * g)], 180, 360, fill=255)
    return finish(img, n)


def icon_plus(n):
    img, d, s = canvas(n)
    g = n / 16.0
    line(d, [(8 * g, 2.5 * g), (8 * g, 13.5 * g)], 2.2 * g)
    line(d, [(2.5 * g, 8 * g), (13.5 * g, 8 * g)], 2.2 * g)
    return finish(img, n)


def icon_battery_bolt(n):
    img, d, s = canvas(n)
    g = n / 16.0
    pts = [(9.5, 1), (4, 9), (7.5, 9), (6.5, 15), (12, 7), (8.5, 7), (9.5, 1)]
    d.polygon([(U(x * g), U(y * g)) for x, y in pts], fill=255)
    return finish(img, n)


def button(n, label, pill=False):
    img, d, s = canvas(n)
    if pill:
        rrect(d, (0, n * 0.12, n, n * 0.88), n * 0.38)
    else:
        circle(d, n / 2, n / 2, n / 2 - 0.05)
    if label in ("+", "-"):
        g = n / 16.0
        rrect(d, (4 * g, 7 * g, 12 * g, 9 * g), 0.6 * g, fill=0)
        if label == "+":
            rrect(d, (7 * g, 4 * g, 9 * g, 12 * g), 0.6 * g, fill=0)
    else:
        font = ImageFont.truetype(BOLD, int(n * SS * (0.62 if not pill else 0.6)))
        bbox = d.textbbox((0, 0), label, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        d.text(((s - tw) / 2 - bbox[0], (s - th) / 2 - bbox[1]), label, fill=0, font=font)
    return finish(img, n)


def icon_photo(n):
    img, d, s = canvas(n)
    g = n / 16.0
    rrect(d, (1 * g, 2.5 * g, 15 * g, 13.5 * g), 2 * g)
    rrect(d, (2.6 * g, 4.1 * g, 13.4 * g, 11.9 * g), 1 * g, fill=0)
    d.polygon([(U(3.2 * g), U(11.4 * g)), (U(6.6 * g), U(7 * g)), (U(9 * g), U(9.6 * g)), (U(10.6 * g), U(8.2 * g)),
               (U(12.8 * g), U(11.4 * g))], fill=255)
    circle(d, 10.8 * g, 6.3 * g, 1.3 * g)
    return finish(img, n)


def icon_medal(n):
    img, d, s = canvas(n)
    g = n / 16.0
    d.polygon([(U(4 * g), U(0.5 * g)), (U(7 * g), U(0.5 * g)), (U(9 * g), U(6 * g)), (U(6.5 * g), U(7 * g))], fill=255)
    d.polygon([(U(12 * g), U(0.5 * g)), (U(9 * g), U(0.5 * g)), (U(7 * g), U(6 * g)), (U(9.5 * g), U(7 * g))], fill=255)
    circle(d, 8 * g, 10.5 * g, 5.2 * g)
    circle(d, 8 * g, 10.5 * g, 3.2 * g, fill=0)
    circle(d, 8 * g, 10.5 * g, 1.8 * g)
    return finish(img, n)



def icon_bag(n):
    """Shopping bag (store)."""
    img, d, s = canvas(n)
    g = n / 16.0
    rrect(d, (2 * g, 5 * g, 14 * g, 15 * g), 2 * g)
    # handle
    d.arc([U(5 * g), U(1 * g), U(11 * g), U(9 * g)], 180, 360, fill=255, width=int(U(1.6 * g)))
    circle(d, 5.6 * g, 8 * g, 1 * g, fill=0)
    circle(d, 10.4 * g, 8 * g, 1 * g, fill=0)
    return finish(img, n)


ICONS = [
    ("ICON_GRID", icon_grid, 16), ("ICON_CART", icon_cart, 16), ("ICON_SEARCH", icon_search, 16),
    ("ICON_GEAR", icon_gear, 16), ("ICON_POWER", icon_power, 16), ("ICON_STAR", icon_star, 16),
    ("ICON_PLAY", icon_play, 16), ("ICON_SAVE", icon_save, 16), ("ICON_BOLT", icon_bolt, 16),
    ("ICON_NOTE", icon_note, 16), ("ICON_TRASH", icon_trash, 16), ("ICON_EYE_OFF", icon_eye_off, 16),
    ("ICON_RESTORE", icon_restore, 16), ("ICON_SORT", icon_sort, 16), ("ICON_SUN", icon_sun, 16),
    ("ICON_SPEAKER", icon_speaker, 16), ("ICON_GLOBE", icon_globe, 16), ("ICON_THEME", icon_theme, 16),
    ("ICON_USER", icon_user, 16), ("ICON_INFO", icon_info, 16), ("ICON_CHECK", icon_check, 16),
    ("ICON_HOME", icon_home, 16), ("ICON_CHEV_R", lambda n: icon_chev(n, True), 12),
    ("ICON_CHEV_L", lambda n: icon_chev(n, False), 12), ("ICON_GAMEPAD", icon_gamepad, 16),
    ("ICON_FOLDER", icon_folder, 16), ("ICON_CLOCK", icon_clock, 12), ("ICON_WARNING", icon_warning, 16),
    ("ICON_HEART", icon_heart, 12), ("ICON_BOLT_S", icon_battery_bolt, 9),
    ("ICON_GRID_L", icon_grid, 30), ("ICON_CART_L", icon_cart, 30), ("ICON_SEARCH_L", icon_search, 30),
    ("ICON_GEAR_L", icon_gear, 30), ("ICON_POWER_L", icon_power, 30), ("ICON_WARNING_L", icon_warning, 30),
    ("ICON_GAMEPAD_L", icon_gamepad, 30), ("ICON_STAR_L", icon_star, 30),
    ("ICON_LOCK", icon_lock, 16), ("ICON_LOCK_S", icon_lock, 11), ("ICON_CHART", icon_chart, 16),
    ("ICON_PALETTE", icon_palette, 16), ("ICON_USERS", icon_users, 16), ("ICON_PLUS_L", icon_plus, 24),
    ("ICON_USER_L", icon_user, 30), ("ICON_FOLDER_L", icon_folder, 30),
    ("ICON_PHOTO", icon_photo, 16), ("ICON_PHOTO_L", icon_photo, 30), ("ICON_MEDAL", icon_medal, 16),
    ("ICON_BAG", icon_bag, 16), ("ICON_BAG_L", icon_bag, 30), ("ICON_USERS_L", icon_users, 30),
    ("BTN_A", lambda n: button(n, "A"), 11), ("BTN_B", lambda n: button(n, "B"), 11),
    ("BTN_X", lambda n: button(n, "X"), 11), ("BTN_Y", lambda n: button(n, "Y"), 11),
    ("BTN_L", lambda n: button(n, "L", True), 11), ("BTN_R", lambda n: button(n, "R", True), 11),
    # START / SELECT are drawn as small labelled pills by ui_hint_bar (ui_widgets.c); these are the fallback
    ("BTN_START", lambda n: button(n, "S", True), 11), ("BTN_SELECT", lambda n: button(n, "s", True), 11),
]


def pack4(img):
    w, h = img.size
    px = img.load()
    out = bytearray()
    for y in range(h):
        row = [px[x, y] for x in range(w)]
        if w % 2:
            row.append(0)
        for i in range(0, len(row), 2):
            a = (row[i] * 15 + 127) // 255
            b = (row[i + 1] * 15 + 127) // 255
            out.append(a | (b << 4))
    return out


def main():
    h = ['// Generated by tools/gen_icons.py - do not edit by hand.', '#ifndef ICON_DATA_H', '#define ICON_DATA_H',
         '#include <stdint.h>', '', 'typedef enum {']
    for name, _, _ in ICONS:
        h.append(f'    {name},')
    h += ['    ICON_COUNT', '} IconId;', '', 'typedef struct {', '    uint8_t size;', '    uint8_t _pad[3];',
          '    uint32_t offset;', '} IconInfo;', '', 'extern const IconInfo g_icons[ICON_COUNT];',
          'extern const uint8_t g_icon_bitmap[];', '', '#endif', '']
    c = ['// Generated by tools/gen_icons.py - do not edit by hand.', '#include "icon_data.h"', '']
    bitmap = bytearray()
    infos = []
    preview = Image.new("L", (sum(sz + 4 for _, _, sz in ICONS), 34), 0)
    px = 2
    for name, fn, size in ICONS:
        img = fn(size)
        infos.append(f'    {{{size}, {{0, 0, 0}}, {len(bitmap)}}}, // {name}')
        bitmap += pack4(img)
        preview.paste(img, (px, 2))
        px += size + 4
    c.append('const IconInfo g_icons[ICON_COUNT] = {')
    c += infos
    c.append('};')
    c.append(f'const uint8_t g_icon_bitmap[{len(bitmap)}] = {{')
    for i in range(0, len(bitmap), 24):
        c.append('    ' + ','.join(str(b) for b in bitmap[i:i + 24]) + ',')
    c.append('};')
    c.append('')
    with open(os.path.join(ROOT, "include", "icon_data.h"), "w") as f:
        f.write("\n".join(h))
    with open(os.path.join(ROOT, "source", "icon_data.c"), "w") as f:
        f.write("\n".join(c))
    preview.resize((preview.width * 3, preview.height * 3), Image.NEAREST).save("/tmp/icons_preview.png")
    print(f"{len(ICONS)} icons, {len(bitmap)} bytes")


if __name__ == "__main__":
    main()

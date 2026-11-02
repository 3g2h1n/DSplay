"""Box art for the HOME Menu (sd:/_hyper/covers).

Lookup order on the console (source/covers.c): <GAMECODE>.png for DS games, then
<filename>.png / <name>.png, in _hyper/covers, _nds/TWiLightMenu/boxart, _hyper/boxart.

Sources used here:
  * Nintendo DS  - GameTDB cover images by game code (art.gametdb.com)
  * other systems - libretro-thumbnails "Named_Boxarts" by No-Intro file name
Covers are re-encoded as RGB PNG, at most 128 px, which the console decodes quickly.
"""

import os
import struct
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

from . import STUDIO_VERSION
from .pngcodec import to_console_png
from .systems import console_filename, iter_library

LIBRETRO_SYSTEMS = {
    "nds": "Nintendo - Nintendo DS",
    "gba": "Nintendo - Game Boy Advance",
    "gb": "Nintendo - Game Boy",
    "gbc": "Nintendo - Game Boy Color",
    "nes": "Nintendo - Nintendo Entertainment System",
    "snes": "Nintendo - Super Nintendo Entertainment System",
    "md": "Sega - Mega Drive - Genesis",
    "sms": "Sega - Master System - Mark III",
    "gg": "Sega - Game Gear",
}

GAMETDB_REGION = {"E": "US", "J": "JA", "K": "KO", "P": "EN", "D": "DE", "F": "FR", "S": "ES", "I": "IT",
                  "H": "NL", "U": "AU", "X": "EN", "Y": "EN", "Z": "EN"}

UA = {"User-Agent": f"DSplayStudio/{STUDIO_VERSION}"}


def _get(url, timeout=15):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=timeout) as r:  # noqa: S310 (fixed https hosts)
        if r.status != 200:
            return None
        data = r.read(4 * 1024 * 1024)
        return data if data[:8] == b"\x89PNG\r\n\x1a\n" else None


def _nds_code(path):
    try:
        with open(path, "rb") as f:
            f.seek(0x0C)
            code = f.read(4).decode("ascii", "replace")
        return code if code.isalnum() and len(code) == 4 else ""
    except OSError:
        return ""


def _libretro_name(stem):
    # libretro-thumbnails replaces these characters with '_'
    for ch in '&*/:`<>?\\|"':
        stem = stem.replace(ch, "_")
    return stem


def candidate_urls(path, sysid):
    urls = []
    if sysid == "nds":
        code = _nds_code(path)
        if code:
            regions = [GAMETDB_REGION.get(code[3], "EN"), "US", "EN", "JA"]
            seen = []
            for reg in regions:
                if reg not in seen:
                    seen.append(reg)
                    urls.append(f"https://art.gametdb.com/ds/coverS/{reg}/{code}.png")
    system = LIBRETRO_SYSTEMS.get(sysid)
    if system:
        name = _libretro_name(os.path.splitext(os.path.basename(path))[0]) + ".png"
        urls.append("https://thumbnails.libretro.com/" + urllib.parse.quote(f"{system}/Named_Boxarts/{name}"))
        repo = system.replace(" ", "_")
        urls.append(f"https://raw.githubusercontent.com/libretro-thumbnails/{urllib.parse.quote(repo)}/master/"
                    + urllib.parse.quote(f"Named_Boxarts/{name}"))
    return urls


def cover_name(path, sysid):
    if sysid == "nds":
        code = _nds_code(path)
        if code and code != "####":
            return code + ".png"
    return console_filename(os.path.basename(path)) + ".png"


def existing_cover(sd, path, sysid):
    names = {cover_name(path, sysid).lower()}
    stem = os.path.splitext(console_filename(os.path.basename(path)))[0]
    names.add(stem.lower() + ".png")
    names.add(stem.lower() + ".bmp")
    for d in ("_hyper/covers", "_nds/TWiLightMenu/boxart", "_hyper/boxart"):
        folder = os.path.join(sd, *d.split("/"))
        try:
            present = {n.lower() for n in os.listdir(folder)}
        except OSError:
            continue
        if names & present:
            return True
    return False


def fetch_one(sd, path, sysid, force=False):
    if not force and existing_cover(sd, path, sysid):
        return "exists"
    for url in candidate_urls(path, sysid):
        try:
            data = _get(url)
        except Exception:  # noqa: BLE001 - network errors, 404s
            data = None
        if not data:
            continue
        try:
            png = to_console_png(data)
        except Exception:  # noqa: BLE001
            continue
        out_dir = os.path.join(sd, "_hyper", "covers")
        os.makedirs(out_dir, exist_ok=True)
        dst = os.path.join(out_dir, cover_name(path, sysid))
        with open(dst + ".tmp", "wb") as f:
            f.write(png)
        os.replace(dst + ".tmp", dst)
        return "downloaded"
    return "missing"


# ---- game cards (Slot-1): full names and box art --------------------------------
TITLES_URL = "https://www.gametdb.com/dstdb.zip?LANG=EN"
TITLES_MAX_AGE = 30 * 24 * 3600


def parse_titles(txt):
    """GameTDB dstdb.txt ("CODE = Title" lines) -> sorted "CODE<TAB>Title" text for the console."""
    rows = {}
    for line in txt.splitlines():
        if " = " not in line:
            continue
        code, title = line.split(" = ", 1)
        code, title = code.strip(), title.strip()
        if len(code) == 4 and code.isalnum() and title and code.upper() != "TITL":
            rows[code.upper()] = title.replace("\t", " ")[:60]
    return "".join(f"{c}\t{t}\n" for c, t in sorted(rows.items()))


def update_titles(sd, force=False):
    """Writes _hyper/titles.txt so the console can show "Mario Party DS" instead of the
    12-letter card header name. Refreshed once a month."""
    import io
    import time
    import zipfile
    dst = os.path.join(sd, "_hyper", "titles.txt")
    try:
        if not force and os.path.isfile(dst) and time.time() - os.path.getmtime(dst) < TITLES_MAX_AGE:
            return "exists"
        req = urllib.request.Request(TITLES_URL, headers=UA)
        with urllib.request.urlopen(req, timeout=30) as r:  # noqa: S310 (fixed https host)
            data = r.read(32 * 1024 * 1024)
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            name = next(n for n in z.namelist() if n.lower().endswith(".txt"))
            txt = z.read(name).decode("utf-8", "replace")
        out = parse_titles(txt)
        if len(out) < 1000:
            return "missing"
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst + ".tmp", "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
        os.replace(dst + ".tmp", dst)
        return "downloaded"
    except Exception:  # noqa: BLE001 - offline, site changed: the console falls back to the header name
        return "missing"


def seen_cards(sd):
    """Codes of game cards the console has seen (_hyper/slot1_seen.txt)."""
    codes = []
    try:
        with open(os.path.join(sd, "_hyper", "slot1_seen.txt"), encoding="utf-8", errors="replace") as f:
            for line in f:
                code = line[:4]
                if len(code) == 4 and code.isalnum() and code not in codes:
                    codes.append(code)
    except OSError:
        pass
    return codes


def fetch_card_cover(sd, code, force=False):
    out_dir = os.path.join(sd, "_hyper", "covers")
    dst = os.path.join(out_dir, code + ".png")
    if not force and os.path.isfile(dst):
        return "exists"
    regions = []
    for reg in (GAMETDB_REGION.get(code[3], "EN"), "US", "EN", "JA"):
        if reg not in regions:
            regions.append(reg)
    for reg in regions:
        try:
            data = _get(f"https://art.gametdb.com/ds/coverS/{reg}/{code}.png")
            png = to_console_png(data) if data else None
        except Exception:  # noqa: BLE001
            png = None
        if png:
            os.makedirs(out_dir, exist_ok=True)
            with open(dst + ".tmp", "wb") as f:
                f.write(png)
            os.replace(dst + ".tmp", dst)
            return "downloaded"
    return "missing"


def sync(sd, force=False, progress=None, workers=4):
    games = list(iter_library(sd))
    counts = {"total": len(games), "downloaded": 0, "exists": 0, "missing": 0, "missing_titles": []}
    counts["titles"] = update_titles(sd, force)
    counts["cards"] = 0
    for code in seen_cards(sd):
        if fetch_card_cover(sd, code, force) == "downloaded":
            counts["cards"] += 1
    done = 0

    def job(item):
        path, sysid = item
        return path, fetch_one(sd, path, sysid, force)

    with ThreadPoolExecutor(max_workers=workers) as pool:
        for path, res in pool.map(job, games):
            done += 1
            counts[res] += 1
            if res == "missing" and len(counts["missing_titles"]) < 50:
                counts["missing_titles"].append(os.path.basename(path))
            if progress:
                progress(done, len(games), os.path.basename(path))
    try:
        counts["pack"] = build_pack(sd)          # pre-made covers: the console reads them directly
    except OSError:
        counts["pack"] = 0
    return counts


def count(sd):
    folder = os.path.join(sd, "_hyper", "covers")
    try:
        return sum(1 for n in os.listdir(folder) if n.lower().endswith((".png", ".bmp")) and not n.startswith((".", "_")))
    except OSError:
        return 0


# ---- pre-made covers for the console (source/covers.c, include/covers.h) ------------------
PACK_NAME = "_covers.pak"
PACK_MAGIC = 0x50564348          # "HCVP"
PACK_VERSION = 2                 # 2: whole picture, transparent border
PACK_DIRS = ("_hyper/covers", "_nds/TWiLightMenu/boxart", "_hyper/boxart")
COVER = 96
_ENTRY = struct.Struct("<IB3xIII")


def name_hash(name):
    """FNV-1a of the file name with A-Z lowered, the same as hash_name() on the console."""
    h = 2166136261
    for b in name.encode("utf-8"):
        if 65 <= b <= 90:
            b += 32
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def _read_bmp(data):
    """24/32-bit (and 16-bit 555) BMP -> (w, h, rgb) top-down, like decode_bmp() on the console."""
    if data[:2] != b"BM" or len(data) < 54:
        raise ValueError("not a bmp")
    off = struct.unpack_from("<I", data, 10)[0]
    w, hh = struct.unpack_from("<ii", data, 18)
    bpp, comp = struct.unpack_from("<H", data, 28)[0], struct.unpack_from("<I", data, 30)[0]
    h = abs(hh)
    if not (8 <= w <= 1024 and 8 <= h <= 1024) or bpp not in (16, 24, 32) or comp not in (0, 3):
        raise ValueError("unsupported bmp")
    stride = ((w * (bpp // 8)) + 3) & ~3
    out = bytearray(w * h * 3)
    for y in range(h):
        row = (h - 1 - y) if hh > 0 else y
        base = off + row * stride
        for x in range(w):
            p = base + x * (bpp // 8)
            o = (y * w + x) * 3
            if bpp == 16:
                v = data[p] | (data[p + 1] << 8)
                out[o], out[o + 1], out[o + 2] = ((v >> 10) & 31) << 3, ((v >> 5) & 31) << 3, (v & 31) << 3
            else:
                out[o], out[o + 1], out[o + 2] = data[p + 2], data[p + 1], data[p]
    return w, h, bytes(out)


def fit_cover(w, h, rgb):
    """96x96 RGB555 picture, the same fit and box filter as fit_cover() in source/covers.c:
    the whole picture with its aspect ratio, centered, the border transparent (0)."""
    dw = dh = COVER
    if w >= h:
        dh = (h * COVER + w // 2) // w
    else:
        dw = (w * COVER + h // 2) // h
    dw, dh = max(dw, 1), max(dh, 1)
    px0, py0 = (COVER - dw) // 2, (COVER - dh) // 2
    r5 = [v >> 3 for v in rgb]
    out = bytearray(COVER * COVER * 2)
    for y in range(dh):
        sy0 = (y * h) // dh
        sy1 = max(sy0 + 1, ((y + 1) * h) // dh)
        for x in range(dw):
            sx0 = (x * w) // dw
            sx1 = max(sx0 + 1, ((x + 1) * w) // dw)
            r = g = b = n = 0
            for yy in range(sy0, sy1):
                base = (yy * w) * 3
                for xx in range(sx0, sx1):
                    i = base + xx * 3
                    r += r5[i]
                    g += r5[i + 1]
                    b += r5[i + 2]
                    n += 1
            struct.pack_into("<H", out, ((py0 + y) * COVER + px0 + x) * 2, (r // n) | ((g // n) << 5) | ((b // n) << 10) | 0x8000)
    return bytes(out)


def _cover_pixels(path):
    from .pngcodec import decode
    with open(path, "rb") as f:
        data = f.read()
    w, h, rgb = _read_bmp(data) if path.lower().endswith(".bmp") else decode(data)
    if not (8 <= w <= 1024 and 8 <= h <= 1024):
        raise ValueError("size")
    return fit_cover(w, h, rgb)


def _read_pack(path):
    """{(dir, hash, size, mtime): pixels} of an existing pack (unchanged covers are reused)."""
    old = {}
    try:
        with open(path, "rb") as f:
            magic, ver, n, esz = struct.unpack("<IIII", f.read(16))
            if magic != PACK_MAGIC or ver != PACK_VERSION or esz != _ENTRY.size:
                return old
            entries = [_ENTRY.unpack(f.read(_ENTRY.size)) for _ in range(n)]
            for h, d, size, mtime, off in entries:
                f.seek(off)
                px = f.read(COVER * COVER * 2)
                if len(px) == COVER * COVER * 2:
                    old[(d, h, size, mtime)] = px
    except (OSError, struct.error):
        pass
    return old


def build_pack(sd, progress=None):
    """Writes sd:/_hyper/covers/_covers.pak with every cover of the three folders, scaled for the
    console. Returns the number of covers in it."""
    out_dir = os.path.join(sd, "_hyper", "covers")
    dst = os.path.join(out_dir, PACK_NAME)
    old = _read_pack(dst)
    files = []
    for d, rel in enumerate(PACK_DIRS):
        folder = os.path.join(sd, *rel.split("/"))
        try:
            names = sorted(os.listdir(folder))
        except OSError:
            continue
        for n in names:
            if n.startswith((".", "_")) or not n.lower().endswith((".png", ".bmp")):
                continue
            p = os.path.join(folder, n)
            if os.path.isfile(p):
                files.append((d, n, p))
    entries, blobs = [], []
    for i, (d, n, p) in enumerate(files[:8192]):
        st = os.stat(p)
        key = (d, name_hash(n), st.st_size & 0xFFFFFFFF, int(st.st_mtime) & 0xFFFFFFFF)
        px = old.get(key)
        if px is None:
            try:
                px = _cover_pixels(p)
            except Exception:  # noqa: BLE001 - a broken picture is decoded (and skipped) on the console
                continue
        entries.append(key)
        blobs.append(px)
        if progress:
            progress(i + 1, len(files), n)
    order = sorted(range(len(entries)), key=lambda k: (entries[k][1], entries[k][0]))
    if not order:
        if os.path.isfile(dst):
            os.remove(dst)
        return 0
    os.makedirs(out_dir, exist_ok=True)
    head = 16 + _ENTRY.size * len(order)
    with open(dst + ".tmp", "wb") as f:
        f.write(struct.pack("<IIII", PACK_MAGIC, PACK_VERSION, len(order), _ENTRY.size))
        for slot, k in enumerate(order):
            d, h, size, mtime = entries[k]
            f.write(_ENTRY.pack(h, d, size, mtime, head + slot * COVER * COVER * 2))
        for k in order:
            f.write(blobs[k])
    os.replace(dst + ".tmp", dst)
    return len(order)

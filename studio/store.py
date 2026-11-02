"""Homebrew store: free DS(i) homebrew from Universal-DB (db.universal-team.net, the database
Universal-Updater uses on the 3DS). Studio downloads the catalog and the files; the console shows
the store itself (source/store.c) and asks Studio over the controller connection.

On the card
    roms/NDS/Homebrew/<file>.nds          installed programs (the menu lists them like any game)
    _hyper/store/catalog.bin              catalog for the console (see build_catalog_bin)
    _hyper/store/installed.txt            "slug|version|file1,file2" per installed program
"""

import json
import os
import shutil
import struct
import tempfile
import threading
import time
import urllib.request

from . import STUDIO_VERSION, pngcodec, roms

CATALOG_URLS = (
    "https://db.universal-team.net/data/full.json",
    "https://raw.githubusercontent.com/Universal-Team/db/master/docs/data/full.json",
)
DEST = ("roms", "NDS", "Homebrew")
STORE_DIR = ("_hyper", "store")
MAX_AGE = 6 * 3600
MAX_FILE = 256 * 1024 * 1024
# installed by DSplay itself (runtime package), need their own installer, or change the system
# memory / flash carts (not something to install with one click from a menu)
SKIP = {"twilightmenu", "twilight-menu", "nds-bootstrap", "ds-forwarder-packs", "gbarunner2",
        "hiyacfw", "safe-nand-manager", "ntm", "cart-flasher", "ndsforwarder-dsi"}
CATEGORIES = {"game": 1, "utility": 2, "save-tool": 2, "emulator": 3, "app": 4, "media": 4}
UA = {"User-Agent": "DSplayStudio/" + STUDIO_VERSION}

_lock = threading.Lock()
_mem = {"t": 0, "items": None}


def cache_dir():
    d = os.path.join(os.path.expanduser("~"), ".dsplay", "store")
    os.makedirs(d, exist_ok=True)
    return d


def _get(url, timeout=30, limit=MAX_FILE, progress=None):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=timeout) as r:  # noqa: S310 (https catalog / release hosts)
        total = int(r.headers.get("Content-Length") or 0)
        buf = bytearray()
        while True:
            chunk = r.read(256 * 1024)
            if not chunk:
                break
            buf += chunk
            if len(buf) > limit:
                raise ValueError("file too big")
            if progress:
                progress(len(buf), total)
        return bytes(buf)


def _pick_download(downloads):
    """The file to install: .nds before .dsi before archives; releases before [git]/[prerelease]."""
    cands = []
    for name, d in (downloads or {}).items():
        ext = os.path.splitext(name)[1].lower()
        if ext not in (".nds", ".dsi", ".zip", ".7z") or not isinstance(d, dict) or not d.get("url"):
            continue
        pre = "[" in name
        rank = (pre, {".nds": 0, ".dsi": 1, ".zip": 2, ".7z": 3}[ext], len(name))
        cands.append((rank, name, d))
    if not cands:
        return None
    _, name, d = min(cands)
    clean = name.split("] ", 1)[-1] if name.startswith("[") else name
    return {"name": clean, "url": d["url"], "size": int(d.get("size") or 0),
            "archive": os.path.splitext(name)[1].lower() in (".zip", ".7z")}


def _ts(s):
    try:
        return int(time.mktime(time.strptime((s or "")[:19], "%Y-%m-%dT%H:%M:%S")))
    except ValueError:
        return 0


def normalize(raw):
    out = []
    for e in raw:
        systems = [s.upper() for s in e.get("systems") or []]
        if "DS" not in systems and "NDS" not in systems:
            continue
        slug = e.get("slug") or ""
        dl = _pick_download(e.get("downloads"))
        if not slug or slug in SKIP or not dl:
            continue
        cats = e.get("categories") or []
        cat = next((CATEGORIES[c] for c in cats if c in CATEGORIES), 4)
        out.append({
            "slug": slug, "title": e.get("title") or slug, "author": e.get("author") or "",
            "description": (e.get("description") or "").strip(), "category": cat, "categories": cats,
            "stars": int(e.get("stars") or 0), "updated": _ts(e.get("updated")), "version": e.get("version") or "",
            "icon": e.get("icon_static") or e.get("icon") or "", "license": e.get("license_name") or e.get("license") or "",
            "screenshots": [s.get("url") for s in (e.get("screenshots") or []) if isinstance(s, dict) and s.get("url")][:4],
            "file": dl["name"], "url": dl["url"], "size": dl["size"], "archive": dl["archive"],
            "website": e.get("website") or (("https://github.com/" + e["github"]) if e.get("github") else ""),
        })
    out.sort(key=lambda x: (-x["stars"], x["title"].lower()))
    return out


def load_catalog(force=False, urls=CATALOG_URLS):
    """The DS part of Universal-DB; cached in memory and in ~/.dsplay/store for MAX_AGE."""
    with _lock:
        now = time.time()
        if not force and _mem["items"] is not None and now - _mem["t"] < MAX_AGE:
            return _mem["items"]
        path = os.path.join(cache_dir(), "full.json")
        raw = None
        if not force and os.path.isfile(path) and now - os.path.getmtime(path) < MAX_AGE:
            try:
                with open(path, encoding="utf-8") as f:
                    raw = json.load(f)
            except (OSError, ValueError):
                raw = None
        if raw is None:
            last = None
            for url in urls:
                try:
                    data = _get(url, timeout=40, limit=64 * 1024 * 1024)
                    raw = json.loads(data.decode("utf-8"))
                    with open(path + ".tmp", "wb") as f:
                        f.write(data)
                    os.replace(path + ".tmp", path)
                    break
                except Exception as e:  # noqa: BLE001 - try the next address, then the old copy
                    last = e
            if raw is None and os.path.isfile(path):
                with open(path, encoding="utf-8") as f:
                    raw = json.load(f)
            if raw is None:
                raise ConnectionError("katalog indirilemedi: %s" % last)
        _mem["items"] = normalize(raw)
        _mem["t"] = now
        return _mem["items"]


def find(slug):
    for it in load_catalog():
        if it["slug"] == slug:
            return it
    return None


# ------------------------------------------------------------------ icons
def icon_bytes(item):
    """Original icon file (PNG/GIF), cached."""
    url = item.get("icon")
    if not url:
        return None
    p = os.path.join(cache_dir(), "icon_" + item["slug"] + os.path.splitext(url.split("?")[0])[1][:5])
    if os.path.isfile(p):
        with open(p, "rb") as f:
            return f.read()
    try:
        data = _get(url, timeout=20, limit=2 * 1024 * 1024)
    except Exception:  # noqa: BLE001
        return None
    with open(p, "wb") as f:
        f.write(data)
    return data


def icon_rgb555(item, size=32):
    """size x size RGB555 (bit 15 set) for the console; None when there is no usable icon."""
    data = icon_bytes(item)
    if not data or not data.startswith(b"\x89PNG"):
        return None
    try:
        w, h, rgb = pngcodec.decode(data)
    except Exception:  # noqa: BLE001
        return None
    # fit into size x size on a dark tile (icons are mostly 32 x 32 already)
    if (w, h) != (size, size):
        if max(w, h) > size:
            w, h, rgb = pngcodec.resize_fit(w, h, rgb, size)
        canvas = bytearray(b"\x28" * (size * size * 3))
        ox, oy = (size - w) // 2, (size - h) // 2
        for y in range(h):
            canvas[((oy + y) * size + ox) * 3:((oy + y) * size + ox + w) * 3] = rgb[y * w * 3:(y + 1) * w * 3]
        rgb = bytes(canvas)
    out = bytearray()
    for i in range(0, size * size * 3, 3):
        r, g, b = rgb[i] >> 3, rgb[i + 1] >> 3, rgb[i + 2] >> 3
        out += struct.pack("<H", 0x8000 | r | (g << 5) | (b << 10))
    return bytes(out)


# ------------------------------------------------------------------ catalog for the console
ENTRY = struct.Struct("<40s48s32s16s200s48sBBHII")   # + 2048 bytes icon
ICON_BYTES = 32 * 32 * 2


def _fit(s, n):
    b = (s or "").encode("utf-8")[:n - 1]
    while b and (b[-1] & 0xC0) == 0x80:      # do not cut a character in half
        b = b[:-1]
    if b and b[-1] >= 0xC0:
        b = b[:-1]
    return b


def build_catalog_bin(items, with_icons=True):
    """HSTR v1: u32 magic, u32 version, u32 count, u32 entry size, then the entries."""
    body = bytearray()
    n = 0
    for it in items[:255]:
        icon = icon_rgb555(it) if with_icons else None
        flags = 1 if icon else 0
        body += ENTRY.pack(_fit(it["slug"], 40), _fit(it["title"], 48), _fit(it["author"], 32), _fit(it["version"], 16),
                           _fit(it["description"], 200), _fit(it["file"], 48), it["category"], flags,
                           min(it["stars"], 65535), it["size"], it["updated"])
        body += icon or b"\0" * ICON_BYTES
        n += 1
    return struct.pack("<4sIII", b"HSTR", 1, n, ENTRY.size + ICON_BYTES) + bytes(body)


# ------------------------------------------------------------------ installing
def _stage(item, progress=None):
    """Downloads the program; returns (tmpdir, [(name, path)]) of the .nds/.dsi files to install."""
    tmp = tempfile.mkdtemp(prefix="dsplay_store_")
    data = _get(item["url"], timeout=60, progress=(lambda d, t: progress({"done": d, "total": t or item["size"] or d,
                                                                          "name": item["file"]})) if progress else None)
    src = os.path.join(tmp, "download" + os.path.splitext(item["url"].split("?")[0])[1])
    with open(src, "wb") as f:
        f.write(data)
    files = []
    if item["archive"]:
        out = os.path.join(tmp, "x")
        os.makedirs(out)
        if not roms._extract_archive(src, out):           # noqa: SLF001 - shared helper
            raise ValueError("arşiv açılamadı")
        for root, _d, fs in os.walk(out):
            for f in sorted(fs):
                if os.path.splitext(f)[1].lower() in (".nds", ".dsi") and not f.startswith("."):
                    files.append((roms._sanitize(f), os.path.join(root, f)))   # noqa: SLF001
    else:
        files.append((roms._sanitize(item["file"]), src))                    # noqa: SLF001
    if not files:
        raise ValueError("içinde .nds yok")
    return tmp, files


def read_installed(text):
    out = {}
    for line in (text or "").splitlines():
        parts = line.strip().split("|")
        if len(parts) >= 3 and parts[0]:
            out[parts[0]] = {"version": parts[1], "files": [p for p in parts[2].split(",") if p]}
    return out


def write_installed(d):
    return "".join("%s|%s|%s\n" % (k, v["version"], ",".join(v["files"])) for k, v in sorted(d.items()))


def installed_on_card(sd):
    p = os.path.join(sd, *STORE_DIR, "installed.txt")
    try:
        with open(p, encoding="utf-8") as f:
            d = read_installed(f.read())
    except OSError:
        d = {}
    # a program the user deleted from the card is not installed any more
    return {k: v for k, v in d.items() if any(os.path.isfile(os.path.join(sd, *DEST, n)) for n in v["files"])}


def _save_installed(sd, d):
    folder = os.path.join(sd, *STORE_DIR)
    os.makedirs(folder, exist_ok=True)
    tmp = os.path.join(folder, "installed.txt.tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(write_installed(d))
    os.replace(tmp, os.path.join(folder, "installed.txt"))


def install_to_card(sd, slug, progress=None):
    item = find(slug)
    if not item:
        return {"ok": False, "reason": "unknown"}
    tmp, files = _stage(item, progress)
    try:
        dest = os.path.join(sd, *DEST)
        os.makedirs(dest, exist_ok=True)
        need = sum(os.path.getsize(p) for _n, p in files)
        if shutil.disk_usage(sd).free < need + 1024 * 1024:
            return {"ok": False, "reason": "space"}
        for name, p in files:
            shutil.copyfile(p, os.path.join(dest, name + ".tmp"))
            os.replace(os.path.join(dest, name + ".tmp"), os.path.join(dest, name))
        d = installed_on_card(sd)
        d[slug] = {"version": item["version"], "files": [n for n, _p in files]}
        _save_installed(sd, d)
        return {"ok": True, "files": [n for n, _p in files], "title": item["title"]}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def remove_from_card(sd, slug):
    d = installed_on_card(sd)
    v = d.pop(slug, None)
    if v:
        for n in v["files"]:
            p = os.path.join(sd, *DEST, n)
            if os.path.isfile(p):
                os.remove(p)
        _save_installed(sd, d)
    return {"ok": bool(v)}


# ------------------------------------------------------------------ over Wi-Fi (console asks)
def send_catalog_wireless(progress=None):
    from . import wireless
    host, token, _ = wireless._connected()                     # noqa: SLF001
    items = load_catalog()
    blob = build_catalog_bin(items)
    fd, tmp = tempfile.mkstemp(suffix=".bin")
    with os.fdopen(fd, "wb") as f:
        f.write(blob)
    try:
        with wireless.Client(host, token) as c:
            st = c.put("_hyper/store/catalog.bin", tmp, overwrite=True, progress=(lambda s, t: progress({"done": s, "total": t})) if progress else None)
            c.done()
        return {"ok": st in ("copied", "exists"), "count": len(items), "status": st}
    finally:
        os.remove(tmp)


def install_wireless(slug, progress=None):
    from . import wireless
    host, token, _ = wireless._connected()                     # noqa: SLF001
    item = find(slug)
    if not item:
        return {"ok": False, "reason": "unknown"}
    tmp, files = _stage(item, progress)
    try:
        with wireless.Client(host, token) as c:
            sts = [c.put("/".join(DEST + (name,)), p, overwrite=True) for name, p in files]
            try:
                d = read_installed(c.get("_hyper/store/installed.txt").decode("utf-8", "replace"))
            except Exception:  # noqa: BLE001 - not there yet
                d = {}
            d[slug] = {"version": item["version"], "files": [n for n, _p in files]}
            itxt = os.path.join(tmp, "installed.txt")
            with open(itxt, "w", encoding="utf-8", newline="\n") as f:
                f.write(write_installed(d))
            c.put("_hyper/store/installed.txt", itxt, overwrite=True)
            c.done()
        ok = all(s in ("copied", "exists") for s in sts)
        return {"ok": ok, "status": sts, "reason": None if ok else (sts[0] if sts else "error")}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# console request (pad.T_STORE): op 1 = send the catalog, op 2 = install <slug>
RES_OK, RES_FAIL, RES_OFFLINE, RES_SPACE, RES_UNKNOWN = 0, 1, 2, 3, 4


def handle_console(service, op, slug):
    try:
        if op == 1:
            r = send_catalog_wireless()
        elif op == 2:
            r = install_wireless(slug)
        else:
            return
        st = RES_OK if r.get("ok") else (RES_SPACE if r.get("reason") == "no-space" else
                                          RES_UNKNOWN if r.get("reason") == "unknown" else RES_FAIL)
    except ConnectionError:
        st = RES_OFFLINE
    except Exception:  # noqa: BLE001
        import traceback
        traceback.print_exc()
        st = RES_FAIL
    service.send_store_result(op, st, slug)


def public(items, installed):
    return [dict(it, installed=it["slug"] in installed,
                 installed_version=(installed.get(it["slug"]) or {}).get("version")) for it in items]

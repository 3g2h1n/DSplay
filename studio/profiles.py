"""Console profiles (up to 4): see include/profiles.h for the on-card layout.

    _hyper/profiles/profiles.bin   last profile, login screen on/off, resume flag
    _hyper/profiles/pN/            profile.bin, settings.bin, avatar.bin, wallpaper.bin,
                                   playtime.bin, favorites.txt, hidden.txt, saves/, backups/
"""

import datetime
import os
import shutil
import struct
import time
import zipfile

MAX_PROFILES = 4
PROFILE_MAGIC = 0x31465250  # "PRF1"
PROFILES_MAGIC = 0x31535250  # "PRS1"
# magic, name[20], color, pin_len, pin[8], reserved[2], created, last_title[48], last_ts
PROFILE_FMT = "<I20sBB8s2sI48sI"
GLOBAL_FMT = "<IBBB9x"  # magic, last, login, resume
PROFILE_COLORS = 8
AVATAR_SIZE = 64
WP_W, WP_H = 256, 192
LEGACY_FILES = ("settings.bin", "favorites.txt", "hidden.txt", "playtime.bin", "session.bin", "avatar.bin", "daylog.bin")
# PlaytimeDB: magic, count, 256 x (filename[64], launch_count, total_minutes, last_played_ts)
PLAYTIME_MAGIC = 0x34594C50
PLAYTIME_ENTRY = "<64sIII"


def _root(sd):
    return os.path.join(sd, "_hyper", "profiles")


def profile_dir(sd, slot):
    return os.path.join(_root(sd), "p%d" % slot)


def _cstr(b):
    return b.split(b"\0")[0].decode("utf-8", "ignore")


def _clip_utf8(text, n):
    raw = (text or "").strip().encode("utf-8")[:n]
    while raw:
        try:
            raw.decode("utf-8")
            break
        except UnicodeDecodeError:
            raw = raw[:-1]
    return raw


def read_profile(sd, slot):
    p = os.path.join(profile_dir(sd, slot), "profile.bin")
    try:
        with open(p, "rb") as f:
            raw = f.read()
    except OSError:
        return None
    raw = raw[:struct.calcsize(PROFILE_FMT)].ljust(struct.calcsize(PROFILE_FMT), b"\0")
    v = struct.unpack(PROFILE_FMT, raw)
    if v[0] != PROFILE_MAGIC:
        return None
    return {"slot": slot, "name": _cstr(v[1]) or "Oyuncu", "color": v[2] if v[2] < PROFILE_COLORS else 0,
            "locked": v[3] > 0, "pin_len": v[3], "pin": v[4], "created": v[6],
            "last_title": _cstr(v[7]), "last_ts": v[8]}


def _write_profile(sd, slot, info):
    folder = profile_dir(sd, slot)
    os.makedirs(folder, exist_ok=True)
    data = struct.pack(PROFILE_FMT, PROFILE_MAGIC, _clip_utf8(info["name"], 19), int(info.get("color", 0)) % PROFILE_COLORS,
                       int(info.get("pin_len", 0)), bytes(info.get("pin", b"")).ljust(8, b"\0")[:8], b"\0\0",
                       int(info.get("created", 0)), _clip_utf8(info.get("last_title", ""), 47), int(info.get("last_ts", 0)))
    tmp = os.path.join(folder, "profile.bin.tmp")
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, os.path.join(folder, "profile.bin"))


def read_global(sd):
    try:
        with open(os.path.join(_root(sd), "profiles.bin"), "rb") as f:
            v = struct.unpack(GLOBAL_FMT, f.read(struct.calcsize(GLOBAL_FMT)))
        if v[0] == PROFILES_MAGIC:
            return {"last": v[1], "login": bool(v[2]), "resume": v[3]}
    except (OSError, struct.error):
        pass
    return None


def _write_global(sd, g):
    os.makedirs(_root(sd), exist_ok=True)
    with open(os.path.join(_root(sd), "profiles.bin"), "wb") as f:
        f.write(struct.pack(GLOBAL_FMT, PROFILES_MAGIC, int(g.get("last", 0)), 1 if g.get("login", True) else 0, int(g.get("resume", 0))))


def _settings_name(path):
    try:
        with open(path, "rb") as f:
            raw = f.read(40)
        if len(raw) == 40 and struct.unpack("<I", raw[:4])[0] == 0x35475453:
            return _cstr(raw[20:40])
    except OSError:
        pass
    return ""


def _move(src, dst):
    """Moves a file; an unreadable (damaged) file is left where it is. Returns False then."""
    try:
        os.replace(src, dst)
        return True
    except OSError:
        pass
    try:
        shutil.copyfile(src, dst)
    except OSError:
        try:
            if os.path.exists(dst):
                os.remove(dst)
        except OSError:
            pass
        return False
    try:
        os.remove(src)
    except OSError:
        pass
    return True


# files that could not be moved because the card's file system has an error (shown by Studio)
damaged_files = []


def ensure_profiles(sd):
    """Same as the console at boot: the files from before profiles become profile 1.
    A damaged file is skipped (the console then starts that list empty) instead of stopping."""
    if any(read_profile(sd, s) for s in range(MAX_PROFILES)):
        if read_global(sd) is None:
            _write_global(sd, {"last": next(s for s in range(MAX_PROFILES) if read_profile(sd, s)), "login": True})
        return
    hyper = os.path.join(sd, "_hyper")
    folder = profile_dir(sd, 0)
    os.makedirs(folder, exist_ok=True)
    for name in LEGACY_FILES:
        src = os.path.join(hyper, name)
        try:
            present = os.path.isfile(src)
        except OSError:
            present = False
        if present and not _move(src, os.path.join(folder, name)):
            rel = "_hyper/" + name
            if rel not in damaged_files:
                damaged_files.append(rel)
    name = _settings_name(os.path.join(folder, "settings.bin")) or "Oyuncu"
    _write_profile(sd, 0, {"name": name, "color": 0, "created": int(time.time())})
    if read_global(sd) is None:
        _write_global(sd, {"last": 0, "login": True})


def active_slot(sd):
    """Profile whose settings Studio edits (the one used last on the console)."""
    g = read_global(sd)
    slots = [s for s in range(MAX_PROFILES) if read_profile(sd, s)]
    if not slots:
        return None
    if g and g["last"] in slots:
        return g["last"]
    return slots[0]


def _playtime_summary(folder):
    try:
        with open(os.path.join(folder, "playtime.bin"), "rb") as f:
            data = f.read()
        magic, count = struct.unpack("<Ii", data[:8])
        if magic != PLAYTIME_MAGIC or count < 0 or count > 256:
            return 0, 0
        size = struct.calcsize(PLAYTIME_ENTRY)
        total = games = 0
        for i in range(count):
            fn, launches, minutes, _ = struct.unpack(PLAYTIME_ENTRY, data[8 + i * size:8 + (i + 1) * size])
            total += minutes
            games += 1 if (minutes or launches) else 0
        return total, games
    except (OSError, struct.error):
        return 0, 0


def list_profiles(sd):
    """Read-only: a card that has not been started with profiles yet shows its one (legacy) profile."""
    g = read_global(sd) or {"last": 0, "login": True}
    out = []
    if not any(read_profile(sd, s) for s in range(MAX_PROFILES)):
        hyper = os.path.join(sd, "_hyper")
        minutes, games = _playtime_summary(hyper)
        out.append({"slot": 0, "name": _settings_name(os.path.join(hyper, "settings.bin")) or "Oyuncu", "color": 0,
                    "locked": False, "last_title": "", "last_ts": 0, "minutes": minutes, "games": games,
                    "avatar": os.path.isfile(os.path.join(hyper, "avatar.bin")), "wallpaper": False, "last": True})
        return {"profiles": out, "login": g["login"], "max": MAX_PROFILES, "damaged": list(damaged_files)}
    for s in range(MAX_PROFILES):
        p = read_profile(sd, s)
        if not p:
            continue
        folder = profile_dir(sd, s)
        minutes, games = _playtime_summary(folder)
        out.append({"slot": s, "name": p["name"], "color": p["color"], "locked": p["locked"],
                    "last_title": p["last_title"], "last_ts": p["last_ts"], "minutes": minutes, "games": games,
                    "avatar": os.path.isfile(os.path.join(folder, "avatar.bin")),
                    "wallpaper": os.path.isfile(os.path.join(folder, "wallpaper.bin")),
                    "last": s == g["last"]})
    return {"profiles": out, "login": g["login"], "max": MAX_PROFILES, "damaged": list(damaged_files)}


def _check_slot(sd, slot):
    try:
        slot = int(slot)
    except (TypeError, ValueError):
        raise ValueError("profil bulunamadı")
    if slot < 0 or slot >= MAX_PROFILES or not read_profile(sd, slot):
        raise ValueError("profil bulunamadı")
    return slot


def _set_settings_name(folder, name):
    path = os.path.join(folder, "settings.bin")
    try:
        with open(path, "rb") as f:
            raw = bytearray(f.read())
    except OSError:
        return
    if len(raw) >= 40 and struct.unpack("<I", bytes(raw[:4]))[0] == 0x35475453:
        raw[20:40] = _clip_utf8(name, 19).ljust(20, b"\0")
        with open(path, "wb") as f:
            f.write(bytes(raw))


def create_profile(sd, name, color=None):
    ensure_profiles(sd)
    free = [s for s in range(MAX_PROFILES) if not read_profile(sd, s)]
    if not free:
        return {"ok": False, "reason": "full"}
    slot = free[0]
    folder = profile_dir(sd, slot)
    if os.path.exists(folder):
        os.replace(folder, os.path.join(_root(sd), "deleted_p%d_%d" % (slot, int(time.time()))))
    used = {read_profile(sd, s)["color"] for s in range(MAX_PROFILES) if read_profile(sd, s)}
    if color is None or not (0 <= int(color) < PROFILE_COLORS):
        color = next((c for c in range(PROFILE_COLORS) if c not in used), 0)
    _write_profile(sd, slot, {"name": (name or "").strip() or "Oyuncu", "color": int(color), "created": int(time.time())})
    # start from the look and language of the profile used last
    src = active_slot(sd)
    if src is not None and src != slot:
        base = os.path.join(profile_dir(sd, src), "settings.bin")
        if os.path.isfile(base):
            with open(base, "rb") as f:
                raw = bytearray(f.read())
            if len(raw) >= 40:
                raw[20:40] = _clip_utf8(name or "Oyuncu", 19).ljust(20, b"\0")
                with open(os.path.join(folder, "settings.bin"), "wb") as f:
                    f.write(bytes(raw))
    return {"ok": True, "slot": slot}


def _set_settings_theme(sd, slot, theme):
    """Light (1) / dark (0) look of a profile (byte 4 of settings.bin)."""
    path = os.path.join(profile_dir(sd, slot), "settings.bin")
    try:
        with open(path, "rb") as f:
            raw = bytearray(f.read())
    except OSError:
        raw = bytearray()
    if len(raw) < 40 or struct.unpack("<I", bytes(raw[:4]))[0] != 0x35475453:
        p = read_profile(sd, slot) or {"name": "Oyuncu"}
        # magic, theme, language(tr), sfx, volume, brightness, sort, slot, cheats, hidden, covers, splash, name
        raw = bytearray(struct.pack("<I11B5x20s", 0x35475453, 0, 1, 0, 4, 4, 0, 0, 0, 0, 0, 1,
                                    _clip_utf8(p["name"], 19).ljust(20, b"\0")))
    raw[4] = 1 if theme else 0
    with open(path, "wb") as f:
        f.write(bytes(raw))


def update_profile(sd, slot, name=None, color=None, reset_pin=False, theme=None):
    slot = _check_slot(sd, slot)
    if theme in (0, 1, True, False):
        _set_settings_theme(sd, slot, int(theme))
    p = read_profile(sd, slot)
    if name is not None and name.strip():
        p["name"] = name.strip()
        _set_settings_name(profile_dir(sd, slot), p["name"])
    if color is not None:
        p["color"] = int(color) % PROFILE_COLORS
    if reset_pin:
        p["pin_len"], p["pin"] = 0, b""
    _write_profile(sd, slot, p)
    return {"ok": True}


def delete_profile(sd, slot):
    slot = _check_slot(sd, slot)
    slots = [s for s in range(MAX_PROFILES) if read_profile(sd, s)]
    if len(slots) <= 1:
        return {"ok": False, "reason": "last"}
    os.replace(profile_dir(sd, slot), os.path.join(_root(sd), "deleted_p%d_%d" % (slot, int(time.time()))))
    g = read_global(sd) or {"last": 0, "login": True}
    if g["last"] == slot:
        g["last"] = next(s for s in slots if s != slot)
        _write_global(sd, g)
    return {"ok": True}


def set_login_screen(sd, on):
    ensure_profiles(sd)
    g = read_global(sd) or {"last": 0, "login": True}
    g["login"] = bool(on)
    _write_global(sd, g)
    return {"ok": True}


# ---- pictures ---------------------------------------------------------------
def _rgb555(rgba, blend_to=40):
    out = bytearray()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
        if a < 255:
            r = (r * a + blend_to * (255 - a)) // 255
            g = (g * a + blend_to * (255 - a)) // 255
            b = (b * a + blend_to * (255 - a)) // 255
        out += struct.pack("<H", 0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10))
    return out


def _write_atomic(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + ".tmp", "wb") as f:
        f.write(data)
    os.replace(path + ".tmp", path)


def install_avatar(sd, slot, rgba):
    slot = _check_slot(sd, slot)
    if len(rgba) != AVATAR_SIZE * AVATAR_SIZE * 4:
        return {"ok": False, "reason": "size"}
    _write_atomic(os.path.join(profile_dir(sd, slot), "avatar.bin"),
                  b"HAV1" + struct.pack("<HH", AVATAR_SIZE, AVATAR_SIZE) + _rgb555(rgba))
    return {"ok": True}


def install_wallpaper(sd, slot, rgba):
    slot = _check_slot(sd, slot)
    if len(rgba) != WP_W * WP_H * 4:
        return {"ok": False, "reason": "size"}
    _write_atomic(os.path.join(profile_dir(sd, slot), "wallpaper.bin"),
                  b"HWP1" + struct.pack("<HH", WP_W, WP_H) + _rgb555(rgba, 0))
    return {"ok": True}


def _bin_png(path, magic, w, h):
    from .pngcodec import encode_rgb
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if data[:4] != magic or len(data) != 8 + w * h * 2:
        return None
    rgb = bytearray()
    for i in range(8, len(data), 2):
        v = data[i] | (data[i + 1] << 8)
        r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
        rgb += bytes(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)))
    return encode_rgb(w, h, bytes(rgb))


def _pic_path(sd, slot, name):
    try:
        slot = int(slot)
    except (TypeError, ValueError):
        raise ValueError("profil bulunamadı")
    if read_profile(sd, slot):
        return os.path.join(profile_dir(sd, slot), name)
    if slot == 0 and not any(read_profile(sd, s) for s in range(MAX_PROFILES)):
        return os.path.join(sd, "_hyper", name)   # card not started with profiles yet
    raise ValueError("profil bulunamadı")


def avatar_png(sd, slot):
    return _bin_png(_pic_path(sd, slot, "avatar.bin"), b"HAV1", AVATAR_SIZE, AVATAR_SIZE)


def wallpaper_png(sd, slot):
    return _bin_png(_pic_path(sd, slot, "wallpaper.bin"), b"HWP1", WP_W, WP_H)


def clear_picture(sd, slot, kind):
    slot = _check_slot(sd, slot)
    name = {"avatar": "avatar.bin", "wallpaper": "wallpaper.bin"}.get(kind)
    if not name:
        raise ValueError("geçersiz tür")
    p = os.path.join(profile_dir(sd, slot), name)
    if os.path.isfile(p):
        os.remove(p)
        return True
    return False


# ---- backup -------------------------------------------------------------------
def backup_profile(sd, slot, target_dir):
    """Zip of one profile: its folder (settings, play time, DS saves of profiles 2-4) and, for
    profile 1, the DS saves next to the games."""
    from .systems import SAVE_EXTENSIONS
    slot = _check_slot(sd, slot)
    p = read_profile(sd, slot)
    os.makedirs(target_dir, exist_ok=True)
    safe = "".join(ch for ch in p["name"] if ch.isalnum() or ch in "-_ ") or "profil"
    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out = os.path.join(target_dir, "DSi-profil_%s_%s.zip" % (safe.strip().replace(" ", "_"), stamp))
    count = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        folder = profile_dir(sd, slot)
        for root, _, files in os.walk(folder):
            for f in files:
                full = os.path.join(root, f)
                try:
                    z.write(full, os.path.relpath(full, sd).replace("\\", "/"))
                    count += 1
                except OSError:
                    damaged_files.append(os.path.relpath(full, sd).replace("\\", "/"))
        if slot == 0:
            for base in ("roms", "games"):
                top = os.path.join(sd, base)
                for root, _, files in os.walk(top):
                    for f in files:
                        if f.lower().endswith(SAVE_EXTENSIONS):
                            full = os.path.join(root, f)
                            try:
                                z.write(full, os.path.relpath(full, sd).replace("\\", "/"))
                                count += 1
                            except OSError:
                                damaged_files.append(os.path.relpath(full, sd).replace("\\", "/"))
    return {"count": count, "zip": out}


def settings_path(sd):
    """settings.bin that Studio should read / change."""
    if os.path.isdir(_root(sd)):
        slot = active_slot(sd)
        if slot is not None:
            return os.path.join(profile_dir(sd, slot), "settings.bin"), slot
    return os.path.join(sd, "_hyper", "settings.bin"), None

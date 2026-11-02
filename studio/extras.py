"""Save backups, GBA BIOS, sound pack and library statistics."""

import datetime
import hashlib
import os
import struct
import wave
import zipfile

from .systems import SAVE_EXTENSIONS, SYSTEMS, iter_library, skip_dir

# ---------------------------------------------------------------------------
# Library statistics
# ---------------------------------------------------------------------------


def library_summary(sd):
    by_system = {}
    total = 0
    size = 0
    for path, sysid in iter_library(sd):
        label = SYSTEMS[sysid][1]
        by_system[label] = by_system.get(label, 0) + 1
        total += 1
        try:
            size += os.path.getsize(path)
        except OSError:
            pass
    return {"total": total, "bytes": size, "by_system": by_system}


# ---------------------------------------------------------------------------
# Saves
# ---------------------------------------------------------------------------
def _iter_saves(sd):
    for root, dirs, files in os.walk(sd):
        rel = os.path.relpath(root, sd)
        top = rel.split(os.sep)[0]
        dirs[:] = [d for d in dirs if not d.startswith(".") and (rel != "." or d in ("roms", "games", "saves", "_hyper") or not skip_dir(d))]
        if top == "_nds":
            continue
        for f in files:
            low = f.lower()
            if f.startswith("."):
                continue
            if low.endswith(SAVE_EXTENSIONS) or (top in ("roms", "games") and low.endswith(".txt")) or (top == "_hyper" and low.endswith((".bak", ".bin", ".txt")) and "covers" not in rel and "sfx" not in rel):
                yield os.path.join(root, f)


def default_backup_dir():
    home = os.path.expanduser("~")
    docs = os.path.join(home, "Documents")
    base = docs if os.path.isdir(docs) else home
    new, old = os.path.join(base, "DSplay Backups"), os.path.join(base, "HyperLauncher Backups")
    if os.path.isdir(old) and not os.path.exists(new):
        try:
            os.rename(old, new)          # the folder of 2.x keeps every backup, only its name changes
        except OSError:
            return old
    return new


def backup_saves(sd, target_dir=None):
    target_dir = target_dir or default_backup_dir()
    os.makedirs(target_dir, exist_ok=True)
    files = list(_iter_saves(sd))
    if not files:
        return {"count": 0, "zip": None}
    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out = os.path.join(target_dir, f"DSi-saves_{stamp}.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        done, skipped = 0, []
        for p in files:
            try:
                z.write(p, os.path.relpath(p, sd).replace("\\", "/"))
                done += 1
            except OSError:  # damaged file on the card: keep going
                skipped.append(os.path.relpath(p, sd).replace("\\", "/"))
    return {"count": done, "zip": out, "skipped": skipped}


def restore_saves(sd, zip_path):
    """Restores save files from a backup made by backup_saves (current saves are backed up first)."""
    from . import safety as _safety
    snap = _safety.snapshot(sd, "saves")      # today's saves, checked; stops here if that fails
    restored = 0
    with zipfile.ZipFile(zip_path) as z:
        for m in z.infolist():
            n = m.filename.replace("\\", "/")
            parts = [p for p in n.split("/") if p not in ("", ".")]
            if m.is_dir() or not parts or ".." in parts or n.startswith("/") or ":" in parts[0]:
                continue
            low = parts[-1].lower()
            if not (low.endswith(SAVE_EXTENSIONS) or (parts[0] in ("roms", "games") and low.endswith(".txt")) or (parts[0] == "_hyper" and low.endswith((".bak", ".bin", ".txt")))):
                continue
            dst = os.path.join(sd, *parts)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with z.open(m) as src, open(dst + ".tmp", "wb") as out:
                out.write(src.read())
            os.replace(dst + ".tmp", dst)
            restored += 1
    return {"restored": restored, "safety_backup": snap["path"]}


# ---------------------------------------------------------------------------
# GBA BIOS (needed by GBARunner2; cannot be shipped for copyright reasons)
# ---------------------------------------------------------------------------
GBA_BIOS_MD5 = "a860e8c0b6d573d191e4ec7db1b1e4f6"


def install_gba_bios(sd, data):
    if len(data) != 16384:
        return {"ok": False, "reason": "size", "size": len(data)}
    md5 = hashlib.md5(data).hexdigest()  # noqa: S324 - identification only
    folder = os.path.join(sd, "_gba")
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "bios.bin"), "wb") as f:
        f.write(data)
    return {"ok": True, "verified": md5 == GBA_BIOS_MD5, "md5": md5}


# ---------------------------------------------------------------------------
# Sound pack (sd:/_hyper/sfx/<event>.wav) - see source/sound_effects.c
# ---------------------------------------------------------------------------
SFX_EVENTS = ["tick", "decide", "back", "launch", "popup", "toggle", "page", "error",
              "insert", "eject", "favorite", "startup", "key", "boot_snap", "lid_close", "lid_open"]
SFX_RATE = 22050
SFX_MAX_SECONDS = {"startup": 3.0, "launch": 2.0, "boot_snap": 3.0}
SFX_DEFAULT_MAX = 1.0


def _read_wav(data):
    import io
    with wave.open(io.BytesIO(data), "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    step = ch * width
    out = []
    for i in range(0, len(raw) - step + 1, step):
        acc = 0
        for c in range(ch):
            s = raw[i + c * width:i + (c + 1) * width]
            if width == 1:
                v = (s[0] - 128) << 8
            elif width == 2:
                v = struct.unpack("<h", s)[0]
            elif width == 3:
                v = int.from_bytes(s, "little", signed=True) >> 8
            else:
                v = struct.unpack("<i", s)[0] >> 16
            acc += v
        out.append(acc // ch)
    return out, rate


def _resample(samples, src, dst):
    if src == dst or not samples:
        return samples
    ratio = src / dst
    n = int(len(samples) / ratio)
    res = []
    for i in range(n):
        pos = i * ratio
        j = int(pos)
        frac = pos - j
        a = samples[j]
        b = samples[j + 1] if j + 1 < len(samples) else a
        res.append(int(a + (b - a) * frac))
    return res


def install_sfx(sd, event, data):
    if event not in SFX_EVENTS:
        return {"ok": False, "reason": "event"}
    try:
        samples, rate = _read_wav(data)
    except (wave.Error, EOFError, struct.error):
        return {"ok": False, "reason": "format"}
    samples = _resample(samples, rate, SFX_RATE)
    samples = samples[: int(SFX_RATE * SFX_MAX_SECONDS.get(event, SFX_DEFAULT_MAX))]
    if not samples:
        return {"ok": False, "reason": "empty"}
    peak = max(abs(s) for s in samples)
    if peak:
        gain = min(4.0, 29000 / peak)
        samples = [max(-32768, min(32767, int(s * gain))) for s in samples]
    # 3 ms fade out avoids clicks
    fade = min(len(samples), int(SFX_RATE * 0.003))
    for i in range(fade):
        samples[-1 - i] = samples[-1 - i] * i // fade
    folder = os.path.join(sd, "_hyper", "sfx")
    os.makedirs(folder, exist_ok=True)
    with wave.open(os.path.join(folder, event + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SFX_RATE)
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    return {"ok": True, "seconds": round(len(samples) / SFX_RATE, 2)}


def _classic_sounds(rate=SFX_RATE):
    """The rounded, marimba-like "classic" menu sounds (the console's former second pack, now
    an optional download): same recipe as the synthesizer DSplay used to carry."""
    import math

    def mix(ms):
        return [0.0] * int(rate * ms / 1000)

    def tone(buf, f, amp, start_ms, dur_ms, attack_ms, decay_ms):
        start, dur = int(rate * start_ms / 1000), int(rate * dur_ms / 1000)
        att = max(1, int(rate * attack_ms / 1000))
        dq = math.exp(-1.0 / (rate * decay_ms / 1000.0))
        env, phase, step = 1.0, 0.0, 2 * math.pi * f / rate
        for i in range(dur):
            if start + i >= len(buf):
                break
            phase += step
            a = min(1.0, i / att)
            buf[start + i] += math.sin(phase) * amp * env * a
            env *= dq

    def end(buf, gain=0.8):
        fade = int(rate * 0.004)
        n = len(buf)
        out = []
        for i, v in enumerate(buf):
            x = v * gain * 30000 / 32767 * 32767
            if i > n - fade:
                x = x * (n - i) / fade
            out.append(max(-32768, min(32767, int(x))))
        return out

    b = mix(40); tone(b, 880, 0.5, 0, 40, 2, 12); tick = end(b)
    b = mix(160); tone(b, 1046, 0.4, 0, 160, 3, 60); tone(b, 1568, 0.3, 40, 120, 3, 60); decide = end(b)
    b = mix(90); tone(b, 330, 0.5, 0, 90, 3, 25); back = end(b)
    b = mix(400)
    for f in (523, 659, 784):
        tone(b, f, 0.25, 0, 400, 4, 120)
    launch = end(b)
    return {"tick": tick, "key": tick, "page": tick, "toggle": tick,
            "decide": decide, "popup": decide, "favorite": decide, "lid_open": decide,
            "back": back, "error": back, "eject": back, "lid_close": back,
            "launch": launch, "insert": launch, "startup": launch, "boot_snap": launch}


def install_classic_sfx(sd):
    """Writes the classic DSi-like pack to sd:/_hyper/sfx (replaces custom sounds of those events)."""
    folder = os.path.join(sd, "_hyper", "sfx")
    os.makedirs(folder, exist_ok=True)
    n = 0
    for ev, samples in _classic_sounds().items():
        tmp = os.path.join(folder, ev + ".wav.tmp")
        with wave.open(tmp, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(SFX_RATE)
            w.writeframes(struct.pack(f"<{len(samples)}h", *samples))
        os.replace(tmp, os.path.join(folder, ev + ".wav"))
        n += 1
    return {"ok": True, "count": n}


def list_sfx(sd):
    folder = os.path.join(sd, "_hyper", "sfx")
    try:
        present = {os.path.splitext(n)[0].lower() for n in os.listdir(folder) if n.lower().endswith(".wav")}
    except OSError:
        present = set()
    return [e for e in SFX_EVENTS if e in present]


def clear_sfx(sd):
    folder = os.path.join(sd, "_hyper", "sfx")
    removed = 0
    for e in SFX_EVENTS:
        p = os.path.join(folder, e + ".wav")
        if os.path.isfile(p):
            os.remove(p)
            removed += 1
    return removed


# ---------------------------------------------------------------------------
# Profile picture (sd:/_hyper/avatar.bin, read by ui_avatar_load in the menu)
# ---------------------------------------------------------------------------
AVATAR_SIZE = 64


def install_avatar(sd, rgba):
    """rgba: 64x64 RGBA bytes (the page crops and scales the picture in the browser)."""
    if len(rgba) != AVATAR_SIZE * AVATAR_SIZE * 4:
        return {"ok": False, "reason": "size"}
    out = bytearray(b"HAV1" + struct.pack("<HH", AVATAR_SIZE, AVATAR_SIZE))
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
        if a < 255:  # transparent parts on the menu's dark grey
            r = (r * a + 40 * (255 - a)) // 255
            g = (g * a + 40 * (255 - a)) // 255
            b = (b * a + 40 * (255 - a)) // 255
        out += struct.pack("<H", 0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10))
    folder = os.path.join(sd, "_hyper")
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "avatar.bin.tmp"), "wb") as f:
        f.write(out)
    os.replace(os.path.join(folder, "avatar.bin.tmp"), os.path.join(folder, "avatar.bin"))
    return {"ok": True}


def avatar_png(sd):
    from .pngcodec import encode_rgb
    path = os.path.join(sd, "_hyper", "avatar.bin")
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if data[:4] != b"HAV1" or len(data) != 8 + AVATAR_SIZE * AVATAR_SIZE * 2:
        return None
    rgb = bytearray()
    for i in range(8, len(data), 2):
        v = data[i] | (data[i + 1] << 8)
        r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
        rgb += bytes(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)))
    return encode_rgb(AVATAR_SIZE, AVATAR_SIZE, bytes(rgb))


def clear_avatar(sd):
    path = os.path.join(sd, "_hyper", "avatar.bin")
    if os.path.isfile(path):
        os.remove(path)
        return True
    return False


# ---------------------------------------------------------------------------
# Photos (sd:/photos/<album>/...): the page scales each picture in the browser (any format the
# browser can open) to at most 640x480 and sends it as a baseline JPEG the console can decode.
# ---------------------------------------------------------------------------
def _safe_part(name, default):
    keep = "".join(ch for ch in (name or "") if ch.isalnum() or ch in " -_.()").strip(" .")
    return keep[:60] or default


def install_photo(sd, album, name, data):
    if not data or data[:2] != b"\xff\xd8":
        return {"ok": False, "reason": "format"}
    album = _safe_part(album, "Studio")
    base = os.path.splitext(_safe_part(name, "photo"))[0] + ".jpg"
    folder = os.path.join(sd, "photos", album)
    os.makedirs(folder, exist_ok=True)
    dst = os.path.join(folder, base)
    if os.path.isfile(dst) and os.path.getsize(dst) == len(data):
        return {"ok": True, "status": "exists"}
    with open(dst + ".tmp", "wb") as f:
        f.write(data)
    os.replace(dst + ".tmp", dst)
    return {"ok": True, "status": "copied"}


def list_photo_albums(sd):
    root = os.path.join(sd, "photos")
    out = []
    try:
        for n in sorted(os.listdir(root)):
            p = os.path.join(root, n)
            if os.path.isdir(p):
                out.append({"name": n, "count": sum(1 for f in os.listdir(p) if f.lower().endswith((".jpg", ".jpeg", ".png")))})
    except OSError:
        pass
    return out

"""Custom boot screen (made on the Themes page, played by source/splash.c).

    _hyper/boot/top.bin      "HWP1", u16 256, u16 192, RGB555   upper screen
    _hyper/boot/bottom.bin   same, optional                      lower screen (black when missing)
    _hyper/boot/boot.wav     16 kHz mono 16-bit PCM, at most 5 s  (optional)
    _hyper/boot/boot.cfg     ms=<duration>  style=fade|flash

The console reads the two pictures once (about 0.05 s) and animates only the display
brightness, so the menu loads exactly as fast as with the built-in animation.
"""

import glob
import os
import struct
import wave

from .profiles import _rgb555, _write_atomic

W, H = 256, 192
MAX_SECONDS = 5.0
RATE = 16000
SPLASH_ON, SPLASH_CUSTOM = 1, 2
SPLASH_OFFSET = 14          # byte of SystemSettings.splash in settings.bin


def _dir(sd):
    return os.path.join(sd, "_hyper", "boot")


def install_picture(sd, which, rgba):
    if which not in ("top", "bottom"):
        raise ValueError("geçersiz ekran")
    if len(rgba) != W * H * 4:
        return {"ok": False, "reason": "size"}
    os.makedirs(_dir(sd), exist_ok=True)
    _write_atomic(os.path.join(_dir(sd), which + ".bin"), b"HWP1" + struct.pack("<HH", W, H) + _rgb555(rgba, 0))
    return {"ok": True}


def clear_picture(sd, which):
    p = os.path.join(_dir(sd), which + ".bin")
    if which in ("top", "bottom") and os.path.isfile(p):
        os.remove(p)


def install_sound(sd, data):
    """A WAV the page made (16 kHz mono 16-bit); checked and cut to 5 s."""
    import io
    try:
        with wave.open(io.BytesIO(data)) as w:
            if w.getnchannels() != 1 or w.getsampwidth() != 2 or w.getframerate() != RATE:
                return {"ok": False, "reason": "format"}
            frames = w.readframes(min(w.getnframes(), int(RATE * MAX_SECONDS)))
    except (wave.Error, EOFError):
        return {"ok": False, "reason": "format"}
    if len(frames) < 64:
        return {"ok": False, "reason": "empty"}
    os.makedirs(_dir(sd), exist_ok=True)
    tmp = os.path.join(_dir(sd), "boot.wav.tmp")
    with wave.open(tmp, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)
    os.replace(tmp, os.path.join(_dir(sd), "boot.wav"))
    return {"ok": True, "seconds": round(len(frames) / 2 / RATE, 2)}


def clear_sound(sd):
    p = os.path.join(_dir(sd), "boot.wav")
    if os.path.isfile(p):
        os.remove(p)


def _settings_files(sd):
    return glob.glob(os.path.join(sd, "_hyper", "profiles", "p[0-3]", "settings.bin")) + \
        [p for p in [os.path.join(sd, "_hyper", "settings.bin")] if os.path.isfile(p)]


def _set_splash(sd, value, only_if=None):
    """Turns the custom boot screen on / off in every profile's settings."""
    n = 0
    for p in _settings_files(sd):
        try:
            with open(p, "rb") as f:
                raw = bytearray(f.read())
        except OSError:
            continue
        if len(raw) < 40 or struct.unpack("<I", bytes(raw[:4]))[0] != 0x35475453:
            continue
        if only_if is not None and raw[SPLASH_OFFSET] != only_if:
            continue
        raw[SPLASH_OFFSET] = value
        _write_atomic(p, bytes(raw))
        n += 1
    return n


def save_settings(sd, ms=2500, style="fade", use=True):
    if not os.path.isfile(os.path.join(_dir(sd), "top.bin")):
        raise ValueError("önce üst ekran resmini seç")
    ms = max(800, min(5000, int(ms)))
    style = "flash" if style == "flash" else "fade"
    os.makedirs(_dir(sd), exist_ok=True)
    tmp = os.path.join(_dir(sd), "boot.cfg.tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write("ms=%d\nstyle=%s\n" % (ms, style))
    os.replace(tmp, os.path.join(_dir(sd), "boot.cfg"))
    profiles_set = _set_splash(sd, SPLASH_CUSTOM) if use else 0
    return {"ok": True, "profiles": profiles_set}


def remove(sd):
    """Deletes the custom boot screen; profiles that used it get the built-in animation."""
    for name in ("top.bin", "bottom.bin", "boot.wav", "boot.cfg"):
        p = os.path.join(_dir(sd), name)
        if os.path.isfile(p):
            os.remove(p)
    return {"ok": True, "profiles": _set_splash(sd, SPLASH_ON, only_if=SPLASH_CUSTOM)}


def status(sd):
    d = _dir(sd)
    cfg = {"ms": 2500, "style": "fade"}
    try:
        with open(os.path.join(d, "boot.cfg"), encoding="utf-8") as f:
            for line in f:
                k, _, v = line.strip().partition("=")
                if k == "ms" and v.isdigit():
                    cfg["ms"] = int(v)
                elif k == "style":
                    cfg["style"] = "flash" if v == "flash" else "fade"
    except OSError:
        pass
    seconds = None
    try:
        with wave.open(os.path.join(d, "boot.wav")) as w:
            seconds = round(w.getnframes() / float(w.getframerate()), 2)
    except (OSError, wave.Error, EOFError):
        pass
    used = 0
    for p in _settings_files(sd):
        try:
            with open(p, "rb") as f:
                raw = f.read(40)
            used += len(raw) >= 40 and raw[SPLASH_OFFSET] == SPLASH_CUSTOM
        except OSError:
            pass
    return {"top": os.path.isfile(os.path.join(d, "top.bin")), "bottom": os.path.isfile(os.path.join(d, "bottom.bin")),
            "sound": seconds, "ms": cfg["ms"], "style": cfg["style"], "profiles_using": used}


def picture_png(sd, which):
    from .profiles import _bin_png
    if which not in ("top", "bottom"):
        return None
    return _bin_png(os.path.join(_dir(sd), which + ".bin"), b"HWP1", W, H)

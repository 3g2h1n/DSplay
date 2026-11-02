"""Kumanda: the DSi as a wireless controller for this computer.

The console (source/pad.c on the DSi) and Studio talk over UDP on the local network:

    header  "HPAD", u8 version (1), u8 type, u16 seq, u32 token          (little endian)
    HELLO   DSi -> broadcast:39511   name[24], dsi id, id of the computer it was paired with
    PAIR    PC  -> DSi:39510         code u16, pc id, token, pc name[32]   (code typed in Studio)
    CONNECT PC  -> DSi:39510         pc id, token, pc name[32]             (known console)
    ACCEPT  DSi -> PC                pc id, dsi id
    STATE   DSi -> PC, 60 per second keys u16, touch x/y/down, page, widget mask u32, stick x/y,
                                     trackpad dx/dy, 4 sliders, battery, signal
    LAYOUT  PC  -> DSi               touch screen layout ("HLAY" blob, see encode_layout)
    PING    PC  -> DSi (+ last rtt), PONG back
    BYE     either way

What the console sends is turned into input on this computer by a backend:
  * Windows: keyboard and mouse through SendInput; an Xbox 360 controller through the
    ViGEmBus driver when the 'vgamepad' package is installed.
  * macOS: keyboard and mouse through Quartz events (needs the Accessibility permission).
    macOS has no way to add a virtual game controller without a signed driver, so "pad:"
    actions are not available there; games can be played with keyboard / mouse layouts.
  * Anything else (and the tests): a recording backend.
"""

import json
import os
import random
import socket
import struct
import sys
import threading
import time

PORT_DSI = 39510
PORT_PC = 39511
VERSION = 1
T_HELLO, T_PAIR, T_ACCEPT, T_CONNECT, T_STATE, T_LAYOUT, T_PING, T_PONG, T_BYE, T_SEEK, T_INFO, T_STORE, T_STORE_RES = range(1, 14)

KEY_NAMES = ["A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y"]
MIC = "MIC"                  # blowing into the microphone acts like one more button
MIC_LEVEL = 70               # 0..127
W_BUTTON, W_STICK, W_TRACKPAD, W_SLIDER = 1, 2, 3, 4
WIDGET_TYPES = {"button": W_BUTTON, "stick": W_STICK, "trackpad": W_TRACKPAD, "slider": W_SLIDER}
MAX_PAGES, MAX_WIDGETS = 4, 16
COLORS = 10          # palette size on the console (pad.c widget_color)

PAD_BUTTONS = ["a", "b", "x", "y", "lb", "rb", "back", "start", "guide", "ls", "rs", "up", "down", "left", "right"]
KEYS = (["%c" % c for c in "abcdefghijklmnopqrstuvwxyz0123456789"] +
        ["f%d" % i for i in range(1, 13)] +
        ["space", "enter", "esc", "tab", "backspace", "delete", "up", "down", "left", "right", "shift", "ctrl", "alt",
         "cmd", "home", "end", "pageup", "pagedown", "minus", "equal", "comma", "period", "slash",
         "volup", "voldown", "mute", "playpause", "next", "prev"])
MODIFIERS = ("shift", "ctrl", "alt", "cmd")


def config_dir():
    d = os.path.join(os.path.expanduser("~"), ".hyperlauncher")
    os.makedirs(d, exist_ok=True)
    return d


# --------------------------------------------------------------------------- layouts
def _w(wid, typ, x, y, w, h, color, label, action, shape=0):
    return {"id": wid, "type": typ, "x": x, "y": y, "w": w, "h": h, "color": color, "shape": shape,
            "label": label, "action": action}


def default_layout(gamepad=True):
    """Same widgets as pad_layout_default() on the console."""
    g = gamepad
    return {"pages": [
        {"name": "Kol", "widgets": [
            _w(0, "stick", 8, 26, 112, 112, 0, "R", "pad:rs" if g else "mouse"),
            _w(1, "button", 130, 26, 56, 40, 1, "LT", "pad:lt" if g else "mouse:right"),
            _w(2, "button", 192, 26, 56, 40, 1, "RT", "pad:rt" if g else "mouse:left"),
            _w(3, "button", 130, 72, 56, 40, 8, "L3", "pad:ls" if g else "key:c"),
            _w(4, "button", 192, 72, 56, 40, 8, "R3", "pad:rs" if g else "key:v"),
            _w(5, "button", 130, 118, 118, 30, 9, "Guide", "pad:guide" if g else "key:m"),
        ]},
        {"name": "Fare", "widgets": [
            _w(6, "trackpad", 8, 26, 196, 130, 9, "", "mouse"),
            _w(7, "button", 210, 26, 38, 62, 0, "Sol", "mouse:left"),
            _w(8, "button", 210, 94, 38, 62, 0, "Sağ", "mouse:right"),
        ]},
    ]}


PRESETS = {
    "gamepad": {"A": "pad:b", "B": "pad:a", "X": "pad:y", "Y": "pad:x", "L": "pad:lb", "R": "pad:rb",
                "START": "pad:start", "SELECT": "pad:back", "UP": "pad:up", "DOWN": "pad:down",
                "LEFT": "pad:left", "RIGHT": "pad:right"},
    "keyboard": {"A": "key:space", "B": "key:shift", "X": "key:e", "Y": "key:q", "L": "key:ctrl", "R": "key:r",
                 "START": "key:esc", "SELECT": "key:tab", "UP": "key:w", "DOWN": "key:s", "LEFT": "key:a",
                 "RIGHT": "key:d"},
    "mouse": {"A": "mouse:left", "B": "mouse:right", "X": "key:enter", "Y": "key:esc", "L": "scroll:up",
              "R": "scroll:down", "START": "key:space", "SELECT": "key:tab", "UP": "key:up", "DOWN": "key:down",
              "LEFT": "key:left", "RIGHT": "key:right"},
    "presenter": {"A": "key:right", "B": "key:left", "X": "key:f5", "Y": "key:esc", "L": "key:pageup",
                  "R": "key:pagedown", "START": "key:b", "SELECT": "none", "UP": "key:up", "DOWN": "key:down",
                  "LEFT": "key:left", "RIGHT": "key:right"},
    "media": {"A": "key:playpause", "B": "key:mute", "X": "key:next", "Y": "key:prev", "L": "key:voldown",
              "R": "key:volup", "START": "key:space", "SELECT": "none", "UP": "key:volup", "DOWN": "key:voldown",
              "LEFT": "key:prev", "RIGHT": "key:next"},
    "macro": {"A": "key:enter", "B": "key:esc", "X": "key:space", "Y": "key:tab", "L": "key:prev", "R": "key:next",
              "START": "key:playpause", "SELECT": "none", "UP": "key:up", "DOWN": "key:down", "LEFT": "key:left",
              "RIGHT": "key:right"},
}


def _mod():
    return "cmd" if sys.platform == "darwin" else "ctrl"


def preset_layout(preset):
    """Touch screen pages that come with a ready-made layout."""
    mouse = default_layout(False)["pages"][1]
    m = _mod()
    if preset == "presenter":
        page = {"name": "Sunum", "widgets": [
            _w(0, "button", 6, 26, 120, 100, 8, "◀ Geri", "key:left"),
            _w(1, "button", 130, 26, 120, 100, 0, "İleri ▶", "key:right"),
            _w(2, "button", 6, 132, 78, 36, 4, "Başlat", "key:f5"),
            _w(3, "button", 89, 132, 78, 36, 9, "Siyah", "key:b"),
            _w(4, "button", 172, 132, 78, 36, 1, "Çık", "key:esc")]}
    elif preset == "media":
        page = {"name": "Medya", "widgets": [
            _w(0, "button", 6, 26, 76, 70, 8, "◀◀", "key:prev"),
            _w(1, "button", 88, 26, 80, 70, 0, "Oynat", "key:playpause", 1),
            _w(2, "button", 174, 26, 76, 70, 8, "▶▶", "key:next"),
            _w(3, "button", 6, 104, 76, 64, 9, "Ses -", "key:voldown"),
            _w(4, "button", 88, 104, 80, 64, 1, "Sessiz", "key:mute"),
            _w(5, "button", 174, 104, 76, 64, 9, "Ses +", "key:volup")]}
    elif preset == "macro":
        shot = "key:cmd+shift+4" if m == "cmd" else "key:cmd+shift+s"
        desk = "key:f11" if m == "cmd" else "key:cmd+d"
        cells = [("Kopyala", "key:%s+c" % m, 0), ("Yapıştır", "key:%s+v" % m, 0), ("Geri al", "key:%s+z" % m, 8),
                 ("Kaydet", "key:%s+s" % m, 4), ("Ekran al", shot, 5), ("Masaüstü", desk, 7),
                 ("Sessiz", "key:mute", 1), ("Ses -", "key:voldown", 9), ("Ses +", "key:volup", 9),
                 ("◀◀", "key:prev", 3), ("Oynat", "key:playpause", 3), ("▶▶", "key:next", 3)]
        page = {"name": "Makro", "widgets": [
            _w(i, "button", 6 + (i % 4) * 62, 26 + (i // 4) * 48, 58, 44, col, lab, act)
            for i, (lab, act, col) in enumerate(cells)]}
    else:
        return None
    return {"pages": [page, mouse]}


def _clean_label(s, limit=12):
    """UTF-8, at most `limit` bytes, never cut inside a character."""
    s = str(s or "")
    out = ""
    for ch in s:
        if len((out + ch).encode("utf-8")) > limit:
            break
        out += ch
    return out


def valid_action(a, kind):
    if not isinstance(a, str):
        return False
    if a == "none":
        return True
    head, _, rest = a.partition(":")
    if kind == "stick":
        return a in ("mouse", "pad:ls", "pad:rs", "keys:wasd", "keys:arrows", "scroll", "none")
    if kind == "trackpad":
        return a in ("mouse", "scroll", "none")
    if kind == "slider":
        return a in ("pad:lt", "pad:rt", "none")
    if head == "key":
        parts = rest.split("+")
        return 0 < len(parts) <= 4 and all(p in KEYS for p in parts)
    if head == "mouse":
        return rest in ("left", "right", "middle")
    if head == "scroll":
        return rest in ("up", "down")
    if head == "pad":
        return rest in PAD_BUTTONS or rest in ("lt", "rt")
    return False


def validate_layout(layout):
    """Checks a layout from the page; returns a clean copy (raises ValueError)."""
    pages = (layout or {}).get("pages")
    if not isinstance(pages, list) or not 1 <= len(pages) <= MAX_PAGES:
        raise ValueError("1-4 sayfa olmalı")
    ids = set()
    out = []
    for p in pages:
        ws = p.get("widgets") or []
        if len(ws) > MAX_WIDGETS:
            raise ValueError("bir sayfada en fazla 16 tuş olabilir")
        clean = []
        for w in ws:
            typ = w.get("type")
            if typ not in WIDGET_TYPES:
                raise ValueError("bilinmeyen tuş türü")
            x, y, ww, hh = (int(w.get(k, 0)) for k in ("x", "y", "w", "h"))
            ww, hh = max(8, min(256, ww)), max(8, min(172, hh))
            x, y = max(0, min(256 - ww, x)), max(0, min(172 - hh, y))
            wid = len(ids)
            ids.add(wid)
            if wid >= 32:
                raise ValueError("en fazla 32 tuş")
            action = w.get("action", "none")
            if not valid_action(action, typ if typ != "button" else "button"):
                raise ValueError("geçersiz eylem: %s" % action)
            clean.append({"id": wid, "type": typ, "x": x, "y": y, "w": ww, "h": hh,
                          "color": max(0, min(COLORS - 1, int(w.get("color", 0)))),
                          "shape": 1 if w.get("shape") in (1, "round") else 0,
                          "label": _clean_label(w.get("label", "")), "action": action})
        out.append({"name": _clean_label(p.get("name", "")) or str(len(out) + 1), "widgets": clean})
    return {"pages": out}


def encode_layout(layout):
    b = bytearray(b"HLAY" + bytes([1, len(layout["pages"]), 0, 0]))
    for p in layout["pages"]:
        b.append(len(p["widgets"]))
        b += p["name"].encode("utf-8")[:12].ljust(12, b"\0")
        for w in p["widgets"]:
            b += bytes([w["id"], WIDGET_TYPES[w["type"]], w["x"], w["y"], w["w"], w["h"], w["color"], w["shape"]])
            b += w["label"].encode("utf-8")[:12].ljust(12, b"\0")
    return bytes(b)


# --------------------------------------------------------------------------- backends
class RecordingBackend:
    """Keeps a list of what would have been done (Linux, tests)."""
    name = "test"
    gamepad = False
    keyboard = True
    hint = ""

    def __init__(self):
        self.events = []

    def key(self, name, down): self.events.append(("key", name, down))
    def mouse_button(self, b, down): self.events.append(("mouse", b, down))
    def mouse_move(self, dx, dy): self.events.append(("move", dx, dy))
    def scroll(self, n): self.events.append(("scroll", n))
    def pad_button(self, b, down): self.events.append(("pad", b, down))
    def pad_axes(self, axes): self.events.append(("axes", dict(axes)))
    def close(self): pass


class WindowsBackend(RecordingBackend):
    name = "windows"
    VK = {"space": 0x20, "enter": 0x0D, "esc": 0x1B, "tab": 0x09, "backspace": 0x08, "delete": 0x2E, "left": 0x25,
          "up": 0x26, "right": 0x27, "down": 0x28, "shift": 0x10, "ctrl": 0x11, "alt": 0x12, "cmd": 0x5B,
          "home": 0x24, "end": 0x23, "pageup": 0x21, "pagedown": 0x22, "minus": 0xBD, "equal": 0xBB,
          "comma": 0xBC, "period": 0xBE, "slash": 0xBF, "volup": 0xAF, "voldown": 0xAE, "mute": 0xAD,
          "playpause": 0xB3, "next": 0xB0, "prev": 0xB1}
    EXTENDED = {"delete", "left", "up", "right", "down", "home", "end", "pageup", "pagedown", "cmd"}
    MEDIA = {"volup", "voldown", "mute", "playpause", "next", "prev"}

    def __init__(self):
        super().__init__()
        import ctypes
        from ctypes import wintypes
        self.ct = ctypes
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        ULONG_PTR = ctypes.c_size_t

        class MOUSEINPUT(ctypes.Structure):
            _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG), ("mouseData", wintypes.DWORD),
                        ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]

        class KEYBDINPUT(ctypes.Structure):
            _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD), ("dwFlags", wintypes.DWORD),
                        ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]

        class HARDWAREINPUT(ctypes.Structure):
            _fields_ = [("uMsg", wintypes.DWORD), ("wParamL", wintypes.WORD), ("wParamH", wintypes.WORD)]

        class _U(ctypes.Union):
            _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]

        class INPUT(ctypes.Structure):
            _fields_ = [("type", wintypes.DWORD), ("u", _U)]

        self.INPUT, self.MOUSEINPUT, self.KEYBDINPUT = INPUT, MOUSEINPUT, KEYBDINPUT
        self.user32.SendInput.argtypes = (wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int)
        self.user32.MapVirtualKeyW.argtypes = (wintypes.UINT, wintypes.UINT)
        self.pad = None
        try:
            import vgamepad  # noqa: F401  (needs the ViGEmBus driver)
            self.vg = vgamepad
            self.pad = vgamepad.VX360Gamepad()
            self.gamepad = True
        except Exception as e:  # noqa: BLE001
            self.vg = None
            self.hint = "gamepad_missing" if isinstance(e, ImportError) else "gamepad_driver"

    def _send(self, inp):
        self.user32.SendInput(1, self.ct.byref(inp), self.ct.sizeof(inp))

    def key(self, name, down):
        vk = self.VK.get(name) or (ord(name.upper()) if len(name) == 1 else 0x70 + int(name[1:]) - 1 if name[0] == "f" else 0)
        if not vk:
            return
        ki = self.KEYBDINPUT(vk, 0, 0, 0, 0)
        if name not in self.MEDIA:
            # scan codes: games reading DirectInput / raw input see these too
            ki.wScan = self.user32.MapVirtualKeyW(vk, 0)
            ki.wVk = 0
            ki.dwFlags = 0x0008  # KEYEVENTF_SCANCODE
        if name in self.EXTENDED:
            ki.dwFlags |= 0x0001
        if not down:
            ki.dwFlags |= 0x0002
        inp = self.INPUT(1)
        inp.u.ki = ki
        self._send(inp)

    def _mouse(self, flags, dx=0, dy=0, data=0):
        inp = self.INPUT(0)
        inp.u.mi = self.MOUSEINPUT(dx, dy, data & 0xFFFFFFFF, flags, 0, 0)
        self._send(inp)

    def mouse_button(self, b, down):
        f = {"left": (0x2, 0x4), "right": (0x8, 0x10), "middle": (0x20, 0x40)}[b]
        self._mouse(f[0] if down else f[1])

    def mouse_move(self, dx, dy):
        self._mouse(0x1, int(dx), int(dy))

    def scroll(self, n):
        self._mouse(0x800, data=int(n * 120))

    def pad_button(self, b, down):
        if not self.pad:
            return
        B = self.vg.XUSB_BUTTON
        m = {"a": B.XUSB_GAMEPAD_A, "b": B.XUSB_GAMEPAD_B, "x": B.XUSB_GAMEPAD_X, "y": B.XUSB_GAMEPAD_Y,
             "lb": B.XUSB_GAMEPAD_LEFT_SHOULDER, "rb": B.XUSB_GAMEPAD_RIGHT_SHOULDER, "back": B.XUSB_GAMEPAD_BACK,
             "start": B.XUSB_GAMEPAD_START, "guide": B.XUSB_GAMEPAD_GUIDE, "ls": B.XUSB_GAMEPAD_LEFT_THUMB,
             "rs": B.XUSB_GAMEPAD_RIGHT_THUMB, "up": B.XUSB_GAMEPAD_DPAD_UP, "down": B.XUSB_GAMEPAD_DPAD_DOWN,
             "left": B.XUSB_GAMEPAD_DPAD_LEFT, "right": B.XUSB_GAMEPAD_DPAD_RIGHT}
        (self.pad.press_button if down else self.pad.release_button)(button=m[b])
        self.pad.update()

    def pad_axes(self, a):
        if not self.pad:
            return
        conv = lambda v: max(-32768, min(32767, int(v * 32767)))  # noqa: E731
        self.pad.left_joystick(x_value=conv(a["lx"]), y_value=conv(-a["ly"]))
        self.pad.right_joystick(x_value=conv(a["rx"]), y_value=conv(-a["ry"]))
        self.pad.left_trigger(value=int(a["lt"] * 255))
        self.pad.right_trigger(value=int(a["rt"] * 255))
        self.pad.update()

    def close(self):
        if self.pad:
            try:
                self.pad.reset()
                self.pad.update()
            except Exception:  # noqa: BLE001
                pass


class MacBackend(RecordingBackend):
    name = "mac"
    KC = {"a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8, "v": 9, "b": 11, "q": 12, "w": 13,
          "e": 14, "r": 15, "y": 16, "t": 17, "1": 18, "2": 19, "3": 20, "4": 21, "6": 22, "5": 23, "equal": 24,
          "9": 25, "7": 26, "minus": 27, "8": 28, "0": 29, "o": 31, "u": 32, "i": 34, "p": 35, "enter": 36, "l": 37,
          "j": 38, "k": 40, "comma": 43, "slash": 44, "n": 45, "m": 46, "period": 47, "tab": 48, "space": 49,
          "backspace": 51, "esc": 53, "cmd": 55, "shift": 56, "alt": 58, "ctrl": 59, "f1": 122, "f2": 120,
          "f3": 99, "f4": 118, "f5": 96, "f6": 97, "f7": 98, "f8": 100, "f9": 101, "f10": 109, "f11": 103,
          "f12": 111, "home": 115, "pageup": 116, "delete": 117, "end": 119, "pagedown": 121, "left": 123,
          "right": 124, "down": 125, "up": 126, "volup": 72, "voldown": 73, "mute": 74}
    FLAGS = {"shift": 0x20000, "ctrl": 0x40000, "alt": 0x80000, "cmd": 0x100000}

    def __init__(self):
        super().__init__()
        import ctypes
        import ctypes.util
        self.ct = ctypes

        class CGPoint(ctypes.Structure):
            _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]

        class CGRect(ctypes.Structure):
            _fields_ = [("origin", CGPoint), ("size", CGPoint)]

        self.CGPoint = CGPoint
        cg = ctypes.CDLL(ctypes.util.find_library("CoreGraphics") or
                         "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics")
        cf = ctypes.CDLL(ctypes.util.find_library("CoreFoundation") or
                         "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
        vp = ctypes.c_void_p
        cg.CGEventCreateKeyboardEvent.restype = vp
        cg.CGEventCreateKeyboardEvent.argtypes = (vp, ctypes.c_uint16, ctypes.c_bool)
        cg.CGEventCreateMouseEvent.restype = vp
        cg.CGEventCreateMouseEvent.argtypes = (vp, ctypes.c_uint32, CGPoint, ctypes.c_uint32)
        cg.CGEventCreateScrollWheelEvent2.restype = vp
        cg.CGEventCreateScrollWheelEvent2.argtypes = (vp, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int32,
                                                      ctypes.c_int32, ctypes.c_int32)
        cg.CGEventCreate.restype = vp
        cg.CGEventCreate.argtypes = (vp,)
        cg.CGEventGetLocation.restype = CGPoint
        cg.CGEventGetLocation.argtypes = (vp,)
        cg.CGEventPost.argtypes = (ctypes.c_uint32, vp)
        cg.CGEventSetFlags.argtypes = (vp, ctypes.c_uint64)
        cg.CGEventSetIntegerValueField.argtypes = (vp, ctypes.c_uint32, ctypes.c_int64)
        cg.CGGetActiveDisplayList.argtypes = (ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32))
        cg.CGDisplayBounds.restype = CGRect
        cg.CGDisplayBounds.argtypes = (ctypes.c_uint32,)
        cf.CFRelease.argtypes = (vp,)
        self.cg, self.cf = cg, cf
        self.mods = set()
        self.buttons = set()
        self.bounds = self._bounds()
        try:
            ax = ctypes.CDLL("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices")
            ax.AXIsProcessTrusted.restype = ctypes.c_bool
            if not ax.AXIsProcessTrusted():
                self.hint = "mac_accessibility"
        except OSError:
            pass
        self.hint = self.hint or "mac_no_gamepad"

    def _bounds(self):
        ct = self.ct
        ids = (ct.c_uint32 * 16)()
        n = ct.c_uint32(0)
        self.cg.CGGetActiveDisplayList(16, ids, ct.byref(n))
        xs, ys, xe, ye = 0.0, 0.0, 1.0, 1.0
        for i in range(n.value):
            r = self.cg.CGDisplayBounds(ids[i])
            xs, ys = min(xs, r.origin.x), min(ys, r.origin.y)
            xe, ye = max(xe, r.origin.x + r.size.x), max(ye, r.origin.y + r.size.y)
        return xs, ys, xe - 1, ye - 1

    def _post(self, ev):
        if ev:
            self.cg.CGEventPost(0, ev)
            self.cf.CFRelease(ev)

    def _flags(self):
        f = 0
        for m in self.mods:
            f |= self.FLAGS[m]
        return f

    def key(self, name, down):
        if name in self.FLAGS:
            (self.mods.add if down else self.mods.discard)(name)
        kc = self.KC.get(name)
        if kc is None:
            return
        ev = self.cg.CGEventCreateKeyboardEvent(None, kc, down)
        self.cg.CGEventSetFlags(ev, self._flags())
        self._post(ev)

    def _pos(self):
        ev = self.cg.CGEventCreate(None)
        p = self.cg.CGEventGetLocation(ev)
        self.cf.CFRelease(ev)
        return p

    def mouse_button(self, b, down):
        btn = {"left": 0, "right": 1, "middle": 2}[b]
        typ = {0: (1, 2), 1: (3, 4), 2: (25, 26)}[btn][0 if down else 1]
        (self.buttons.add if down else self.buttons.discard)(b)
        self._post(self.cg.CGEventCreateMouseEvent(None, typ, self._pos(), btn))

    def mouse_move(self, dx, dy):
        p = self._pos()
        x0, y0, x1, y1 = self.bounds
        q = self.CGPoint(max(x0, min(x1, p.x + dx)), max(y0, min(y1, p.y + dy)))
        typ, btn = (6, 0) if "left" in self.buttons else (7, 1) if "right" in self.buttons else (5, 0)
        ev = self.cg.CGEventCreateMouseEvent(None, typ, q, btn)
        self.cg.CGEventSetIntegerValueField(ev, 4, int(dx))   # kCGMouseEventDeltaX: games read these
        self.cg.CGEventSetIntegerValueField(ev, 5, int(dy))
        self._post(ev)

    def scroll(self, n):
        self._post(self.cg.CGEventCreateScrollWheelEvent2(None, 1, 1, int(n), 0, 0))


def make_backend():
    try:
        if sys.platform == "win32":
            return WindowsBackend()
        if sys.platform == "darwin":
            return MacBackend()
    except Exception:  # noqa: BLE001
        import traceback
        traceback.print_exc()
    b = RecordingBackend()
    b.keyboard = False
    b.hint = "unsupported"
    return b


# --------------------------------------------------------------------------- state -> input
class Engine:
    def __init__(self, backend):
        self.b = backend
        self.cfg = None
        self.digital = set()
        self.axes = {"lx": 0.0, "ly": 0.0, "rx": 0.0, "ry": 0.0, "lt": 0.0, "rt": 0.0}
        self.scroll_t = {}
        self.frac = [0.0, 0.0]
        self.last = None

    def set_config(self, cfg):
        self.release_all()
        self.cfg = cfg
        self.widgets = {}
        for pi, p in enumerate(cfg["layout"]["pages"]):
            for w in p["widgets"]:
                self.widgets[w["id"]] = (pi, w)

    # actions that are simply held down
    def _press(self, act, down):
        kind, _, arg = act.partition(":")
        if kind == "key":
            parts = arg.split("+")
            for p in (parts if down else reversed(parts)):
                self.b.key(p, down)
        elif kind == "mouse":
            self.b.mouse_button(arg, down)
        elif kind == "pad" and arg in PAD_BUTTONS:
            self.b.pad_button(arg, down)

    def release_all(self):
        for a in list(self.digital):
            self._press(a, False)
        self.digital = set()
        if any(self.axes.values()):
            self.axes = {k: 0.0 for k in self.axes}
            self.b.pad_axes(self.axes)

    def update(self, st):
        """st: parsed STATE (see parse_state)."""
        if not self.cfg:
            return
        self.last = st
        cfg = self.cfg
        want = set()
        axes = {k: 0.0 for k in self.axes}
        move = [0.0, 0.0]
        scroll = 0.0
        for i, name in enumerate(KEY_NAMES):
            if st["keys"] >> i & 1:
                a = cfg["buttons"].get(name, "none")
                if a != "none":
                    want.add(a)
        if st.get("mic", 0) >= MIC_LEVEL and cfg["buttons"].get(MIC, "none") != "none":
            want.add(cfg["buttons"][MIC])
        speed = cfg.get("mouse_speed", 1.0)
        page_sliders = []
        for wid, (pi, w) in self.widgets.items():
            if w["type"] == "slider" and pi == st["page"]:
                page_sliders.append(w)
            if not (st["pressed"] >> wid & 1) or pi != st["page"]:
                continue
            a = w["action"]
            if w["type"] == "button":
                if a != "none":
                    want.add(a)
            elif w["type"] == "stick":
                sx, sy = st["sx"] / 127.0, st["sy"] / 127.0
                if a in ("pad:ls", "pad:rs"):
                    k = "l" if a == "pad:ls" else "r"
                    axes[k + "x"], axes[k + "y"] = sx, sy
                elif a == "mouse":
                    move[0] += sx * abs(sx) * 14 * speed
                    move[1] += sy * abs(sy) * 14 * speed
                elif a == "scroll":
                    scroll -= sy * 0.35
                elif a in ("keys:wasd", "keys:arrows"):
                    names = ("w", "a", "s", "d") if a == "keys:wasd" else ("up", "left", "down", "right")
                    if sy < -0.35: want.add("key:" + names[0])
                    if sx < -0.35: want.add("key:" + names[1])
                    if sy > 0.35: want.add("key:" + names[2])
                    if sx > 0.35: want.add("key:" + names[3])
            elif w["type"] == "trackpad":
                if a == "mouse":
                    move[0] += st["dx"] * 2.2 * speed
                    move[1] += st["dy"] * 2.2 * speed
                elif a == "scroll":
                    scroll -= st["dy"] * 0.15
        for i, w in enumerate(page_sliders[:4]):
            if w["action"] in ("pad:lt", "pad:rt"):
                axes[w["action"][4:]] = max(axes[w["action"][4:]], st["sliders"][i] / 255.0)
        # trigger buttons are analog on the controller
        for a in list(want):
            if a in ("pad:lt", "pad:rt"):
                axes[a[4:]] = 1.0
                want.discard(a)
        # scroll:up / scroll:down: one notch at once, then repeated while held
        held_scroll = {a for a in want if a.startswith("scroll:")}
        want -= held_scroll
        for a in held_scroll:
            if self.scroll_t.get(a, 0) % 8 == 0:
                scroll += 1 if a == "scroll:up" else -1
        self.scroll_t = {a: self.scroll_t.get(a, 0) + 1 for a in held_scroll}

        for a in self.digital - want:
            self._press(a, False)
        for a in sorted(want - self.digital):
            self._press(a, True)
        self.digital = want
        if axes != self.axes:
            self.axes = axes
            self.b.pad_axes(axes)
        if move[0] or move[1]:
            self.frac[0] += move[0]
            self.frac[1] += move[1]
            mx, my = int(self.frac[0]), int(self.frac[1])
            self.frac[0] -= mx
            self.frac[1] -= my
            if mx or my:
                self.b.mouse_move(mx, my)
        if scroll:
            self.b.scroll(scroll)


def parse_state(p):
    if len(p) < 22:
        return None
    keys, tx, ty, touch, page, pressed, sx, sy, dx, dy, s0, s1, s2, s3, batt, sig = struct.unpack_from("<HBBBBIbbhh4BBB", p)
    return {"keys": keys, "tx": tx, "ty": ty, "touch": bool(touch), "page": page, "pressed": pressed, "sx": sx,
            "sy": sy, "dx": dx, "dy": dy, "sliders": [s0, s1, s2, s3], "battery": batt, "signal": sig,
            "mic": p[22] if len(p) > 22 else 0}


# --------------------------------------------------------------------------- the service
class PadService:
    def __init__(self, path=None, backend=None, port=PORT_PC, dsi_port=PORT_DSI, broadcast_ok=True):
        self.path = path or os.path.join(config_dir(), "pad.json")
        self.port, self.dsi_port = port, dsi_port
        self.lock = threading.RLock()
        self.backend = backend
        self.engine = None
        self.sock = None
        self.thread = None
        self.running = False
        self.error = None
        self.seen = {}          # dsi id -> {name, addr, t, pc_id}
        self.pending = {}       # token -> time, while pairing
        self.rx = 0             # packets received from consoles (shown on the page: "is anything arriving?")
        self.local_ips = []
        self.active = None      # {id, addr, token, t, rtt, state, rate, ...}
        self.seq = 0
        self.on_store = None    # store.handle_console, set by the server (console asks for the store)
        self.store_busy = 0     # store requests waiting or running
        self.store_lock = threading.Lock()
        self.load()

    # -- settings
    def load(self):
        try:
            with open(self.path, encoding="utf-8") as f:
                c = json.load(f)
        except (OSError, ValueError):
            c = {}
        gp = sys.platform == "win32"
        c.setdefault("pc_id", random.randint(1, 0xFFFFFFFF))
        c.setdefault("pc_name", (socket.gethostname() or "PC").split(".")[0][:31])
        c.setdefault("devices", {})
        c.setdefault("preset", "gamepad" if gp else "keyboard")
        c.setdefault("buttons", dict(PRESETS[c["preset"]]))
        c.setdefault("layout", default_layout(gp))
        c.setdefault("mouse_speed", 1.0)
        try:
            c["layout"] = validate_layout(c["layout"])
        except (ValueError, TypeError):
            c["layout"] = default_layout(gp)
        self.cfg = c
        self.save()

    def save(self):
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.cfg, f, ensure_ascii=False, indent=1)
        os.replace(tmp, self.path)

    def set_config(self, buttons=None, layout=None, mouse_speed=None, preset=None):
        with self.lock:
            c = self.cfg
            if preset:
                if preset not in PRESETS:
                    raise ValueError("bilinmeyen hazır ayar")
                c["preset"] = preset
                c["buttons"] = dict(PRESETS[preset])
                c["layout"] = validate_layout(preset_layout(preset) or default_layout(preset == "gamepad"))
                if preset == "mouse":
                    c["layout"]["pages"].reverse()        # the mouse page first
            if buttons is not None:
                clean = {}
                for k in KEY_NAMES + [MIC]:
                    a = buttons.get(k, "none")
                    if not valid_action(a, "button"):
                        raise ValueError("geçersiz eylem: %s" % a)
                    clean[k] = a
                c["buttons"] = clean
            if layout is not None:
                c["layout"] = validate_layout(layout)
            if mouse_speed is not None:
                c["mouse_speed"] = max(0.2, min(4.0, float(mouse_speed)))
            self.save()
            if self.engine:
                self.engine.set_config(c)
            if self.active:
                self._send_layout()
        return self.public_config()

    def public_config(self):
        c = self.cfg
        return {"buttons": c["buttons"], "layout": c["layout"], "mouse_speed": c["mouse_speed"], "preset": c["preset"]}

    # -- network
    def start(self):
        with self.lock:
            if self.running:
                return True
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                s.bind(("0.0.0.0", self.port))
                s.settimeout(0.05)
            except OSError as e:
                self.error = str(e)
                return False
            self.sock = s
            self.error = None
            if self.backend is None:
                self.backend = make_backend()
            self.engine = Engine(self.backend)
            self.engine.set_config(self.cfg)
            self.running = True
            self.thread = threading.Thread(target=self._loop, name="pad", daemon=True)
            self.thread.start()
            threading.Thread(target=self._info_loop, name="pad-info", daemon=True).start()
            return True

    def stop(self):
        with self.lock:
            if not self.running:
                return
            if self.active:
                self._send(self.active["addr"], T_BYE, b"", self.active["token"])
            self.running = False
        self.thread.join(1)
        with self.lock:
            if self.engine:
                self.engine.release_all()
            self.active = None
            self.sock.close()
            self.sock = None

    def _send(self, addr, typ, payload, token=0):
        self.seq = (self.seq + 1) & 0xFFFF
        try:
            self.sock.sendto(struct.pack("<4sBBHI", b"HPAD", VERSION, typ, self.seq, token) + payload,
                             (addr[0], self.dsi_port))
        except OSError:
            pass

    def send_store_result(self, op, status, slug):
        """Answer to a store request of the console (T_STORE_RES: op, status, slug[40])."""
        with self.lock:
            a = self.active
            if a:
                self._send(a["addr"], T_STORE_RES, bytes([op & 0xFF, status & 0xFF]) + slug.encode("utf-8")[:39].ljust(40, b"\0"),
                           a["token"])

    def _name32(self):
        return self.cfg["pc_name"].encode("utf-8")[:31].ljust(32, b"\0")

    def _send_layout(self):
        a = self.active
        self._send(a["addr"], T_LAYOUT, encode_layout(self.cfg["layout"]), a["token"])

    def pair(self, code, ip=None):
        """Sends the code typed by the user to the consoles waiting on this network - or straight to
        the address shown on the console's screen (works where the router drops broadcasts)."""
        code = int(code)
        if not 1000 <= code <= 9999:
            raise ValueError("kod 4 haneli olmalı")
        targets = []
        if ip:
            try:
                socket.inet_aton(ip)
                if ip.count(".") != 3:
                    raise OSError
            except OSError:
                raise ValueError("IP adresi geçersiz (ör. 192.168.1.23)")
            targets.append((ip, self.dsi_port))
        now = time.time()
        with self.lock:
            if not self.running:
                raise ValueError("kumanda servisi çalışmıyor")
            for did, d in self.seen.items():
                if now - d["t"] > 5 or (self.active and self.active["id"] == did):
                    continue
                if d["addr"][0] not in [t[0] for t in targets]:
                    targets.append(d["addr"])
            for addr in targets:
                tok = random.randint(1, 0xFFFFFFFF)
                self.pending[tok] = now
                self._send(addr, T_PAIR, struct.pack("<HII", code, self.cfg["pc_id"], tok) + self._name32())
        return len(targets)

    # -- finding each other (broadcasts both ways + addresses we remember)
    def _local_addrs(self):
        ips = set()
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("10.255.255.255", 1))          # no packet is sent: only picks the outgoing interface
            ips.add(s.getsockname()[0])
            s.close()
        except OSError:
            pass
        try:
            for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
                ips.add(info[4][0])
        except OSError:
            pass
        return sorted(ip for ip in ips if not ip.startswith(("127.", "169.254.")))

    def _seek(self):
        if not self.local_ips or random.random() < 0.05:
            self.local_ips = self._local_addrs()
        payload = struct.pack("<I", self.cfg["pc_id"]) + self._name32()
        dests = {"255.255.255.255"} | {ip.rsplit(".", 1)[0] + ".255" for ip in self.local_ips}
        for d in dests:
            self._send((d, self.dsi_port), T_SEEK, payload)
        # paired consoles: knock on their last address too
        for did, dev in self.cfg["devices"].items():
            if dev.get("ip"):
                self._send((dev["ip"], self.dsi_port), T_CONNECT, struct.pack("<II", self.cfg["pc_id"], dev["token"]) + self._name32())

    def forget(self, did):
        with self.lock:
            self.cfg["devices"].pop(str(did), None)
            self.save()
            if self.active and str(self.active["id"]) == str(did):
                self._send(self.active["addr"], T_BYE, b"", self.active["token"])
                self._drop()

    def _drop(self):
        if self.engine:
            self.engine.release_all()
        self.active = None

    def _loop(self):
        last_ping = last_seek = 0
        while True:
            with self.lock:
                if not self.running:
                    return
                sock = self.sock
            try:
                data, addr = sock.recvfrom(2048)
            except socket.timeout:
                data = None
            except OSError:
                data = None
                time.sleep(0.05)
            now = time.time()
            with self.lock:
                if data:
                    self._handle(data, addr, now)
                a = self.active
                if not a and now - last_seek > 1.0:
                    last_seek = now
                    self._seek()
                    self.pending = {t: ts for t, ts in self.pending.items() if now - ts < 30}
                if a and now - a["t"] > 3:
                    self._drop()
                elif a and now - last_ping > 0.5:
                    last_ping = now
                    rtt = a["rtt"] if a["rtt"] is not None else 0xFFFF
                    self._send(a["addr"], T_PING, struct.pack("<IH", int(now * 1000) & 0xFFFFFFFF, min(rtt, 0xFFFE)),
                               a["token"])

    def _info_loop(self):
        info = PcInfo()
        while self.running:
            time.sleep(2)
            with self.lock:
                a = self.active
            if not a:
                continue
            try:
                pkt = info.packet()
            except Exception:  # noqa: BLE001
                continue
            with self.lock:
                if self.active:
                    self._send(self.active["addr"], T_INFO, pkt, self.active["token"])

    def _handle(self, data, addr, now):
        if len(data) < 12:
            return
        magic, ver, typ, seq, token = struct.unpack_from("<4sBBHI", data)
        if magic != b"HPAD" or ver != VERSION:
            return
        p = data[12:]
        self.rx += 1
        devs = self.cfg["devices"]
        if typ == T_HELLO and len(p) >= 32:
            name = p[:24].split(b"\0")[0].decode("utf-8", "replace")
            did, pc_id = struct.unpack_from("<II", p, 24)
            self.seen[did] = {"name": name, "addr": addr, "t": now, "pc_id": pc_id}
            dev = devs.get(str(did))
            if dev and dev.get("name") != name:
                dev["name"] = name
            if dev and pc_id == self.cfg["pc_id"] and not self.active:
                self._send(addr, T_CONNECT, struct.pack("<II", self.cfg["pc_id"], dev["token"]) + self._name32())
        elif typ == T_ACCEPT and len(p) >= 8:
            pc_id, did = struct.unpack_from("<II", p)
            if pc_id != self.cfg["pc_id"] or (self.active and self.active["id"] != did):
                return
            if token in self.pending:
                name = self.seen.get(did, {}).get("name", "DSi")
                devs[str(did)] = {"name": name, "token": token, "paired": int(now), "ip": addr[0]}
                self.pending.pop(token, None)
                self.save()
            dev = devs.get(str(did))
            if not dev or dev["token"] != token:
                return
            if dev.get("ip") != addr[0]:
                dev["ip"] = addr[0]
                self.save()
            fresh = not self.active
            self.active = {"id": did, "addr": addr, "token": token, "t": now, "rtt": None, "state": None,
                           "name": dev["name"], "since": now, "count": 0, "rate": 0, "rate_t": now, "rate_n": 0}
            if fresh:
                self._send_layout()
        elif self.active and addr[0] == self.active["addr"][0] and token == self.active["token"]:
            a = self.active
            a["t"] = now
            if typ == T_STATE:
                st = parse_state(p)
                if st:
                    a["state"] = st
                    a["count"] += 1
                    a["rate_n"] += 1
                    if now - a["rate_t"] >= 1:
                        a["rate"] = round(a["rate_n"] / (now - a["rate_t"]))
                        a["rate_t"], a["rate_n"] = now, 0
                    try:
                        self.engine.update(st)
                    except Exception:  # noqa: BLE001 - one bad packet must not stop the controller
                        import traceback
                        traceback.print_exc()
            elif typ == T_PONG and len(p) >= 4:
                (sent,) = struct.unpack_from("<I", p)
                a["rtt"] = max(0, (int(now * 1000) - sent) & 0xFFFFFFFF)
            elif typ == T_BYE:
                self._drop()
            elif typ == T_STORE and len(p) >= 41 and self.on_store and self.store_busy < 4:
                op = p[0]
                slug = p[1:41].split(b"\0")[0].decode("utf-8", "replace")
                self.store_busy += 1

                def job():
                    try:
                        with self.store_lock:          # one request after the other (catalog, then installs)
                            self.on_store(self, op, slug)
                    finally:
                        self.store_busy -= 1
                threading.Thread(target=job, daemon=True).start()

    # -- for the page
    def status(self):
        with self.lock:
            now = time.time()
            devs = []
            ids = set(self.cfg["devices"]) | {str(k) for k, v in self.seen.items() if now - v["t"] < 5}
            for did in sorted(ids):
                d = self.cfg["devices"].get(did)
                s = self.seen.get(int(did))
                devs.append({"id": did, "name": (d or s or {}).get("name", "DSi"), "paired": bool(d),
                             "online": bool(s and now - s["t"] < 5),
                             "active": bool(self.active and str(self.active["id"]) == did)})
            a = self.active
            active = None
            if a:
                st = a["state"] or {}
                active = {"id": str(a["id"]), "name": a["name"], "rtt": a["rtt"], "rate": a["rate"],
                          "battery": st.get("battery"), "signal": st.get("signal"),
                          "keys": [n for i, n in enumerate(KEY_NAMES) if st.get("keys", 0) >> i & 1],
                          "pressed": [i for i in range(32) if st.get("pressed", 0) >> i & 1],
                          "page": st.get("page", 0), "mic": st.get("mic", 0), "touch": [st.get("tx"), st.get("ty")] if st.get("touch") else None}
            b = self.backend
            return {"running": self.running, "error": self.error, "pc_name": self.cfg["pc_name"],
                    "platform": sys.platform, "devices": devs, "active": active, "pairing": bool(self.pending),
                    "backend": b.name if b else None, "gamepad": bool(b and b.gamepad),
                    "hint": b.hint if b else ("gamepad_missing" if sys.platform == "win32" else
                                              "mac_no_gamepad" if sys.platform == "darwin" else "unsupported"),
                    "local_ips": self.local_ips, "rx": self.rx,
                    "config": self.public_config(), "keys": KEYS, "pad_buttons": PAD_BUTTONS,
                    "presets": list(PRESETS)}


# --------------------------------------------------------------------------- PC info for the top screen
class PcInfo:
    """CPU / memory use, the computer's battery and the song that is playing (macOS: Music /
    Spotify). Everything is best effort; unknown values are left out."""

    def __init__(self):
        self.prev = None
        self.last_music = (0.0, "", "")

    def _cpu(self):
        if sys.platform == "win32":
            import ctypes
            from ctypes import wintypes
            idle, kern, user = wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME()
            if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kern), ctypes.byref(user)):
                return None
            v = lambda f: (f.dwHighDateTime << 32) | f.dwLowDateTime  # noqa: E731
            cur = (v(idle), v(kern) + v(user))
            prev, self.prev = self.prev, cur
            if not prev or cur[1] == prev[1]:
                return None
            return max(0, min(100, int(100 - (cur[0] - prev[0]) * 100 / (cur[1] - prev[1]))))
        try:
            return max(0, min(100, int(os.getloadavg()[0] * 100 / (os.cpu_count() or 1))))
        except (OSError, AttributeError):
            return None

    def _ram(self):
        import subprocess
        try:
            if sys.platform == "win32":
                import ctypes

                class MEM(ctypes.Structure):
                    _fields_ = [("len", ctypes.c_ulong), ("load", ctypes.c_ulong)] + \
                               [(n, ctypes.c_ulonglong) for n in ("tp", "ap", "tf", "af", "tv", "av", "ae")]
                m = MEM()
                m.len = ctypes.sizeof(MEM)
                return int(m.load) if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)) else None
            if sys.platform == "darwin":
                out = subprocess.run(["memory_pressure"], capture_output=True, text=True, timeout=3).stdout
                for line in out.splitlines():
                    if "free percentage" in line:
                        return 100 - int(line.rsplit(":", 1)[1].strip().rstrip("%"))
                return None
            with open("/proc/meminfo") as f:
                d = {l.split(":")[0]: int(l.split()[1]) for l in f if ":" in l}
            return int(100 - d["MemAvailable"] * 100 / d["MemTotal"])
        except Exception:  # noqa: BLE001
            return None

    def _battery(self):
        import subprocess
        try:
            if sys.platform == "win32":
                import ctypes

                class SPS(ctypes.Structure):
                    _fields_ = [("ac", ctypes.c_ubyte), ("flag", ctypes.c_ubyte), ("pct", ctypes.c_ubyte),
                                ("r", ctypes.c_ubyte), ("life", ctypes.c_ulong), ("full", ctypes.c_ulong)]
                st = SPS()
                if ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(st)) and st.pct != 255 and not st.flag & 128:
                    return st.pct, st.ac == 1
                return None
            if sys.platform == "darwin":
                out = subprocess.run(["pmset", "-g", "batt"], capture_output=True, text=True, timeout=3).stdout
                import re
                m = re.search(r"(\d+)%;\s*(\w+)", out)
                if m:
                    return int(m.group(1)), m.group(2) in ("charging", "charged", "AC")
        except Exception:  # noqa: BLE001
            pass
        return None

    def _music(self):
        if sys.platform != "darwin":
            return "", ""
        import subprocess
        now = time.time()
        if now - self.last_music[0] < 5:
            return self.last_music[1], self.last_music[2]
        title = artist = ""
        for app in ("Spotify", "Music"):
            script = ('if application "%s" is running then tell application "%s" to if player state is playing then '
                      'return (name of current track) & "\n" & (artist of current track)') % (app, app)
            try:
                out = subprocess.run(["osascript", "-e", script], capture_output=True, text=True, timeout=3).stdout.strip()
            except Exception:  # noqa: BLE001
                out = ""
            if out:
                title, _, artist = out.partition("\n")
                break
        self.last_music = (now, title, artist)
        return title, artist

    def packet(self):
        cpu, ram, batt = self._cpu(), self._ram(), self._battery()
        title, artist = self._music()
        return struct.pack("<BBBB", 255 if cpu is None else cpu, 255 if ram is None else ram,
                           255 if not batt else batt[0], 1 if batt and batt[1] else 0) + \
            title.encode("utf-8")[:47].ljust(48, b"\0") + artist.encode("utf-8")[:31].ljust(32, b"\0")


_service = None
_service_lock = threading.Lock()


def service():
    global _service
    with _service_lock:
        if _service is None:
            _service = PadService()
        return _service


def write_wifi(sd, ssid, password):
    """Network the console uses for Kumanda (when the DSi's own Wi-Fi settings don't have it)."""
    ssid = (ssid or "").strip()
    password = password or ""
    hyper = os.path.join(sd, "_hyper")
    path = os.path.join(hyper, "wifi.txt")
    key = os.path.join(hyper, "wifi.key")
    if os.path.exists(key):
        os.remove(key)                       # derived again for the new network
    if not ssid:
        if os.path.exists(path):
            os.remove(path)
        return {"removed": True}
    if len(ssid.encode("utf-8")) > 32:
        raise ValueError("ağ adı en fazla 32 bayt olabilir")
    if password and not 8 <= len(password) <= 63:
        raise ValueError("WPA şifresi 8-63 karakter olmalı")
    if "\n" in ssid or "\n" in password or "\r" in ssid or "\r" in password:
        raise ValueError("geçersiz karakter")
    os.makedirs(hyper, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(ssid + "\n" + password + "\n")
    return {"ssid": ssid}


def read_wifi(sd):
    try:
        with open(os.path.join(sd, "_hyper", "wifi.txt"), encoding="utf-8") as f:
            return {"ssid": f.readline().rstrip("\r\n"), "has_password": bool(f.readline().rstrip("\r\n"))}
    except OSError:
        return {"ssid": "", "has_password": False}

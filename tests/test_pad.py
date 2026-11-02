#!/usr/bin/env python3
"""Kumanda end to end: the console's pad.c (host build, build/pad_host) pairs with Studio's
receiver over UDP on this computer, gets the layout and sends input.

    make test-pad
"""
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from studio import pad  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


def unit():
    lay = pad.validate_layout(pad.default_layout(True))
    blob = pad.encode_layout(lay)
    check(blob[:4] == b"HLAY" and blob[5] == 2, "layout blob header")
    check(len(blob) == 8 + 2 * 13 + 9 * 20, "layout blob size")
    check(pad._clean_label("Ağaçöüşığ12345") == "Ağaçöüş", "labels cut on UTF-8 boundaries")
    for bad in ({"pages": []}, {"pages": [{"widgets": [{"type": "laser"}]}]},
                {"pages": [{"widgets": [{"type": "button", "action": "key:nope"}]}]}):
        try:
            pad.validate_layout(bad)
            check(False, "bad layout rejected")
        except ValueError:
            check(True, "bad layout rejected")
    check(pad.valid_action("key:ctrl+shift+s", "button"), "key combo action")
    check(not pad.valid_action("key:ctrl+shift+alt+cmd+s", "button"), "too long combo rejected")
    check(pad.valid_action("keys:wasd", "stick") and not pad.valid_action("keys:wasd", "button"), "stick actions")

    # engine: physical keys, combos, stick to keys
    rb = pad.RecordingBackend()
    e = pad.Engine(rb)
    cfg = {"buttons": dict(pad.PRESETS["keyboard"]), "layout": lay, "mouse_speed": 1.0}
    cfg["buttons"]["X"] = "key:ctrl+c"
    cfg["layout"]["pages"][0]["widgets"][0]["action"] = "keys:wasd"
    e.set_config(cfg)
    base = {"keys": 0, "tx": 0, "ty": 0, "touch": False, "page": 0, "pressed": 0, "sx": 0, "sy": 0, "dx": 0, "dy": 0,
            "sliders": [0] * 4, "battery": 10, "signal": 3}
    e.update(dict(base, keys=1 << 10))           # X
    check(rb.events == [("key", "ctrl", True), ("key", "c", True)], "combo pressed in order")
    rb.events.clear()
    e.update(base)
    check(rb.events == [("key", "c", False), ("key", "ctrl", False)], "combo released in reverse")
    rb.events.clear()
    e.update(dict(base, pressed=1, sx=127, sy=-127))
    check(set(rb.events) == {("key", "d", True), ("key", "w", True)}, "stick as WASD")
    rb.events.clear()
    e.release_all()
    check(set(rb.events) == {("key", "d", False), ("key", "w", False)}, "release_all")
    # blowing into the microphone
    rb.events.clear()
    cfg["buttons"]["MIC"] = "key:space"
    e.set_config(cfg)
    e.update(dict(base, mic=100))
    check(("key", "space", True) in rb.events, "mic blow presses its key")
    e.update(dict(base, mic=10))
    check(("key", "space", False) in rb.events, "quiet again: released")
    for preset in ("presenter", "media", "macro"):
        lay = pad.validate_layout(pad.preset_layout(preset))
        check(len(lay["pages"]) == 2 and pad.encode_layout(lay)[:4] == b"HLAY", "%s layout valid" % preset)
    check(len(pad.PcInfo().packet()) == 84, "PC info packet")


def e2e():
    exe = os.path.join(ROOT, "build", "pad_host")
    if not os.path.exists(exe):
        print("build/pad_host missing (make build/pad_host)")
        return
    tmp = tempfile.mkdtemp()
    os.makedirs(os.path.join(tmp, "sd", "_hyper"))
    rb = pad.RecordingBackend()
    svc = pad.PadService(path=os.path.join(tmp, "pad.json"), backend=rb)
    svc.set_config(preset="gamepad")
    check(svc.start(), "service started")
    env = dict(os.environ, HL_PAD_HOST="127.0.0.1")
    proc = subprocess.Popen([exe, os.path.join(tmp, "sd"), "12"], stdout=subprocess.PIPE, text=True, env=env)
    lines = []

    def reader():
        for ln in proc.stdout:
            lines.append(ln.strip())
    threading.Thread(target=reader, daemon=True).start()
    t0 = time.time()
    while not any(l.startswith("CODE") for l in lines) and time.time() - t0 < 3:
        time.sleep(0.02)
    code = int([l for l in lines if l.startswith("CODE")][0].split()[1])
    # wait for the console's HELLO
    while not svc.status()["devices"] and time.time() - t0 < 4:
        time.sleep(0.05)
    check(svc.status()["devices"] and not svc.status()["devices"][0]["paired"], "console discovered, not paired")
    wrong = 1000 if code != 1000 else 1001
    svc.pair(wrong)
    time.sleep(0.3)
    check(svc.status()["active"] is None, "wrong code does not pair")
    svc.pair(code)
    while not any(l.startswith("CONNECTED") for l in lines) and time.time() - t0 < 6:
        time.sleep(0.05)
    check(any(l == "CONNECTED 2 pages" for l in lines), "console connected and got the layout")
    st = svc.status()
    check(st["devices"][0]["paired"] and st["active"], "paired in Studio")
    while not any(l == "DONE" for l in lines) and time.time() - t0 < 12:
        time.sleep(0.05)
    ev = list(rb.events)
    check(("pad", "b", True) in ev and ("pad", "b", False) in ev, "A -> Xbox B pressed and released")
    axes = [e[1] for e in ev if e[0] == "axes"]
    check(any(a["rx"] > 0.9 for a in axes) and axes[-1]["rx"] == 0, "touch stick -> right stick, back to 0")
    moves = [e for e in ev if e[0] == "move"]
    check(sum(m[1] for m in moves) > 40, "trackpad on page 2 moves the mouse (%d)" % sum(m[1] for m in moves))
    a = svc.status()["active"]
    check(a and a["rate"] >= 30, "about 60 states a second (%s)" % (a and a["rate"]))
    check(a and a["rtt"] is not None and a["rtt"] < 100, "latency measured (%s ms)" % (a and a["rtt"]))
    proc.wait(15)
    time.sleep(0.3)
    check(svc.status()["active"] is None, "BYE ends the connection")
    check(os.path.exists(os.path.join(tmp, "sd", "_hyper", "pad.cfg")), "console remembers the computer")

    # second start: known computer, no code needed
    rb.events.clear()
    proc = subprocess.Popen([exe, os.path.join(tmp, "sd"), "3"], stdout=subprocess.PIPE, text=True, env=env)
    out = proc.communicate(timeout=10)[0]
    check("CONNECTED" in out, "reconnects by itself the next time")
    svc.stop()


def manual_ip():
    """The router drops the console's broadcasts: pairing with the IP shown on the DSi still works."""
    exe = os.path.join(ROOT, "build", "pad_host")
    if not os.path.exists(exe):
        return
    tmp = tempfile.mkdtemp()
    os.makedirs(os.path.join(tmp, "sd", "_hyper"))
    svc = pad.PadService(path=os.path.join(tmp, "pad.json"), backend=pad.RecordingBackend())
    check(svc.start(), "service started (manual IP)")
    env = dict(os.environ, HL_PAD_HOST="192.0.2.1")        # HELLO goes nowhere
    proc = subprocess.Popen([exe, os.path.join(tmp, "sd"), "12"], stdout=subprocess.PIPE, text=True, env=env)
    code = int(proc.stdout.readline().split()[1])
    time.sleep(1.0)
    # the console's own call never arrives, but Studio's search (SEEK) gets an answer
    check(bool(svc.status()["devices"]), "console found by Studio's search although its HELLO is lost")
    check(svc.pair(code, "127.0.0.1") >= 1, "PAIR sent to the typed address")
    t0 = time.time()
    while not svc.status()["active"] and time.time() - t0 < 3:
        time.sleep(0.05)
    st = svc.status()
    check(st["active"] is not None and st["rx"] > 0, "paired through the IP address")
    check(any(d.get("ip") == "127.0.0.1" for d in svc.cfg["devices"].values()), "address remembered for next time")
    # wireless transfer over the same connection
    from studio import wireless
    sd = os.path.join(tmp, "sd")
    os.makedirs(os.path.join(sd, "roms", "NDS"), exist_ok=True)
    with open(os.path.join(sd, "roms", "NDS", "Old.sav"), "wb") as f:
        f.write(b"MY-SAVE")
    game = os.path.join(tmp, "Game.nds")
    with open(game, "wb") as f:
        f.write(os.urandom(300000))
    a = svc.status()["active"]
    tok = svc.active["token"]
    with wireless.Client("127.0.0.1", tok) as c:
        check(c.put("roms/NDS/Game.nds", game) == "copied", "game copied over Wi-Fi")
        check(c.put("roms/NDS/Game.nds", game) == "exists", "same game again: skipped")
        with open(os.path.join(tmp, "evil.sav"), "wb") as f:
            f.write(b"OVERWRITE")
        check(c.put("roms/NDS/Old.sav", os.path.join(tmp, "evil.sav"), overwrite=True) == "skipped", "a save is never replaced")
        check(c.put("../escape.bin", game) == "denied" and c.put("BOOT.NDS", game) == "denied", "writes outside roms/covers refused")
        lst = dict(c.list("roms"))
        check(lst.get("roms/NDS/Game.nds") == 300000 and "roms/NDS/Old.sav" in lst, "list")
        check(c.get("roms/NDS/Old.sav") == b"MY-SAVE", "save read back")
        check(c.get("roms/NDS/Game.nds", 0, 16) == open(game, "rb").read(16), "partial read")
        c.done()
    with open(os.path.join(sd, "roms", "NDS", "Game.nds"), "rb") as f1, open(game, "rb") as f2:
        check(f1.read() == f2.read(), "file arrived intact")
    check(open(os.path.join(sd, "roms", "NDS", "Old.sav"), "rb").read() == b"MY-SAVE", "save untouched")
    try:
        wireless.Client("127.0.0.1", tok ^ 1).list("roms")
        check(False, "wrong token refused")
    except (wireless.XferError, OSError):
        check(True, "wrong token refused")
    try:
        svc.pair(1234, "999.1.1")
        check(False, "bad IP rejected")
    except ValueError:
        check(True, "bad IP rejected")
    proc.wait(20)
    svc.stop()


def store_e2e():
    """Store: the console asks Studio for the catalog, then installs a program over Wi-Fi."""
    exe = os.path.join(ROOT, "build", "pad_host")
    if not os.path.exists(exe):
        return
    from studio import store, pngcodec
    tmp = tempfile.mkdtemp()
    sd = os.path.join(tmp, "sd")
    os.makedirs(os.path.join(sd, "_hyper"))
    # a program and its icon "on the internet" (file:// addresses)
    prog = os.path.join(tmp, "Wordle.nds")
    with open(prog, "wb") as f:
        f.write(b"\0" * 0x200 + os.urandom(50000))
    icon = os.path.join(tmp, "icon.png")
    with open(icon, "wb") as f:
        f.write(pngcodec.encode_rgb(32, 32, bytes([200, 30, 30]) * 1024))
    raw = [{"slug": "wordle-ds", "title": "Wordle DS", "author": "Pk11", "systems": ["DS"], "categories": ["game"],
            "stars": 76, "version": "v3.2.3", "description": "A clone of Wordle", "icon": "file://" + icon,
            "downloads": {"WordleDS.nds": {"url": "file://" + prog, "size": 50512}}},
           {"slug": "only-3ds", "title": "X", "systems": ["3DS"], "downloads": {"x.3dsx": {"url": "file:///x"}}}]
    items = store.normalize(raw)
    check(len(items) == 1 and items[0]["file"] == "WordleDS.nds" and items[0]["category"] == 1, "catalog: DS programs only")
    blob = store.build_catalog_bin(items)
    check(len(blob) == 16 + 2444 and blob[:4] == b"HSTR", "catalog for the console")
    store._mem.update(items=items, t=time.time())          # as if just downloaded
    svc = pad.PadService(path=os.path.join(tmp, "pad.json"), backend=pad.RecordingBackend())
    svc.on_store = store.handle_console
    pad._service = svc
    check(svc.start(), "service started (store)")
    env = dict(os.environ, HL_PAD_HOST="127.0.0.1", HL_PAD_STORE="1")
    proc = subprocess.Popen([exe, sd, "10"], stdout=subprocess.PIPE, text=True, env=env)
    code = int(proc.stdout.readline().split()[1])
    time.sleep(0.5)
    check(svc.pair(code, "127.0.0.1") >= 1, "store console paired")
    out = proc.communicate(timeout=30)[0]
    svc.stop()
    pad._service = None
    check("ASKED" in out, "console asked for the catalog")
    check("CATALOG 1 0" in out, "catalog arrived on the console")
    check("INSTALL wordle-ds" in out and "RESULT 2 0 wordle-ds" in out, "program installed over Wi-Fi (" + out.replace("\n", " | ") + ")")
    got = os.path.join(sd, "roms", "NDS", "Homebrew", "WordleDS.nds")
    check(os.path.isfile(got) and open(got, "rb").read() == open(prog, "rb").read(), "program on the card")
    check("CATALOG 1 1" in out, "console sees it as installed")
    check(store.installed_on_card(sd).get("wordle-ds", {}).get("version") == "v3.2.3", "installed.txt written")


def nearby_e2e():
    """Nearby: the console sends its card and lists the card of another console."""
    exe = os.path.join(ROOT, "build", "pad_host")
    if not os.path.exists(exe):
        return
    tmp = tempfile.mkdtemp()
    sd = os.path.join(tmp, "sd")
    os.makedirs(os.path.join(sd, "_hyper"))
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.bind(("127.0.0.1", 0))
    rx.settimeout(5)
    env = dict(os.environ, HL_PAD_HOST="127.0.0.1", HL_PAD_NEARBY="1", HL_NEARBY_PEER="127.0.0.1:%d" % rx.getsockname()[1])
    proc = subprocess.Popen([exe, sd, "4"], stdout=subprocess.PIPE, text=True, env=env)
    check(proc.stdout.readline().startswith("READY"), "nearby started")
    try:
        data, _ = rx.recvfrom(2000)
    except socket.timeout:
        data = b""
    check(data[:5] == b"HNBR\x01" and data[9:12] == b"Ege", "the console sends its card")
    other = bytearray(b"HNBR\x01") + struct.pack("<I", 99) + "Ayşe".encode().ljust(20, b"\0") + bytes([2, 0]) + \
        "Selam!".encode().ljust(48, b"\0") + "Tetris".encode().ljust(48, b"\0") + struct.pack("<IH", 120, 3)
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for _ in range(3):
        tx.sendto(bytes(other), ("127.0.0.1", 39513))
        time.sleep(0.2)
    out = proc.communicate(timeout=20)[0]
    check("MET Ayşe Selam!" in out, "the other console was met (" + out.replace("\n", " | ") + ")")
    check(os.path.getsize(os.path.join(sd, "nearby.bin")) > 1000, "met people kept on the card")


if __name__ == "__main__":
    unit()
    e2e()
    manual_ip()
    store_e2e()
    nearby_e2e()
    print("ALL OK" if not fails else "%d FAILED" % fails)
    sys.exit(1 if fails else 0)

"""Wireless transfer: games, covers and saves between this computer and a connected console
(source/xfer.c on the DSi, protocol in include/xfer.h). The console must be in PC mode
(the Controller tile) and paired with this Studio."""

import io
import os
import shutil
import socket
import struct
import tempfile
import zipfile

from . import pad

PORT = 39512
OK, SAME, EXISTS, DENIED, NOSPACE, IOERR = range(6)
STATUS = {OK: "copied", SAME: "exists", EXISTS: "skipped", DENIED: "denied", NOSPACE: "no-space", IOERR: "error"}


class XferError(RuntimeError):
    pass


class Client:
    def __init__(self, host, token, timeout=20):
        self.s = socket.create_connection((host, PORT), timeout=timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.s.sendall(b"HXFR" + struct.pack("<I", token))

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def _recv(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                raise XferError("bağlantı koptu")
            buf += chunk
        return bytes(buf)

    @staticmethod
    def _path(rel):
        b = rel.replace("\\", "/").encode("utf-8")
        if len(b) > 200:
            raise XferError("yol çok uzun: " + rel)
        return struct.pack("<H", len(b)) + b

    def put(self, rel, src, overwrite=False, progress=None):
        size = os.path.getsize(src)
        self.s.sendall(b"\x01" + self._path(rel) + struct.pack("<IB", size, 1 if overwrite else 0))
        st = self._recv(1)[0]
        if st != OK:
            return STATUS.get(st, "error")
        sent = 0
        with open(src, "rb") as f:
            while True:
                chunk = f.read(64 * 1024)
                if not chunk:
                    break
                self.s.sendall(chunk)
                sent += len(chunk)
                if progress:
                    progress(sent, size)
        st = self._recv(1)[0]
        return STATUS.get(st, "error")

    def get(self, rel, offset=0, length=0):
        self.s.sendall(b"\x02" + self._path(rel) + struct.pack("<II", offset, length))
        st = self._recv(1)[0]
        n = struct.unpack("<I", self._recv(4))[0]
        if st != OK:
            raise XferError("okunamadı: " + rel)
        return self._recv(n)

    def list(self, folder=""):
        self.s.sendall(b"\x03" + self._path(folder))
        out = []
        while True:
            (n,) = struct.unpack("<H", self._recv(2))
            if not n:
                return out
            path = self._recv(n).decode("utf-8", "replace")
            (size,) = struct.unpack("<I", self._recv(4))
            out.append((path, size))

    def done(self):
        self.s.sendall(b"\x04")
        self._recv(1)


def _connected():
    svc = pad.service()
    with svc.lock:
        a = svc.active
        if not a:
            raise XferError("Konsol bağlı değil: DSi'de Kumanda'yı aç ve bu bilgisayarla eşleştir.")
        return a["addr"][0], a["token"], a["name"]


def status():
    svc = pad.service()
    with svc.lock:
        a = svc.active
        return {"connected": bool(a), "name": a["name"] if a else None}


def send_games(paths, names, mode="flat", progress=None):
    """Places the files like a card install would (archives unpacked, right folder, short names)
    into a staging folder, then copies what came out to the console."""
    from . import roms
    host, token, _ = _connected()
    stage = tempfile.mkdtemp(prefix="hl_wifi_")
    results = []
    try:
        for src, name in zip(paths, names):
            r = roms.place_any(src, stage, name, mode)
            results += r if isinstance(r, list) else [r]
        files = []
        for root, _d, fs in os.walk(stage):
            for f in fs:
                p = os.path.join(root, f)
                files.append((os.path.relpath(p, stage).replace("\\", "/"), p))
        total = sum(os.path.getsize(p) for _r, p in files) or 1
        done = 0
        out = []
        with Client(host, token) as c:
            for rel, p in files:
                size = os.path.getsize(p)

                def prog(sent, _size, base=done, name=rel):
                    if progress:
                        progress({"done": base + sent, "total": total, "name": os.path.basename(name)})
                st = c.put(rel, p, progress=prog)
                out.append({"file": os.path.basename(rel), "status": st})
                done += size
            c.done()
        return {"files": out, "placed": results}
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def backup_saves(target_dir=None, progress=None):
    """Every save of the console into one ZIP (same layout as a card backup, so it can be
    restored with Studio later)."""
    from .extras import default_backup_dir
    from .safety import is_protected
    import datetime
    host, token, name = _connected()
    target_dir = target_dir or default_backup_dir()
    os.makedirs(target_dir, exist_ok=True)
    with Client(host, token) as c:
        files = [(p, s) for p, s in c.list("") if is_protected(p) and not p.lower().startswith("_hyper/save_conflicts/")]
        stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        out = os.path.join(target_dir, "DSi-saves-wifi_%s.zip" % stamp)
        tmp = out + ".part"
        n = 0
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
            for i, (p, size) in enumerate(files):
                if progress:
                    progress({"done": i + 1, "total": len(files), "name": os.path.basename(p)})
                data = c.get(p)
                if len(data) != size:
                    raise XferError("eksik okundu: " + p)
                z.writestr(p, data)
                n += 1
        with zipfile.ZipFile(tmp) as z:
            if z.testzip() is not None:
                raise XferError("yedek doğrulanamadı")
        os.replace(tmp, out)
    return {"count": n, "zip": out}


def send_covers(progress=None):
    """Downloads box art for the console's games (like the Covers page) and copies it over."""
    from . import covers, systems
    host, token, _ = _connected()
    mirror = tempfile.mkdtemp(prefix="hl_wifi_cov_")
    sent = 0
    try:
        with Client(host, token) as c:
            have = {p.lower() for p, _s in c.list("_hyper/covers")}
            games = [(p, s) for p, s in c.list("roms")]
            todo = [(p, systems.system_for(p)) for p, _s in games if systems.system_for(p)]
            for i, (p, sysid) in enumerate(todo):
                if progress:
                    progress({"done": i + 1, "total": len(todo), "name": os.path.basename(p)})
                local = os.path.join(mirror, *p.split("/"))
                os.makedirs(os.path.dirname(local), exist_ok=True)
                with open(local, "wb") as f:
                    if sysid == "nds":           # the header is enough to know the game code
                        f.write(c.get(p, 0, 0x200))
                if ("_hyper/covers/" + covers.cover_name(local, sysid)).lower() in have:
                    continue
                try:
                    covers.fetch_one(mirror, local, sysid)
                except Exception:  # noqa: BLE001 - one missing cover must not stop the rest
                    continue
            cdir = os.path.join(mirror, "_hyper", "covers")
            if os.path.isdir(cdir):
                for f in os.listdir(cdir):
                    if c.put("_hyper/covers/" + f, os.path.join(cdir, f)) == "copied":
                        sent += 1
            c.done()
    finally:
        shutil.rmtree(mirror, ignore_errors=True)
    return {"sent": sent}

"""Action Replay cheat database (usrcheat.dat) on the card.

The console lists the cheats of a game in its options (Y > Cheats) and gives the chosen ones
to nds-bootstrap (source/cheats.c). The file is the usual R4 / TWiLight Menu++ database and
lives where TWiLight Menu++ keeps it, so both menus share it and its choices.
"""

import os
import struct
import urllib.request

from . import STUDIO_VERSION

DB_REL = ("_nds", "TWiLightMenu", "extras", "usrcheat.dat")
HEADER = b"R4 CheatCode"
MAX_SIZE = 96 * 1024 * 1024
# DeadSkullzJr's database (the one TWiLight Menu++ recommends) and a mirror of it
URLS = (
    "https://raw.githubusercontent.com/DeadSkullzJr/NDS-Cheat-Databases/master/Cheat%20Databases/usrcheat.dat",
    "https://raw.githubusercontent.com/DeadSkullzJr/NDS-Cheat-Databases/main/Cheat%20Databases/usrcheat.dat",
    "https://raw.githubusercontent.com/szTheory/NDS-Cheat-Databases/master/Cheat%20Databases/usrcheat.dat",
)
INFO_URL = "https://gbatemp.net/threads/deadskullzjrs-nds-i-cheat-databases.488711/"


def db_path(sd):
    return os.path.join(sd, *DB_REL)


def _count_games(path):
    n = 0
    try:
        with open(path, "rb") as f:
            if f.read(12) != HEADER:
                return None
            f.seek(0x100)
            while True:
                e = f.read(16)
                if len(e) < 16:
                    break
                if struct.unpack_from("<Q", e, 8)[0] == 0:
                    break
                n += 1
    except OSError:
        return None
    return n


def status(sd):
    p = db_path(sd)
    if not os.path.isfile(p):
        return {"present": False, "info_url": INFO_URL}
    return {"present": True, "size": os.path.getsize(p), "games": _count_games(p), "info_url": INFO_URL}


def check(data):
    if len(data) < 0x110 or data[:12] != HEADER:
        return "format"
    if len(data) > MAX_SIZE:
        return "size"
    return None


def install(sd, data):
    """Puts a usrcheat.dat on the card (the choices made in the old file are not carried over)."""
    bad = check(data)
    if bad:
        return {"ok": False, "reason": bad}
    p = db_path(sd)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    tmp = p + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, p)
    return {"ok": True, "size": len(data), "games": _count_games(p)}


def download(sd, progress=None, urls=URLS):
    last = None
    for url in urls:
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "DSplayStudio/" + STUDIO_VERSION})
            with urllib.request.urlopen(req, timeout=60) as r:  # noqa: S310 (fixed https hosts)
                total = int(r.headers.get("Content-Length") or 0)
                buf = bytearray()
                while True:
                    chunk = r.read(256 * 1024)
                    if not chunk:
                        break
                    buf += chunk
                    if len(buf) > MAX_SIZE:
                        raise ValueError("too big")
                    if progress:
                        progress({"done": len(buf), "total": total or len(buf), "name": "usrcheat.dat"})
            res = install(sd, bytes(buf))
            if res.get("ok"):
                res["source"] = url
                return res
            last = res.get("reason")
        except Exception as e:  # noqa: BLE001 - try the next address
            last = str(e)
    return {"ok": False, "reason": "download", "detail": last, "info_url": INFO_URL}


def remove(sd):
    p = db_path(sd)
    if os.path.isfile(p):
        os.remove(p)
    return {"ok": True}

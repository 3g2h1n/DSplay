"""Card snapshots: nothing Studio does may cost anyone a save, and a card can always go back.

Every snapshot is a folder on this computer:

    <Documents>/DSplay Backups/Kart yedekleri/<card name>_<card id>/<date>_<kind>/
        manifest.json     every file that was on the card (size, time) + what the snapshot holds
        saves.zip         all saves of the card (always)
        files/...         copies of card files: the files the installer is going to replace
                          ("install") or every file of the card ("full")

kinds:  install   taken automatically before every installation / update
        original  the first "install" snapshot of a card that did not have DSplay yet
        full      the whole card, on request (also offered before the first installation)
        saves     only saves (before formatting, restoring, removing ...)

A small copy of the "original" snapshot (manifest + the files the installer replaced) is also
kept on the card in _hyper/restore/, so the card can be put back from any computer and the
console can bring back its old menu (_hyper/restore/original_boot.nds) on its own.

Rules that hold everywhere in this module:
  * a save (see is_protected) is never deleted and never overwritten; when a backup holds a
    different version of a save that is on the card, that version is put in
    _hyper/save_conflicts/<date>/ next to it, the card's version stays where it is;
  * every operation that changes the card starts with a fresh saves.zip, and stops when that
    backup cannot be written and read back;
  * files are written to a temporary name, checked and only then put in place.
"""

import datetime
import hashlib
import json
import os
import re
import secrets
import shutil
import zipfile

from . import STUDIO_VERSION
from .systems import SAVE_EXTENSIONS

SNAP_DIR_NAME = "Kart yedekleri"
CARD_ID = "_hyper/card_id.txt"
CARD_RESTORE = "_hyper/restore"
KEEP = {"install": 10, "saves": 20}

# never part of a snapshot, never touched
_IGNORE_DIRS = {"system volume information", "$recycle.bin", ".spotlight-v100", ".trashes", ".fseventsd",
                ".temporaryitems", ".documentrevisions-v100"}
_IGNORE_FILES = {".ds_store", "thumbs.db", "desktop.ini"}


class SafetyError(RuntimeError):
    pass


# --------------------------------------------------------------------------- what is a save
def is_protected(rel):
    """Saves and everything else a player made: never deleted, never overwritten."""
    r = rel.replace("\\", "/").lower()
    parts = r.split("/")
    name = parts[-1]
    if name.endswith(SAVE_EXTENSIONS) or name.endswith((".dsv", ".srm", ".bak", ".sav")):
        return True
    if re.search(r"\.sav\d*$", name) or ".sav." in name:
        return True
    if "saves" in parts[:-1] or "cardsaves" in parts[:-1]:
        return True
    if parts[0] == "_hyper" and len(parts) > 2 and parts[1] in ("profiles", "deleted_saves", "save_conflicts", "backups"):
        return True
    if parts[0] in ("roms", "games") and name.endswith(".txt"):       # notes / cheats next to games
        return True
    if parts[0] == "photo" or parts[0] == "dcim":                    # DSi camera
        return True
    return False


# --------------------------------------------------------------------------- helpers
def backup_root():
    from .extras import default_backup_dir
    return os.path.join(default_backup_dir(), SNAP_DIR_NAME)


def _rel(sd, path):
    return os.path.relpath(path, sd).replace("\\", "/")


def _skip_name(name):
    low = name.lower()
    return low in _IGNORE_FILES or name.startswith("._")


def scan(sd):
    """{rel path: [size, mtime]} of every file on the card."""
    out = {}
    for root, dirs, files in os.walk(sd):
        dirs[:] = [d for d in dirs if d.lower() not in _IGNORE_DIRS and not d.startswith("._")]
        for f in files:
            if _skip_name(f):
                continue
            p = os.path.join(root, f)
            try:
                st = os.stat(p)
            except OSError:
                out[_rel(sd, p)] = [-1, 0]       # unreadable (damaged) entry: listed, not copied
                continue
            out[_rel(sd, p)] = [st.st_size, int(st.st_mtime)]
    return out


def _sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _same(a, b):
    try:
        if os.path.getsize(a) != os.path.getsize(b):
            return False
        return _sha1(a) == _sha1(b)
    except OSError:
        return False


def copy_checked(src, dst):
    """Copies through a temporary file, checks the copy, then puts it in place."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + ".hltmp"
    shutil.copyfile(src, tmp)
    if os.path.getsize(tmp) != os.path.getsize(src) or _sha1(tmp) != _sha1(src):
        os.remove(tmp)
        raise SafetyError("kopya doğrulanamadı: %s" % os.path.basename(src))
    shutil.copystat(src, tmp)
    os.replace(tmp, dst)


def card_id(sd, create=False):
    p = os.path.join(sd, *CARD_ID.split("/"))
    try:
        with open(p, encoding="utf-8") as f:
            v = f.read().strip()
            if re.fullmatch(r"[0-9a-f]{8,32}", v):
                return v
    except OSError:
        pass
    if not create:
        return None
    v = secrets.token_hex(6)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8") as f:
        f.write(v + "\n")
    return v


def _card_label(sd):
    try:
        from .drives import describe_path
        name = (describe_path(sd) or {}).get("name") or ""
    except Exception:  # noqa: BLE001
        name = ""
    name = name or os.path.basename(os.path.normpath(sd)) or "SD"
    return re.sub(r"[^\w\- ]+", "", name).strip()[:24] or "SD"


def card_dir(sd, create=False):
    cid = card_id(sd, create=create)
    if not cid:
        return None
    root = backup_root()
    if os.path.isdir(root):
        for d in os.listdir(root):
            if d.endswith("_" + cid):
                return os.path.join(root, d)
    return os.path.join(root, "%s_%s" % (_card_label(sd), cid))


def install_targets(sd):
    """Card paths (lower case) the installer writes. Mirrors runtime.install()."""
    from . import runtime
    out = {"boot.nds", "hyperlauncher.nds", "_nds/twilightmenu/settings.ini"}
    for _m, rel in runtime.runtime_members():
        out.add(rel.lower())
    return out


def _existing_ci(sd, rel_lower, current):
    """The card's spelling of a path (FAT does not care about upper / lower case)."""
    if not isinstance(current, dict) or "__lower__" not in current:
        for r in current:
            if r.lower() == rel_lower:
                return r
        return None
    return current["__lower__"].get(rel_lower)


def _with_lower(files):
    d = dict(files)
    d["__lower__"] = {r.lower(): r for r in files}
    return d


def _saves_zip(sd, files, out_path):
    """saves.zip of every protected file; read back and checked."""
    saves = sorted(r for r, (size, _t) in files.items() if is_protected(r) and size >= 0)
    skipped = []
    tmp = out_path + ".hltmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for r in saves:
            try:
                z.write(os.path.join(sd, *r.split("/")), r)
            except OSError:
                skipped.append(r)
    with zipfile.ZipFile(tmp) as z:
        bad = z.testzip()
        if bad is not None:
            raise SafetyError("kayıt yedeği okunamadı (%s)" % bad)
        names = set(z.namelist())
    if len(names) != len(saves) - len(skipped):
        raise SafetyError("kayıt yedeği eksik")
    os.replace(tmp, out_path)
    return len(names), skipped


def _need_bytes(files, rels):
    return sum(max(0, files[r][0]) for r in rels) + 16 * 1024 * 1024


# --------------------------------------------------------------------------- snapshot
def snapshot(sd, kind="install", progress=None):
    """Takes a snapshot (see module doc). Raises SafetyError when it cannot be completed."""
    if kind not in ("install", "full", "saves"):
        raise ValueError("kind")
    if not os.path.isdir(sd):
        raise SafetyError("SD kart bulunamadı")
    files = scan(sd)
    cdir = card_dir(sd, create=True)
    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")

    boot = _existing_ci(sd, "boot.nds", files)
    had_hyper = False
    if boot:
        from .runtime import nds_banner_title, is_dsplay_title
        had_hyper = is_dsplay_title(nds_banner_title(os.path.join(sd, boot)))
    first = kind in ("install", "full") and not had_hyper and not _has_kind(cdir, "original")
    real_kind = "original" if first else kind

    if kind == "full":
        to_copy = [r for r, (s, _t) in files.items() if s >= 0]
    elif kind == "install":
        targets = install_targets(sd)
        to_copy = [r for r, (s, _t) in files.items() if s >= 0 and r.lower() in targets]
    else:
        to_copy = []
    need = _need_bytes(files, to_copy) + _need_bytes(files, [r for r in files if is_protected(r)])
    os.makedirs(cdir, exist_ok=True)
    free = shutil.disk_usage(cdir).free
    if free < need:
        raise SafetyError("bilgisayarda yedek için yeterli yer yok (%d MB gerekli)" % (need // (1 << 20)))

    sdir = os.path.join(cdir, "%s_%s" % (stamp, real_kind))
    n = 2
    while os.path.exists(sdir):              # two snapshots in the same second
        sdir = os.path.join(cdir, "%s-%d_%s" % (stamp, n, real_kind))
        n += 1
    work = sdir + ".part"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    try:
        n_saves, skipped = _saves_zip(sd, files, os.path.join(work, "saves.zip"))
        total = len(to_copy)
        copied = 0
        for i, r in enumerate(sorted(to_copy)):
            if progress and (i % 8 == 0 or i == total - 1):
                progress({"step": "backup", "done": i + 1, "total": total, "name": r})
            try:
                copy_checked(os.path.join(sd, *r.split("/")), os.path.join(work, "files", *r.split("/")))
                copied += 1
            except OSError:
                skipped.append(r)
        manifest = {"version": 1, "studio": STUDIO_VERSION, "kind": real_kind, "created": stamp,
                    "card": _card_label(sd), "card_id": card_id(sd), "had_hyperlauncher": had_hyper, "full": kind == "full",
                    "boot": boot, "files": files, "copied": copied, "saves": n_saves, "skipped": skipped}
        with open(os.path.join(work, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump(manifest, f, ensure_ascii=False)
        os.replace(work, sdir)
    except BaseException:
        shutil.rmtree(work, ignore_errors=True)
        raise
    if first:
        _card_copy(sd, sdir, manifest)
    _prune(cdir)
    return _info(sdir, manifest)


def _has_kind(cdir, kind):
    return bool(cdir and os.path.isdir(cdir) and any(d.endswith("_" + kind) for d in os.listdir(cdir)))


def _card_copy(sd, sdir, manifest):
    """The original state also goes on the card (small: only the files the installer replaces)."""
    src = os.path.join(sdir, "files")
    targets = install_targets(sd)
    picked = []
    if os.path.isdir(src):
        for root, _d, fs in os.walk(src):
            for f in fs:
                p = os.path.join(root, f)
                if _rel(src, p).lower() in targets:
                    picked.append(p)
    size = sum(os.path.getsize(p) for p in picked)
    if shutil.disk_usage(sd).free < size + 64 * 1024 * 1024:
        return False
    dst = os.path.join(sd, *CARD_RESTORE.split("/"), "original")
    if os.path.isdir(dst):
        return False                         # never replace the first original
    work = dst + ".part"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for p in picked:
        copy_checked(p, os.path.join(work, "files", os.path.relpath(p, src)))
    m = dict(manifest, full=False)
    with open(os.path.join(work, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(m, f, ensure_ascii=False)
    os.replace(work, dst)
    if manifest.get("boot"):
        b = os.path.join(dst, "files", *manifest["boot"].split("/"))
        if os.path.isfile(b):
            copy_checked(b, os.path.join(sd, *CARD_RESTORE.split("/"), "original_boot.nds"))
    return True


def _prune(cdir):
    snaps = sorted(d for d in os.listdir(cdir) if os.path.isdir(os.path.join(cdir, d)) and not d.endswith(".part"))
    for kind, keep in KEEP.items():
        mine = [d for d in snaps if d.endswith("_" + kind)]
        for d in mine[:-keep]:
            shutil.rmtree(os.path.join(cdir, d), ignore_errors=True)


def _info(sdir, manifest):
    size = 0
    for r, _d, fs in os.walk(sdir):
        for f in fs:
            try:
                size += os.path.getsize(os.path.join(r, f))
            except OSError:
                pass
    return {"id": os.path.basename(sdir), "path": sdir, "kind": manifest.get("kind"), "created": manifest.get("created"),
            "files": len(manifest.get("files", {})), "copied": manifest.get("copied", 0), "saves": manifest.get("saves", 0),
            "skipped": manifest.get("skipped", []), "size": size, "on_card": False, "full": bool(manifest.get("full"))}


def list_snapshots(sd):
    out = []
    cdir = card_dir(sd)
    if cdir and os.path.isdir(cdir):
        for d in sorted(os.listdir(cdir), reverse=True):
            p = os.path.join(cdir, d)
            m = _read_manifest(p)
            if m:
                out.append(_info(p, m))
    p = os.path.join(sd, *CARD_RESTORE.split("/"), "original")
    m = _read_manifest(p)
    if m:
        i = _info(p, m)
        i["id"] = "card:original"
        i["on_card"] = True
        out.append(i)
    return {"card_id": card_id(sd), "dir": cdir, "snapshots": out}


def _read_manifest(p):
    try:
        with open(os.path.join(p, "manifest.json"), encoding="utf-8") as f:
            m = json.load(f)
        return m if isinstance(m, dict) and "files" in m else None
    except (OSError, ValueError):
        return None


def _resolve(sd, snap_id):
    if snap_id == "card:original":
        return os.path.join(sd, *CARD_RESTORE.split("/"), "original")
    if not isinstance(snap_id, str) or not re.fullmatch(r"[\w\-]+", snap_id):
        raise ValueError("geçersiz yedek")
    cdir = card_dir(sd)
    p = os.path.join(cdir or "", snap_id)
    if not cdir or not os.path.isdir(p):
        raise ValueError("yedek bulunamadı")
    return p


# --------------------------------------------------------------------------- going back
def restore(sd, snap_id, progress=None):
    """Puts the card back to the state of a snapshot. Saves are never lost (see module doc)."""
    sdir = _resolve(sd, snap_id)
    m = _read_manifest(sdir)
    if not m:
        raise SafetyError("yedek okunamadı")
    # 0. a fresh copy of today's saves first
    safety = snapshot(sd, "saves")
    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    conflicts_dir = os.path.join(sd, "_hyper", "save_conflicts", stamp)
    report = {"safety": safety["path"], "restored": 0, "removed": 0, "conflicts": [], "kept_saves": [], "errors": []}
    before = {r.lower() for r in m["files"]}
    scanned = scan(sd)
    current = _with_lower(scanned)
    targets = install_targets(sd)
    only_saves = m.get("kind") == "saves"

    # 1. files the installer added since the snapshot (only those, never anything else)
    for r in sorted(scanned, reverse=True):
        low = r.lower()
        if only_saves or low in before or low not in targets or is_protected(r):
            continue
        try:
            os.remove(os.path.join(sd, *r.split("/")))
            report["removed"] += 1
        except OSError as e:
            report["errors"].append("%s: %s" % (r, e))
    if not only_saves:
        _remove_empty_dirs(sd, targets, before)

    # 2. files of the snapshot
    items = []
    fdir = os.path.join(sdir, "files")
    if os.path.isdir(fdir):
        for root, _d, fs in os.walk(fdir):
            for f in fs:
                p = os.path.join(root, f)
                items.append((_rel(fdir, p), p))
    total = len(items)
    for i, (r, src) in enumerate(items):
        if progress and (i % 8 == 0 or i == total - 1):
            progress({"step": "restore", "done": i + 1, "total": total, "name": r})
        cur = _existing_ci(sd, r.lower(), current) or r
        dst = os.path.join(sd, *cur.split("/"))
        try:
            if os.path.exists(dst) and _same(src, dst):
                continue
            if is_protected(r) and os.path.exists(dst):
                copy_checked(src, os.path.join(conflicts_dir, *r.split("/")))
                report["conflicts"].append(r)
                continue
            copy_checked(src, dst)
            report["restored"] += 1
        except (OSError, SafetyError) as e:
            report["errors"].append("%s: %s" % (r, e))

    # 3. saves of the snapshot: missing ones come back, other versions go to save_conflicts
    in_files = {r.lower() for r, _p in items}
    try:
        with zipfile.ZipFile(os.path.join(sdir, "saves.zip")) as z:
            for n in z.namelist():
                parts = n.split("/")
                if n.lower() in in_files or n.startswith("/") or ".." in parts or ":" in parts[0] or n.endswith("/"):
                    continue
                cur = _existing_ci(sd, n.lower(), current)
                if cur:
                    data = z.read(n)
                    p = os.path.join(sd, *cur.split("/"))
                    try:
                        with open(p, "rb") as f:
                            same = f.read() == data
                    except OSError:
                        same = False
                    if same:
                        continue
                    dst = os.path.join(conflicts_dir, *parts)
                    report["conflicts"].append(n)
                else:
                    dst = os.path.join(sd, *parts)
                    report["restored"] += 1
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with z.open(n) as s, open(dst + ".hltmp", "wb") as o:
                    shutil.copyfileobj(s, o)
                os.replace(dst + ".hltmp", dst)
    except (OSError, zipfile.BadZipFile) as e:
        if not isinstance(e, FileNotFoundError):
            report["errors"].append("saves.zip: %s" % e)

    # 4. saves only DSplay knows about (2nd-4th profiles) stay where they are: tell where
    for r in scan(sd):
        if re.match(r"_hyper/profiles/p[1-3]/saves/", r, re.I) and r.lower().endswith(SAVE_EXTENSIONS):
            report["kept_saves"].append(r)
    try:
        os.sync()  # type: ignore[attr-defined]
    except AttributeError:
        pass
    return report


def _remove_empty_dirs(sd, targets, before):
    """Empty folders the installer made (and that did not exist before) go away."""
    keep = set()
    for r in before:
        parts = r.split("/")[:-1]
        for n in range(1, len(parts) + 1):
            keep.add("/".join(parts[:n]))
    made = set()
    for r in targets:
        parts = r.split("/")[:-1]
        for n in range(1, len(parts) + 1):
            made.add("/".join(parts[:n]))
    real = {}
    for root, dirs, _f in os.walk(sd):
        for d in dirs:
            p = os.path.join(root, d)
            real[_rel(sd, p).lower()] = p
    for d in sorted(made - keep, key=lambda x: -x.count("/")):
        p = real.get(d)
        if not p:
            continue
        try:
            if os.path.isdir(p) and not os.listdir(p):
                os.rmdir(p)
        except OSError:
            pass

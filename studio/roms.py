"""Game file inspection and copying to the SD card."""

import os
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile

from .systems import ARCHIVE_EXTENSIONS, MAX_NAME_BYTES, SYSTEMS, system_for

SERIES_PATTERNS = [
    ("Pokemon", r"pok[eé]mon"),
    ("Mario", r"\b(mario|luigi|wario|yoshi)"),
    ("Zelda", r"zelda"),
    ("Dragon Quest", r"dragon\s*quest"),
    ("Final Fantasy", r"final\s*fantasy"),
    ("Castlevania", r"castlevania"),
    ("Kirby", r"kirby"),
    ("Ace Attorney", r"(phoenix\s*wright|ace\s*attorney|gyakuten)"),
    ("Layton", r"layton"),
    ("Sonic", r"sonic"),
]


def _crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def inspect_nds(path):
    """Header facts the UI shows for an .nds file."""
    info = {"code": "", "title": "", "dsi": False, "homebrew": False, "corrupt": "", "rom_size": 0}
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            hdr = f.read(0x200)
            if len(hdr) < 0x200:
                info["corrupt"] = "header"
                return info
            if _crc16(hdr[:0x15E]) != struct.unpack_from("<H", hdr, 0x15E)[0]:
                info["corrupt"] = "header-crc"
            info["code"] = hdr[0x0C:0x10].decode("ascii", "replace").strip("\x00")
            info["dsi"] = bool(hdr[0x12] & 0x02)
            info["homebrew"] = info["code"] in ("####", "PASS", "") or not info["code"].isalnum()
            used = struct.unpack_from("<I", hdr, 0x80)[0]
            info["rom_size"] = used
            if used > 0x10000 and size + 0x10000 < used:
                info["corrupt"] = "truncated"
            boff = struct.unpack_from("<I", hdr, 0x68)[0]
            if 0x200 <= boff < size:
                f.seek(boff + 0x340)
                raw = f.read(0x100)
                lines = [ln.strip() for ln in raw.decode("utf-16le", "ignore").split("\x00")[0].split("\n") if ln.strip()]
                if len(lines) > 1:
                    lines = lines[:-1]  # last line is the publisher
                info["title"] = " ".join(lines)
    except OSError:
        info["corrupt"] = "read"
    return info


def nds_category(name, mode):
    if mode == "series":
        low = name.lower()
        for folder, pat in SERIES_PATTERNS:
            if re.search(pat, low):
                return folder
    elif mode == "alphabet":
        ch = name[:1].upper()
        return ch if ch.isalpha() and ch.isascii() else "#"
    return ""


def destination_folder(sd, name, mode="flat"):
    sysid = system_for(name)
    if not sysid:
        return None
    folder = os.path.join(sd, "roms", SYSTEMS[sysid][0])
    if sysid == "nds":
        cat = nds_category(name, mode)
        if cat:
            folder = os.path.join(folder, cat)
    return folder


def _sanitize(name):
    name = os.path.basename(name.replace("\\", "/"))
    name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name).strip().rstrip(".")
    return name or "game"


def _shorten(name):
    """Keep names within what the console stores (63 bytes) so covers/stats match."""
    if len(name.encode("utf-8")) <= MAX_NAME_BYTES:
        return name
    stem, ext = os.path.splitext(name)
    budget = MAX_NAME_BYTES - len(ext.encode("utf-8"))
    raw = stem.encode("utf-8")[:budget]
    return raw.decode("utf-8", "ignore").rstrip() + ext


def place_file(src_path, sd, original_name, mode="flat", overwrite=False):
    """Copies one game file (and a neighbouring .sav) to the card. Returns a result dict."""
    name = _shorten(_sanitize(original_name))
    folder = destination_folder(sd, name, mode)
    if not folder:
        return {"file": original_name, "status": "skipped", "reason": "unsupported"}
    os.makedirs(folder, exist_ok=True)
    dst = os.path.join(folder, name)
    size = os.path.getsize(src_path)
    if os.path.exists(dst):
        if os.path.getsize(dst) == size:
            return {"file": name, "status": "exists", "dest": os.path.relpath(dst, sd)}
        if not overwrite:
            return {"file": name, "status": "skipped", "reason": "different-file-exists", "dest": os.path.relpath(dst, sd)}
    if shutil.disk_usage(sd).free < size + 4 * 1024 * 1024:
        return {"file": name, "status": "error", "reason": "no-space"}
    tmp = dst + ".part"
    try:
        shutil.copyfile(src_path, tmp)
        os.replace(tmp, dst)
    except OSError as e:
        try:
            os.remove(tmp)
        except OSError:
            pass
        return {"file": name, "status": "error", "reason": str(e)}
    result = {"file": name, "status": "copied", "dest": os.path.relpath(dst, sd).replace("\\", "/")}
    # a save file next to the source travels along - but never replaces a save already on the card
    sav = os.path.splitext(src_path)[0] + ".sav"
    if os.path.isfile(sav):
        dst_sav = os.path.splitext(dst)[0] + ".sav"
        if not os.path.exists(dst_sav):
            shutil.copyfile(sav, dst_sav)
            result["save"] = "copied"
        else:
            result["save"] = "kept"
    return result


def _extract_archive(path, out_dir):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".zip":
        with zipfile.ZipFile(path) as z:
            for m in z.infolist():
                n = m.filename.replace("\\", "/")
                parts = [p for p in n.split("/") if p not in ("", ".")]
                if m.is_dir() or not parts or ".." in parts or n.startswith("/") or "__MACOSX" in parts:
                    continue
                if not system_for(parts[-1]):
                    continue
                target = os.path.join(out_dir, *parts)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with z.open(m) as src, open(target, "wb") as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
        return True
    if ext == ".7z":
        try:
            import py7zr  # type: ignore
            with py7zr.SevenZipFile(path, mode="r") as z:
                z.extractall(path=out_dir)
            return True
        except ImportError:
            pass
    for tool in (["tar", "-xf", path, "-C", out_dir], ["7z", "x", "-y", f"-o{out_dir}", path]):
        if shutil.which(tool[0]):
            try:
                if subprocess.run(tool, capture_output=True, timeout=3600).returncode == 0:
                    return True
            except (OSError, subprocess.TimeoutExpired):
                continue
    return False


def place_any(src_path, sd, original_name, mode="flat", overwrite=False):
    """Like place_file, but also unpacks archives. Returns a list of results."""
    if os.path.splitext(original_name)[1].lower() not in ARCHIVE_EXTENSIONS:
        return [place_file(src_path, sd, original_name, mode, overwrite)]
    tmp = tempfile.mkdtemp(prefix="hyperstudio_")
    try:
        if not _extract_archive(src_path, tmp):
            return [{"file": original_name, "status": "error", "reason": "archive"}]
        results = []
        for root, _, files in os.walk(tmp):
            if "__MACOSX" in root:
                continue
            for f in sorted(files):
                if f.startswith(".") or not system_for(f):
                    continue
                full = os.path.join(root, f)
                if os.path.islink(full):
                    continue
                results.append(place_file(full, sd, f, mode, overwrite))
        if not results:
            results.append({"file": original_name, "status": "skipped", "reason": "empty-archive"})
        return results
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

"""Installs / checks the DSplay runtime on an SD card.

Card layout written by install():

    BOOT.NDS, HyperLauncher.nds        DSplay (Unlaunch boots BOOT.NDS)
    _nds/                              nds-bootstrap, GBARunner2, TWiLight Menu++ emulators
    _gba/bios.bin                      (user supplied) GBA BIOS for GBARunner2
    _hyper/                            DSplay data: settings, library cache, covers, sfx, backups
    roms/<System>/                     games
"""

import datetime
import hashlib
import os
import shutil
import struct
import zipfile

from . import STUDIO_VERSION
from .systems import ROM_FOLDERS, payload_dir

RUNTIME_ZIP = "HyperDSi-Runtime.zip"

# (label, alternatives) - the first existing alternative counts. Mirrors source/launcher.c
COMPONENTS = [
    ("DSplay", ["BOOT.NDS"]),
    ("nds-bootstrap", ["_nds/nds-bootstrap-release.nds", "_nds/nds-bootstrap-nightly.nds"]),
    ("nds-bootstrap (homebrew)", ["_nds/nds-bootstrap-hb-release.nds", "_nds/nds-bootstrap-hb-nightly.nds"]),
    ("GBARunner2", ["_nds/GBARunner2_arm7dldi_dsi.nds", "_nds/GBARunner2_arm7dldi_nodsp_dsi.nds", "_nds/GBARunner2.nds"]),
    ("GameYob (GB/GBC)", ["_nds/TWiLightMenu/emulators/gameyob.nds", "_nds/gameyob.nds"]),
    ("nesDS (NES)", ["_nds/TWiLightMenu/emulators/nesDS.nds", "_nds/nesds.nds"]),
    ("SNEmulDS (SNES)", ["_nds/TWiLightMenu/emulators/SNEmulDS-legacy.nds", "_nds/snemulds.nds"]),
    ("PicoDriveTWL (Mega Drive)", ["_nds/TWiLightMenu/emulators/PicoDriveTWL.nds", "_nds/picodrive.nds"]),
    ("jEnesisDS (Mega Drive)", ["_nds/TWiLightMenu/emulators/jEnesisDS.nds"]),
    ("S8DS (Master System / Game Gear)", ["_nds/TWiLightMenu/emulators/S8DS.nds", "_nds/s8ds.nds"]),
]

GBA_BIOS_PATHS = ["_gba/bios.bin", "gba/bios.bin", "bios.bin"]

# Files from the runtime archive that must never overwrite the user's copy
_KEEP_IF_PRESENT = {"_nds/twilightmenu/settings.ini", "snemul.cfg"}
# Files we install ourselves
_SKIP_MEMBERS = {"boot.nds", "hyperlauncher.nds"}

# sd:/_hyper/settings.bin (SystemSettings in include/common.h)
SETTINGS_MAGIC = 0x35475453
SETTINGS_FMT = "<I11B5x20s"  # magic, theme, language, sfx, volume, brightness, sort, slot, anim_off, hidden, covers, splash, name
LANGS = {"en": 0, "tr": 1, "es": 2, "de": 3, "fr": 4, "az": 5}


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
def _sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _copy_atomic(src, dst):
    tmp = dst + ".tmp"
    shutil.copyfile(src, tmp)
    os.replace(tmp, dst)


def _exists_ci(root, rel):
    """Case-insensitive existence check (FAT is case-insensitive, the host may not be)."""
    cur = root
    for part in rel.split("/"):
        try:
            names = os.listdir(cur)
        except OSError:
            return None
        match = next((n for n in names if n.lower() == part.lower()), None)
        if match is None:
            return None
        cur = os.path.join(cur, match)
    return cur


def is_dsplay_title(title):
    """True for the banner of DSplay (and of HyperLauncher DS, its name before 3.0)."""
    return "DSplay" in (title or "") or "HyperLauncher" in (title or "")


def nds_banner_title(path):
    """English banner title of an .nds file ('' if unreadable)."""
    try:
        with open(path, "rb") as f:
            hdr = f.read(0x200)
            if len(hdr) < 0x200:
                return ""
            off = struct.unpack_from("<I", hdr, 0x68)[0]
            if off < 0x200:
                return ""
            f.seek(off + 0x340)
            raw = f.read(0x100)
        return raw.decode("utf-16le", errors="ignore").split("\x00")[0].replace("\n", " · ").strip()
    except OSError:
        return ""


def payload_status():
    pd = payload_dir()
    boot = os.path.join(pd, "BOOT.NDS")
    rt = os.path.join(pd, RUNTIME_ZIP)
    info = {"boot": os.path.isfile(boot), "runtime": os.path.isfile(rt), "runtime_version": ""}
    if info["runtime"]:
        try:
            with zipfile.ZipFile(rt) as z:
                if "version.txt" in z.namelist():
                    info["runtime_version"] = z.read("version.txt").decode("utf-8", "ignore").strip()
        except zipfile.BadZipFile:
            info["runtime"] = False
    return info


# ---------------------------------------------------------------------------
# status
# ---------------------------------------------------------------------------
def card_status(sd):
    comps = []
    for label, alts in COMPONENTS:
        found = next((a for a in alts if _exists_ci(sd, a)), None)
        comps.append({"name": label, "ok": bool(found), "path": found or alts[0]})

    boot = _exists_ci(sd, "BOOT.NDS")
    boot_info = {"present": bool(boot), "hyper": False, "title": "", "current": False}
    if boot:
        boot_info["title"] = nds_banner_title(boot)
        boot_info["hyper"] = is_dsplay_title(boot_info["title"])
        pl = os.path.join(payload_dir(), "BOOT.NDS")
        if os.path.isfile(pl):
            try:
                boot_info["current"] = _sha1(pl) == _sha1(boot)
            except OSError:
                pass

    bios = next((p for p in GBA_BIOS_PATHS if _exists_ci(sd, p)), None)
    runtime_txt = _exists_ci(sd, "_hyper/runtime.txt")
    rt_version = ""
    if runtime_txt:
        try:
            with open(runtime_txt, encoding="utf-8") as f:
                rt_version = f.read().strip()
        except OSError:
            pass
    stale_cache = 0
    roms = os.path.join(sd, "roms")
    if os.path.isdir(roms):
        for root, _, files in os.walk(roms):
            stale_cache += sum(1 for f in files if f == ".hyper_cache.bin")
    settings = None
    from . import profiles as _profiles
    sp, _slot = _profiles.settings_path(sd)
    if not os.path.isfile(sp):
        sp = _exists_ci(sd, "_hyper/settings.bin")
    if sp:
        try:
            with open(sp, "rb") as f:
                v = struct.unpack(SETTINGS_FMT, f.read(struct.calcsize(SETTINGS_FMT)))
            if v[0] == SETTINGS_MAGIC:
                code = {n: c for c, n in LANGS.items()}.get(v[2], "tr")
                settings = {"theme": v[1], "lang": code, "profile": v[12].split(b"\0")[0].decode("utf-8", "ignore")}
        except (OSError, struct.error):
            pass
    return {
        "settings": settings,
        "components": comps,
        "boot": boot_info,
        "gba_bios": bios,
        "runtime_version": rt_version,
        "stale_cache_files": stale_cache,
    }


# ---------------------------------------------------------------------------
# install
# ---------------------------------------------------------------------------
def _safe_member_path(sd, name):
    norm = name.replace("\\", "/")
    if norm.startswith("/") or ":" in norm.split("/")[0]:
        return None
    parts = [p for p in norm.split("/") if p not in ("", ".")]
    if not parts or ".." in parts:
        return None
    return os.path.join(sd, *parts)


def _write_default_settings(sd, lang, profile, theme):
    old_raw = None
    old_tail = b""
    from . import profiles as _profiles
    path, slot = _profiles.settings_path(sd)
    if slot is not None and profile:
        _profiles.update_profile(sd, slot, name=profile)
    if os.path.exists(path):
        # keep the user's choices, only update what Studio was asked to change
        try:
            with open(path, "rb") as f:
                whole = f.read(4096)
            raw = whole[:struct.calcsize(SETTINGS_FMT)]
            vals = list(struct.unpack(SETTINGS_FMT, raw))
            old_raw = raw
            old_tail = whole[len(raw):]     # newer fields (home buttons ...) set on the console
            if vals[0] != SETTINGS_MAGIC:
                raise ValueError
        except (OSError, struct.error, ValueError):
            vals = None
            old_tail = b""
    else:
        vals = None
    if vals is None:
        # magic, theme, language, sfx_pack, volume, brightness, sort, save_slot, cheats, show_hidden, name
        vals = [SETTINGS_MAGIC, 0, 1, 0, 4, 4, 0, 0, 0, 0, 0, 0, "Oyuncu".encode("utf-8")]
    if lang in LANGS:
        vals[2] = LANGS[lang]
    if theme in (0, 1):
        vals[1] = theme
    if profile:
        name = profile.strip().encode("utf-8")[:19]
        while name:
            try:
                name.decode("utf-8")
                break
            except UnicodeDecodeError:
                name = name[:-1]
        if name:
            vals[12] = name
    data = bytearray(struct.pack(SETTINGS_FMT, *vals))
    if old_raw and len(old_raw) >= 20:
        data[15:20] = old_raw[15:20]     # lid options + home screen layout: set on the console, kept
    data += old_tail
    with open(path, "wb") as f:
        f.write(bytes(data))


TWL_SETTINGS = """[SRLOADER]
ROM_FOLDER = sd:/roms
INITIAL_ROM_FOLDER = sd:/roms
SHOW_DIRECTORIES = 1
SHOW_BOX_ART = 1
DSI_SPLASH = 0
"""


def _member_wanted(m):
    low = m.filename.replace("\\", "/").lower()
    if m.is_dir() or low.startswith("__macosx/") or low.endswith(".ds_store") or "coleco" in low:
        return False
    return low not in _SKIP_MEMBERS and low != "version.txt"


def runtime_members():
    """(zip member, card path) of every runtime file install() writes."""
    rt_zip = os.path.join(payload_dir(), RUNTIME_ZIP)
    if not os.path.isfile(rt_zip):
        return []
    out = []
    with zipfile.ZipFile(rt_zip) as z:
        for m in z.infolist():
            if not _member_wanted(m):
                continue
            parts = [p for p in m.filename.replace("\\", "/").split("/") if p not in ("", ".")]
            if not parts or ".." in parts or ":" in parts[0]:
                continue
            out.append((m, "/".join(parts)))
    return out


def install(sd, lang="tr", profile="", theme=None, progress=None, backup="install"):
    """Installs everything DSplay needs. Returns a report dict.

    Nothing is changed before a snapshot of the card (safety.snapshot: all saves + every file
    the installer replaces, or the whole card with backup="full") was written and checked."""
    def step(msg):
        if progress:
            progress(msg)

    if not os.path.isdir(sd):
        raise RuntimeError("SD kart bulunamadı.")
    pd = payload_dir()
    boot_src = os.path.join(pd, "BOOT.NDS")
    rt_zip = os.path.join(pd, RUNTIME_ZIP)
    if not os.path.isfile(boot_src):
        raise RuntimeError("assets/payloads/BOOT.NDS bulunamadı (önce 'make' ile derleyin).")
    if not os.path.isfile(rt_zip):
        raise RuntimeError(f"assets/payloads/{RUNTIME_ZIP} bulunamadı.")

    from . import safety
    step("backup")
    snap = safety.snapshot(sd, "full" if backup == "full" else "install", progress=progress)
    report = {"backed_up": [], "extracted": 0, "kept": [], "removed_stale": 0, "snapshot": snap}
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backups = os.path.join(sd, "_hyper", "backups")
    os.makedirs(backups, exist_ok=True)

    # 1. keep a copy of whatever BOOT.NDS was there (TWiLight Menu++, an older DSplay, ...)
    old_boot = _exists_ci(sd, "BOOT.NDS")
    if old_boot:
        try:
            if _sha1(old_boot) != _sha1(boot_src):
                title = nds_banner_title(old_boot)
                if not is_dsplay_title(title):
                    dst = os.path.join(backups, f"BOOT.NDS.{stamp}.bak")
                    shutil.copyfile(old_boot, dst)
                    report["backed_up"].append(os.path.relpath(dst, sd))
        except OSError:
            pass

    # 2. free space check
    with zipfile.ZipFile(rt_zip) as z:
        members = z.infolist()
        need = sum(m.file_size for m in members) + os.path.getsize(boot_src) * 2
    free = shutil.disk_usage(sd).free
    if free < need:
        raise RuntimeError(f"SD kartta yeterli boş alan yok ({need // (1024 * 1024)} MB gerekli).")

    # 3. runtime (nds-bootstrap, GBARunner2, emulators)
    step("runtime")
    with zipfile.ZipFile(rt_zip) as z:
        for m in members:
            name = m.filename
            low = name.replace("\\", "/").lower()
            if not _member_wanted(m):
                continue
            dst = _safe_member_path(sd, name)
            if not dst:
                continue
            if low in _KEEP_IF_PRESENT and _exists_ci(sd, low):
                report["kept"].append(name)
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            tmp = dst + ".tmp"
            with z.open(m) as src, open(tmp, "wb") as out:
                shutil.copyfileobj(src, out, 1 << 20)
            os.replace(tmp, dst)
            report["extracted"] += 1
        version = z.read("version.txt").decode("utf-8", "ignore").strip() if "version.txt" in z.namelist() else ""

    # 4. DSplay itself
    step("boot")
    for name in ("BOOT.NDS", "HyperLauncher.nds"):
        existing = _exists_ci(sd, name)
        _copy_atomic(boot_src, existing or os.path.join(sd, name))

    # 5. folders
    for sub in ["_hyper/covers", "_hyper/sfx", "_gba"] + [f"roms/{f}" for f in ROM_FOLDERS]:
        os.makedirs(os.path.join(sd, *sub.split("/")), exist_ok=True)

    # 6. leftovers of the previous (python-generated) cache format confuse nothing but waste space
    roms = os.path.join(sd, "roms")
    for root, _, files in os.walk(roms):
        for f in files:
            if f == ".hyper_cache.bin":
                try:
                    os.remove(os.path.join(root, f))
                    report["removed_stale"] += 1
                except OSError:
                    pass

    # 7. TWiLight Menu++ fallback settings (only if the user has none) + DSplay settings
    twl = os.path.join(sd, "_nds", "TWiLightMenu", "settings.ini")
    if not _exists_ci(sd, "_nds/TWiLightMenu/settings.ini"):
        os.makedirs(os.path.dirname(twl), exist_ok=True)
        with open(twl, "w", encoding="utf-8") as f:
            f.write(TWL_SETTINGS)
    _write_default_settings(sd, lang, profile, theme)

    with open(os.path.join(sd, "_hyper", "runtime.txt"), "w", encoding="utf-8") as f:
        f.write(f"DSplay Studio {STUDIO_VERSION}\n{version}\ninstalled {stamp}\n")

    try:
        os.sync()  # type: ignore[attr-defined]
    except AttributeError:
        pass
    step("done")
    report["status"] = card_status(sd)
    report["runtime_version"] = version
    return report


def update_settings(sd, lang=None, profile=None, theme=None):
    os.makedirs(os.path.join(sd, "_hyper"), exist_ok=True)
    _write_default_settings(sd, lang, profile, theme)

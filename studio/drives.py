"""SD card detection and formatting (macOS, Windows, Linux).

Safety rules
------------
* Only removable / external media are listed. Internal disks, disk images and the
  system drive never appear.
* Formatting is only offered for media the OS reports as removable, and the server
  additionally requires the user to type the volume name to confirm.
* On macOS the whole card is re-partitioned as MBR + FAT32 (the DSi cannot read GPT
  or exFAT). On Windows the volume is formatted FAT32 with 32 KB clusters.
"""

import os
import plistlib
import re
import shutil
import subprocess
import sys
import time

FAT32_LABEL = "HYPERDSI"
WIN_FAT32_LIMIT = 32 * 1024 ** 3


def _run(cmd, timeout=60):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=timeout)


def _space(path):
    try:
        u = shutil.disk_usage(path)
        return u.total, u.free
    except OSError:
        return 0, 0


# ---------------------------------------------------------------------------
# macOS
# ---------------------------------------------------------------------------
def _mac_info(target):
    try:
        r = _run(["diskutil", "info", "-plist", target], timeout=20)
        if r.returncode != 0:
            return None
        return plistlib.loads(r.stdout.encode("utf-8"))
    except Exception:
        return None


def _mac_is_candidate(info):
    proto = str(info.get("BusProtocol", "") or info.get("Protocol", ""))
    if "Disk Image" in proto or info.get("VirtualOrPhysical") == "Virtual":
        return False, False
    if info.get("MountPoint") in ("/", "/System/Volumes/Data") or info.get("SystemImage"):
        return False, False
    sd_slot = "Secure Digital" in proto
    # card readers report removable *media*; external hard disks are only "ejectable"
    removable_media = bool(info.get("RemovableMedia") or info.get("Removable"))
    internal = bool(info.get("Internal"))
    listed = sd_slot or removable_media or not internal
    formattable = sd_slot or removable_media
    return listed, formattable


def _mac_list():
    out = []
    root = "/Volumes"
    if not os.path.isdir(root):
        return out
    for name in sorted(os.listdir(root)):
        path = os.path.join(root, name)
        if not os.path.isdir(path) or os.path.islink(path):
            continue
        info = _mac_info(path)
        if not info:
            continue
        listed, formattable = _mac_is_candidate(info)
        if not listed:
            continue
        total, free = _space(path)
        fs = str(info.get("FilesystemType", "") or "").lower()
        fs_name = str(info.get("FilesystemName", "") or info.get("FilesystemUserVisibleName", "") or fs)
        out.append({
            "name": info.get("VolumeName") or name,
            "path": path,
            "device": info.get("DeviceIdentifier", ""),
            "whole_disk": info.get("ParentWholeDisk", ""),
            "total": total,
            "free": free,
            "fs": fs_name,
            "fat": fs == "msdos" and "32" in fs_name,
            "fat_any": fs == "msdos",
            "formattable": formattable,
            "protocol": str(info.get("BusProtocol", "") or ""),
        })
    return out


def _mac_format(vol):
    whole = vol.get("whole_disk") or ""
    if not re.fullmatch(r"disk\d+", whole):
        return False, "Disk kimliği okunamadı."
    info = _mac_info(whole)
    if not info:
        return False, "Disk bilgisi okunamadı."
    _, formattable = _mac_is_candidate(info)
    if not formattable:
        return False, "Bu disk çıkarılabilir bir SD kart olarak görünmüyor; güvenlik için formatlanmadı."
    size = int(info.get("TotalSize") or info.get("Size") or 0)
    if size > 2 * 1024 ** 4:
        return False, "Disk 2 TB'tan büyük; bir SD kart gibi görünmüyor."
    # MBR + FAT32: the only layout the DSi SD slot understands
    r = _run(["diskutil", "eraseDisk", "FAT32", FAT32_LABEL, "MBRFormat", "/dev/" + whole], timeout=600)
    if r.returncode != 0:
        return False, (r.stderr or r.stdout).strip() or "diskutil hata verdi."
    # find the new mount point
    new_path = os.path.join("/Volumes", FAT32_LABEL)
    for _ in range(20):
        if os.path.isdir(new_path):
            break
        time.sleep(0.5)
    return True, new_path


def _mac_eject(vol):
    path = vol["path"]
    if shutil.which("dot_clean"):
        _run(["dot_clean", "-m", path], timeout=120)  # merge / remove ._ AppleDouble files
    r = _run(["diskutil", "eject", path], timeout=60)
    return r.returncode == 0, (r.stderr or r.stdout).strip()


# ---------------------------------------------------------------------------
# Windows
# ---------------------------------------------------------------------------
def _win_list():
    out = []
    try:
        import ctypes
        from ctypes import wintypes
        k32 = ctypes.windll.kernel32  # type: ignore[attr-defined]
    except Exception:
        return out
    system_drive = os.environ.get("SystemDrive", "C:").upper()[:1]
    bus_types = None
    mask = k32.GetLogicalDrives()
    for i in range(26):
        if not mask & (1 << i):
            continue
        letter = chr(65 + i)
        if letter == system_drive:
            continue
        root = letter + ":\\"
        dtype = k32.GetDriveTypeW(root)
        bus = ""
        if dtype == 3:  # DRIVE_FIXED: many laptop SD slots (Realtek etc.) report this
            if bus_types is None:
                bus_types = _win_bus_types()
            bus = bus_types.get(letter, "")
            if bus not in ("SD", "MMC", "USB"):
                continue
        elif dtype != 2:  # DRIVE_REMOVABLE
            continue
        label = ctypes.create_unicode_buffer(261)
        fsname = ctypes.create_unicode_buffer(261)
        serial = wintypes.DWORD()
        maxlen = wintypes.DWORD()
        flags = wintypes.DWORD()
        ok = k32.GetVolumeInformationW(root, label, 261, ctypes.byref(serial), ctypes.byref(maxlen),
                                       ctypes.byref(flags), fsname, 261)
        total, free = _space(root) if ok else (0, 0)
        fs = fsname.value if ok else ""
        out.append({
            "name": (label.value or "SD") + f" ({letter}:)",
            "label": label.value,
            "path": root,
            "letter": letter,
            "device": letter + ":",
            "total": total,
            "free": free,
            "fs": fs or "RAW",
            "fat": fs.upper() == "FAT32",
            "fat_any": fs.upper().startswith("FAT"),
            # fixed USB disks (external hard drives) can be set up but are never formatted
            "formattable": dtype == 2 or bus in ("SD", "MMC"),
            "protocol": bus or "removable",
        })
    return out


def _win_bus_types():
    """{'E': 'SD', 'F': 'USB', ...} for every lettered partition (PowerShell Storage module)."""
    ps = ["powershell", "-NoProfile", "-NonInteractive", "-Command",
          "Get-Partition | Where-Object DriveLetter | ForEach-Object { "
          "\"$($_.DriveLetter)=$((Get-Disk -Number $_.DiskNumber).BusType)\" }"]
    try:
        r = _run(ps, timeout=20)
    except Exception:  # noqa: BLE001
        return {}
    res = {}
    for line in r.stdout.splitlines():
        if "=" in line:
            k, v = line.strip().split("=", 1)
            if len(k) == 1:
                res[k.upper()] = v.strip().upper()
    return res


def _win_format(vol):
    letter = str(vol.get("letter", ""))
    if not re.fullmatch(r"[A-Z]", letter):
        return False, "Geçersiz sürücü harfi."
    if letter == os.environ.get("SystemDrive", "C:").upper()[:1]:
        return False, "Sistem sürücüsü formatlanamaz."
    if vol.get("total", 0) > WIN_FAT32_LIMIT:
        return False, ("Windows, 32 GB'tan büyük kartları FAT32 olarak formatlayamıyor. "
                       "Kartı 'FAT32 Format' (guiformat) aracıyla 32 KB küme boyutunda formatlayın, "
                       "sonra 'Kurulum' adımıyla devam edin.")
    ps = ["powershell", "-NoProfile", "-NonInteractive", "-Command",
          f"Format-Volume -DriveLetter {letter} -FileSystem FAT32 -AllocationUnitSize 32768 "
          f"-NewFileSystemLabel {FAT32_LABEL} -Confirm:$false -Force -ErrorAction Stop | Out-Null; 'OK'"]
    try:
        r = _run(ps, timeout=900)
        if r.returncode == 0 and "OK" in r.stdout:
            return True, vol["path"]
        err = (r.stderr or r.stdout).strip()
    except Exception as e:  # noqa: BLE001
        err = str(e)
    if "Access" in err or "erişim" in err.lower() or "administrator" in err.lower():
        err = "Formatlamak için DSplay Studio'yu 'Yönetici olarak çalıştır' ile açın. (" + err[:200] + ")"
    return False, err or "Format-Volume başarısız oldu."


def _win_eject(vol):
    letter = str(vol.get("letter", ""))
    if not re.fullmatch(r"[A-Z]", letter):
        return False, "Geçersiz sürücü."
    ps = ["powershell", "-NoProfile", "-NonInteractive", "-Command",
          f"(New-Object -ComObject Shell.Application).Namespace(17).ParseName('{letter}:').InvokeVerb('Eject')"]
    r = _run(ps, timeout=60)
    return r.returncode == 0, (r.stderr or "").strip()


# ---------------------------------------------------------------------------
# Linux (listing + eject only)
# ---------------------------------------------------------------------------
def _linux_list():
    out = []
    try:
        with open("/proc/mounts", encoding="utf-8") as f:
            mounts = [ln.split() for ln in f]
    except OSError:
        return out
    for dev, mnt, fs, *_ in mounts:
        mnt = mnt.replace("\\040", " ")
        if not (mnt.startswith("/media/") or mnt.startswith("/run/media/") or mnt.startswith("/mnt/")):
            continue
        if not dev.startswith("/dev/"):
            continue
        base = re.sub(r"(p?\d+)$", "", os.path.basename(dev))
        removable = False
        try:
            with open(f"/sys/block/{base}/removable") as rf:
                removable = rf.read().strip() == "1"
        except OSError:
            removable = base.startswith("mmcblk") or base.startswith("sd")
        total, free = _space(mnt)
        out.append({
            "name": os.path.basename(mnt), "path": mnt, "device": dev, "total": total, "free": free,
            "fs": fs, "fat": fs == "vfat", "fat_any": fs in ("vfat", "msdos"),
            "formattable": False, "protocol": "removable" if removable else "external",
        })
    return out


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------
def list_drives():
    if sys.platform == "darwin":
        return _mac_list()
    if sys.platform == "win32":
        return _win_list()
    return _linux_list()


def describe_path(path):
    """Drive info for any folder (manual selection / testing)."""
    path = os.path.abspath(path)
    for d in list_drives():
        if os.path.normcase(os.path.abspath(d["path"])) == os.path.normcase(path):
            return d
    total, free = _space(path)
    return {"name": os.path.basename(path.rstrip("\\/")) or path, "path": path, "device": "", "total": total,
            "free": free, "fs": "?", "fat": None, "fat_any": None, "formattable": False, "protocol": "folder"}


def find_drive(path):
    for d in list_drives():
        if os.path.normcase(os.path.abspath(d["path"])) == os.path.normcase(os.path.abspath(path)):
            return d
    return None


def format_card(path):
    """Returns (ok, new_path_or_error)."""
    vol = find_drive(path)
    if not vol or not vol.get("formattable"):
        return False, "Bu sürücü formatlanabilir bir SD kart olarak algılanmadı."
    if sys.platform == "darwin":
        return _mac_format(vol)
    if sys.platform == "win32":
        return _win_format(vol)
    return False, "Linux'ta formatlama desteklenmiyor; kartı FAT32 (MBR) olarak kendiniz biçimlendirin."


def eject_card(path):
    vol = find_drive(path)
    if not vol:
        return False, "Sürücü bulunamadı."
    try:
        os.sync()  # type: ignore[attr-defined]
    except AttributeError:
        pass
    if sys.platform == "darwin":
        return _mac_eject(vol)
    if sys.platform == "win32":
        return _win_eject(vol)
    r = _run(["umount", vol["path"]])
    return r.returncode == 0, (r.stderr or "").strip()

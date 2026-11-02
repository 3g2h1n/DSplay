"""File-type rules shared by every Studio module.

Mirrors source/romdb.c (extension -> system) and source/library.c (which folders the
HOME Menu scans) so the desktop app and the console always agree.
"""

import os
import sys

# system id -> (SD folder under /roms, display name)
SYSTEMS = {
    "nds": ("NDS", "Nintendo DS"),
    "gba": ("GBA", "Game Boy Advance"),
    "gb": ("GameBoy", "Game Boy"),
    "gbc": ("GameBoy", "Game Boy Color"),
    "nes": ("NES", "NES"),
    "snes": ("SNES", "Super Nintendo"),
    "md": ("Genesis", "Mega Drive / Genesis"),
    "sms": ("MasterSystem", "Master System"),
    "gg": ("GameGear", "Game Gear"),
}

EXTENSIONS = {
    ".nds": "nds", ".dsi": "nds", ".srl": "nds", ".ids": "nds",
    ".gba": "gba", ".agb": "gba",
    ".gb": "gb", ".sgb": "gb",
    ".gbc": "gbc",
    ".nes": "nes", ".fds": "nes",
    ".sfc": "snes", ".smc": "snes",
    ".gen": "md", ".md": "md", ".bin": "md",
    ".sms": "sms",
    ".gg": "gg",
}

ARCHIVE_EXTENSIONS = (".zip", ".7z", ".rar", ".tar", ".gz", ".tgz")
SAVE_EXTENSIONS = (".sav", ".dsv", ".srm", ".sav0", ".sav1", ".sav2", ".sav3", ".sav4",
                   ".sav5", ".sav6", ".sav7", ".sav8", ".sav9")

ROM_FOLDERS = sorted({v[0] for v in SYSTEMS.values()})

# Same skip rules as source/library.c
_SKIP_DIRS = {"saves", "system volume information", "nintendo 3ds", "luma", "title", "ticket",
              "private", "shared1", "sys", "photo", "dcim", "gm9", "$recycle.bin"}
_SKIP_ROOT_FILES = {"boot.nds", "hyperlauncher.nds", "ntrboot.nds", "hiya.dsi", "boot.firm", "bios.bin"}

# The console stores at most 63 bytes of a file name (GameItem.filename[64])
MAX_NAME_BYTES = 63
MAX_LIBRARY_ITEMS = 1024


def system_for(name):
    return EXTENSIONS.get(os.path.splitext(name)[1].lower())


def skip_dir(name):
    return name.startswith(".") or name.startswith("_") or name.lower() in _SKIP_DIRS


def console_filename(name):
    """The file name as the console stores it (truncated to 63 UTF-8 bytes)."""
    raw = name.encode("utf-8")[:MAX_NAME_BYTES]
    return raw.decode("utf-8", errors="ignore")


def iter_library(sd_path):
    """Yields (path, system) for every title the HOME Menu will list.

    Scans /roms and /games recursively (4 levels) and loose files in the card root.
    """
    def walk(folder, depth, recursive):
        try:
            entries = list(os.scandir(folder))
        except OSError:
            return
        for e in entries:
            if e.name.startswith("."):
                continue
            try:
                is_dir = e.is_dir()
            except OSError:
                continue
            if is_dir:
                if recursive and depth < 4 and not skip_dir(e.name):
                    yield from walk(e.path, depth + 1, True)
                continue
            if depth == 0 and not recursive and e.name.lower() in _SKIP_ROOT_FILES:
                continue
            sysid = system_for(e.name)
            if sysid and e.name.lower() != "usrcheat.dat":
                yield e.path, sysid

    for sub in ("roms", "games"):
        p = os.path.join(sd_path, sub)
        if os.path.isdir(p):
            yield from walk(p, 0, True)
    yield from walk(sd_path, 0, False)


def resource_root():
    """Project root (or the PyInstaller bundle root)."""
    if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
        return sys._MEIPASS  # type: ignore[attr-defined]
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def payload_dir():
    return os.path.join(resource_root(), "assets", "payloads")

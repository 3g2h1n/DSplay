#!/usr/bin/env python3
"""Packages DSplay Studio as a stand-alone app (.app on macOS, .exe folder on Windows).

    python3 -m pip install pyinstaller
    python3 tools/build_standalone.py

Output: dist/DSplayStudio/  (users do not need Python installed)
"""

import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def main():
    try:
        import PyInstaller  # noqa: F401
    except ImportError:
        sys.exit("PyInstaller is not installed:  python3 -m pip install pyinstaller")
    sep = ";" if sys.platform == "win32" else ":"
    payloads = os.path.join(ROOT, "assets", "payloads")
    if not os.path.isfile(os.path.join(payloads, "BOOT.NDS")):
        sys.exit("assets/payloads/BOOT.NDS is missing - run 'make' first.")
    cmd = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--onedir", "--windowed",
           "--name", "DSplayStudio",
           "--distpath", os.path.join(ROOT, "dist"), "--workpath", os.path.join(ROOT, "build", "pyinstaller"),
           "--add-data", f"{os.path.join(ROOT, 'studio', 'web')}{sep}studio/web",
           "--add-data", f"{payloads}{sep}assets/payloads",
           os.path.join(ROOT, "launcher.py")]
    print(" ".join(cmd))
    sys.exit(subprocess.call(cmd))


if __name__ == "__main__":
    main()

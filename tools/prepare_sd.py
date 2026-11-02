#!/usr/bin/env python3
"""DSplay Studio from the command line (no browser needed).

Examples:
  python3 tools/prepare_sd.py --sd /Volumes/HYPERDSI --install
  python3 tools/prepare_sd.py --sd /Volumes/HYPERDSI --add ~/ROMs/*.nds --covers
  python3 tools/prepare_sd.py --sd E:\\ --bios ~/gba_bios.bin --sfx ~/my_sounds
  python3 tools/prepare_sd.py --sd /Volumes/HYPERDSI --status
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from studio import covers, extras, roms, runtime  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sd", required=True, help="SD card root (e.g. /Volumes/HYPERDSI or E:\\)")
    ap.add_argument("--install", action="store_true", help="install / update DSplay and the emulators")
    ap.add_argument("--lang", default="tr", choices=sorted(runtime.LANGS), help="console language for a new card")
    ap.add_argument("--profile", default="", help="profile name shown on the HOME Menu")
    ap.add_argument("--add", nargs="*", default=[], help="game files / archives to copy")
    ap.add_argument("--mode", default="flat", choices=["flat", "series", "alphabet"], help="DS sub-folders")
    ap.add_argument("--covers", action="store_true", help="download missing box art")
    ap.add_argument("--bios", help="GBA BIOS (16 KB) to install as _gba/bios.bin")
    ap.add_argument("--sfx", help="folder with tick.wav, decide.wav, ... (custom menu sounds)")
    ap.add_argument("--backup", action="store_true", help="zip all save files to Documents/DSplay Backups")
    ap.add_argument("--status", action="store_true", help="print what is installed on the card")
    a = ap.parse_args()

    sd = os.path.abspath(a.sd)
    if not os.path.isdir(sd):
        sys.exit(f"SD card not found: {sd}")

    if a.install:
        rep = runtime.install(sd, a.lang, a.profile, progress=lambda s: print("  ..", s))
        print(f"[install] {rep['extracted']} files, runtime: {rep['runtime_version'].splitlines()[:2]}")
        for b in rep["backed_up"]:
            print("[install] previous BOOT.NDS saved as", b)
    for path in a.add:
        for r in roms.place_any(path, sd, os.path.basename(path), a.mode):
            print(f"[games] {r['status']:8} {r['file']}  {r.get('dest', r.get('reason', ''))}")
    if a.bios:
        with open(a.bios, "rb") as f:
            r = extras.install_gba_bios(sd, f.read())
        print("[bios]", "ok" if r["ok"] else "rejected", "(verified)" if r.get("verified") else "")
    if a.sfx:
        for ev in extras.SFX_EVENTS:
            p = os.path.join(a.sfx, ev + ".wav")
            if os.path.isfile(p):
                with open(p, "rb") as f:
                    print(f"[sfx] {ev}.wav ->", extras.install_sfx(sd, ev, f.read()))
    if a.covers:
        r = covers.sync(sd, progress=lambda d, t, n: print(f"\r[covers] {d}/{t}", end="", flush=True))
        print(f"\n[covers] {r['downloaded']} downloaded, {r['exists']} present, {r['missing']} not found")
    if a.backup:
        r = extras.backup_saves(sd)
        print("[backup]", r["count"], "files ->", r["zip"])
    if a.status or not any([a.install, a.add, a.bios, a.sfx, a.covers, a.backup]):
        st = runtime.card_status(sd)
        st["library"] = extras.library_summary(sd)
        st["covers"] = covers.count(sd)
        print(json.dumps(st, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()

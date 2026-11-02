#!/usr/bin/env python3
"""Card snapshots / going back (studio/safety.py): saves must survive everything.

    python3 tests/test_safety.py
"""
import os
import shutil
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
TMP = tempfile.mkdtemp()
os.environ["HOME"] = os.environ["USERPROFILE"] = os.path.join(TMP, "home")
os.makedirs(os.environ["HOME"])

from studio import runtime, safety  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


def w(sd, rel, data):
    p = os.path.join(sd, *rel.split("/"))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())


def r(sd, rel):
    p = os.path.join(sd, *rel.split("/"))
    if not os.path.exists(p):
        return None
    with open(p, "rb") as f:
        return f.read()


def main():
    check(safety.is_protected("roms/NDS/Mario.sav") and safety.is_protected("_hyper/profiles/p1/saves/x.sav")
          and safety.is_protected("roms/GBA/saves/a.sav1") and safety.is_protected("roms/SNES/Zelda.srm")
          and not safety.is_protected("_nds/nds-bootstrap-release.nds") and not safety.is_protected("BOOT.NDS"),
          "save detection")

    sd = os.path.join(TMP, "sd")
    os.makedirs(sd)
    members = runtime.runtime_members()
    check(len(members) > 20, "runtime members listed (%d)" % len(members))
    replaced = members[0][1]
    w(sd, "BOOT.NDS", b"TWILIGHT-MENU" * 1000)
    w(sd, "_nds/TWiLightMenu/settings.ini", "[SRLOADER]\nTHEME = 3\n")
    w(sd, replaced, b"old version of a runtime file")
    w(sd, "_nds/TWiLightMenu/mine.txt", "user file in _nds")
    w(sd, "roms/NDS/Mario.nds", b"\0" * 4096)
    w(sd, "roms/NDS/Mario.sav", b"SAVE-v1")
    w(sd, "roms/NDS/saves/Zelda.sav", b"ZELDA-v1")

    rep = runtime.install(sd, "tr", "Ege", 0)
    snap = rep["snapshot"]
    check(snap["kind"] == "original" and snap["saves"] == 2, "original snapshot before install (%s, %d saves)" % (snap["kind"], snap["saves"]))
    check(r(sd, "BOOT.NDS") != b"TWILIGHT-MENU" * 1000, "install replaced BOOT.NDS")
    check(r(sd, "_hyper/restore/original_boot.nds") == b"TWILIGHT-MENU" * 1000, "old BOOT.NDS also kept on the card")
    check(r(sd, "roms/NDS/Mario.sav") == b"SAVE-v1", "install did not touch saves")

    # playing with DSplay
    w(sd, "roms/NDS/Mario.sav", b"SAVE-v2-newer")
    w(sd, "roms/NDS/New.sav", b"NEW-GAME")
    w(sd, "_hyper/profiles/p1/saves/Mario.sav", b"SECOND-PROFILE")
    os.remove(os.path.join(sd, "roms", "NDS", "saves", "Zelda.sav"))        # lost somehow

    # second install (update): an "install" snapshot, the original stays
    rep2 = runtime.install(sd, "tr", "Ege", 0)
    check(rep2["snapshot"]["kind"] == "install", "update makes an install snapshot")
    lst = safety.list_snapshots(sd)["snapshots"]
    check(sum(1 for s in lst if s["kind"] == "original" and not s["on_card"]) == 1 and any(s["on_card"] for s in lst), "one original (+ card copy)")

    orig = next(s for s in lst if s["kind"] == "original" and not s["on_card"])
    res = safety.restore(sd, orig["id"])
    check(r(sd, "BOOT.NDS") == b"TWILIGHT-MENU" * 1000, "BOOT.NDS back to the old menu")
    check(r(sd, "_nds/TWiLightMenu/settings.ini") == b"[SRLOADER]\nTHEME = 3\n", "old settings.ini back")
    check(r(sd, replaced) == b"old version of a runtime file", "replaced runtime file back")
    check(r(sd, "_nds/TWiLightMenu/mine.txt") == b"user file in _nds", "user files untouched")
    check(r(sd, "HyperLauncher.nds") is None and r(sd, members[-1][1]) is None or members[-1][1] == replaced,
          "files added by the installer removed")
    check(r(sd, "roms/NDS/Mario.sav") == b"SAVE-v2-newer", "newer save kept (not rolled back)")
    conf = [c for c in res["conflicts"] if c.endswith("Mario.sav")]
    check(bool(conf), "older save version kept in save_conflicts")
    check(r(sd, "roms/NDS/New.sav") == b"NEW-GAME", "save made after install kept")
    check(r(sd, "roms/NDS/saves/Zelda.sav") == b"ZELDA-v1", "lost save came back from the snapshot")
    check(r(sd, "_hyper/profiles/p1/saves/Mario.sav") == b"SECOND-PROFILE" and res["kept_saves"], "profile saves kept + reported")
    with zipfile.ZipFile(os.path.join(res["safety"], "saves.zip")) as z:
        check(z.read("roms/NDS/Mario.sav") == b"SAVE-v2-newer", "saves backed up before going back")

    # whole card backup + restore after losing a game
    full = safety.snapshot(sd, "full")
    check(full["copied"] >= 5, "full snapshot copies every file (%d)" % full["copied"])
    os.remove(os.path.join(sd, "roms", "NDS", "Mario.nds"))
    w(sd, "roms/NDS/Mario.sav", b"SAVE-v3")
    safety.restore(sd, full["id"])
    check(r(sd, "roms/NDS/Mario.nds") == b"\0" * 4096, "full restore brings the game back")
    check(r(sd, "roms/NDS/Mario.sav") == b"SAVE-v3", "full restore never rolls a save back")

    # a backup that cannot be completed stops the installer before it changes anything
    orig_snap = safety.snapshot
    safety.snapshot = lambda *a, **k: (_ for _ in ()).throw(safety.SafetyError("disk full"))
    boot_before = r(sd, "BOOT.NDS")
    try:
        runtime.install(sd, "tr", "Ege", 0)
        check(False, "install stops without a backup")
    except safety.SafetyError:
        check(r(sd, "BOOT.NDS") == boot_before, "install stops without a backup")
    safety.snapshot = orig_snap

    # first installation with a whole-card backup: that is the original (and only the
    # installer's files go on the card)
    sd2 = os.path.join(TMP, "sd2")
    w(sd2, "BOOT.NDS", b"OTHER-MENU")
    w(sd2, "roms/NDS/Big.nds", b"\1" * 100000)
    rep3 = runtime.install(sd2, "tr", "", 0, backup="full")
    check(rep3["snapshot"]["kind"] == "original" and rep3["snapshot"]["full"], "full backup at first install = original")
    check(r(sd2, "_hyper/restore/original/files/roms/NDS/Big.nds") is None and r(sd2, "_hyper/restore/original/files/BOOT.NDS") == b"OTHER-MENU",
          "card copy holds only the installer's files")

    shutil.rmtree(TMP, ignore_errors=True)
    print("ALL OK" if not fails else "%d FAILED" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

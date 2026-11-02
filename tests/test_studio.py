"""DSplay Studio tests:  python3 tests/test_studio.py"""
import urllib.parse, os, sys, io, json, shutil, struct, wave, zipfile, threading, urllib.request, http.client, tempfile
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, ROOT)
# backups made by the tests must not end up in the real Documents folder
os.environ["HOME"] = os.environ["USERPROFILE"] = tempfile.mkdtemp(prefix="home_")
from studio import runtime, roms, extras, covers, pngcodec, systems, server, profiles

ok = 0
def check(c, msg):
    global ok
    if not c: raise SystemExit("FAIL: " + msg)
    ok += 1

sd = tempfile.mkdtemp(prefix="sd_")
if not os.path.isfile(os.path.join(systems.payload_dir(), "HyperDSi-Runtime.zip")) or not os.path.isfile(os.path.join(systems.payload_dir(), "BOOT.NDS")):
    raise SystemExit("assets/payloads/BOOT.NDS and HyperDSi-Runtime.zip are needed for this test")
# an "old" TWiLight boot + stale cache
open(os.path.join(sd, "BOOT.NDS"), "wb").write(b"x" * 1000)
os.makedirs(os.path.join(sd, "roms/NDS"))
open(os.path.join(sd, "roms/NDS/.hyper_cache.bin"), "wb").write(b"old")
rep = runtime.install(sd, "en", "Ege", 1)
check(rep["extracted"] > 100, "extracted")
check(len(rep["backed_up"]) == 1, "boot backup")
check(rep["removed_stale"] == 1, "stale removed")
st = runtime.card_status(sd)
check(all(c["ok"] for c in st["components"]), "components " + str([c for c in st["components"] if not c["ok"]]))
check(st["boot"]["hyper"] and st["boot"]["current"], "boot current " + str(st["boot"]))
raw = open(os.path.join(sd, "_hyper/settings.bin"), "rb").read()
check(len(raw) == 40, "settings size")
v = struct.unpack(runtime.SETTINGS_FMT, raw)
check(v[0] == 0x35475453 and v[1] == 1 and v[2] == 0 and v[12].rstrip(b"\0") == b"Ege", "settings vals " + str(v))
# reinstall keeps settings/profile & twl settings
open(os.path.join(sd, "_nds/TWiLightMenu/settings.ini"), "w").write("USER")
rep2 = runtime.install(sd, "tr", "", None)
check(open(os.path.join(sd, "_nds/TWiLightMenu/settings.ini")).read() == "USER", "twl kept")
v = struct.unpack(runtime.SETTINGS_FMT, open(os.path.join(sd, "_hyper/settings.bin"), "rb").read())
check(v[2] == 1 and v[1] == 1 and v[12].rstrip(b"\0") == b"Ege", "settings merge " + str(v))
check(len(rep2["backed_up"]) == 0, "no second backup")
check(not os.path.exists(os.path.join(sd, "version.txt")), "no version.txt at root")

# games (small fake files; BOOT.NDS doubles as a real .nds)
src = tempfile.mkdtemp(prefix="roms_")
for sub, name, size in [("GBA", "Advance Wars (USA) [!].gba", 4096), ("GBA", "Golden Sun (Europe).gba", 8192),
                        ("GameBoy", "Tetris (World).gb", 32768), ("NES", "Mega Man 2 (USA).nes", 4000)]:
    os.makedirs(os.path.join(src, sub), exist_ok=True)
    open(os.path.join(src, sub, name), "wb").write(bytes([len(name)]) * size)
os.makedirs(os.path.join(src, "NDS"), exist_ok=True)
shutil.copy(os.path.join(ROOT, "assets/payloads/BOOT.NDS"), os.path.join(src, "NDS/A8DS.nds"))
res = roms.place_any(f"{src}/GBA/Advance Wars (USA) [!].gba", sd, "Advance Wars (USA) [!].gba")
check(res[0]["status"] == "copied" and res[0]["dest"] == "roms/GBA/Advance Wars (USA) [!].gba", str(res))
res = roms.place_any(f"{src}/GBA/Advance Wars (USA) [!].gba", sd, "Advance Wars (USA) [!].gba")
check(res[0]["status"] == "exists", "exists")
# zip archive with nested game + junk + zip-slip attempt
zp = os.path.join(sd, "..", "pack.zip")
with zipfile.ZipFile(zp, "w") as z:
    z.write(f"{src}/GameBoy/Tetris (World).gb", "sub/Tetris (World).gb")
    z.writestr("../../evil.nes", b"bad")
    z.writestr("readme.txt", b"hi")
res = roms.place_any(zp, sd, "pack.zip")
check([r["status"] for r in res] == ["copied"], "zip " + str(res))
check(not os.path.exists(os.path.join(os.path.dirname(sd), "evil.nes")), "zip slip")
# nds in series mode, save travels, existing save kept
tmpd = tempfile.mkdtemp()
shutil.copy(f"{src}/NDS/A8DS.nds", os.path.join(tmpd, "Pokemon Test.nds"))
open(os.path.join(tmpd, "Pokemon Test.sav"), "wb").write(b"S" * 512)
res = roms.place_file(os.path.join(tmpd, "Pokemon Test.nds"), sd, "Pokemon Test.nds", "series")
check(res["dest"] == "roms/NDS/Pokemon/Pokemon Test.nds" and res["save"] == "copied", str(res))
longname = "A" * 80 + ".gba"
shutil.copy(f"{src}/GBA/Golden Sun (Europe).gba", os.path.join(tmpd, longname))
res = roms.place_file(os.path.join(tmpd, longname), sd, longname)
check(len(res["file"].encode()) <= 63 and res["file"].endswith(".gba"), "shorten " + res["file"])
check(roms.place_file(os.path.join(tmpd, "Pokemon Test.sav"), sd, "x.txt")["status"] == "skipped", "unsupported")
lib = extras.library_summary(sd)
check(lib["total"] == 4, "library " + str(lib))
info = roms.inspect_nds(f"{src}/NDS/A8DS.nds")
check(info["title"] and not info["corrupt"], "inspect " + str(info))

# bios
r = extras.install_gba_bios(sd, b"\0" * 16384)
check(r["ok"] and not r["verified"], "bios")
check(extras.install_gba_bios(sd, b"\0" * 100)["ok"] is False, "bios size")
check(runtime.card_status(sd)["gba_bios"] == "_gba/bios.bin", "bios status")

# sfx
buf = io.BytesIO()
with wave.open(buf, "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
    w.writeframes(b"".join(struct.pack("<hh", (i * 300) % 20000 - 10000, 0) for i in range(44100 * 2)))
r = extras.install_sfx(sd, "tick", buf.getvalue())
check(r["ok"] and r["seconds"] == 1.0, "sfx " + str(r))
with wave.open(os.path.join(sd, "_hyper/sfx/tick.wav")) as w:
    check(w.getframerate() == 22050 and w.getnchannels() == 1 and w.getsampwidth() == 2, "sfx fmt")
check(extras.install_sfx(sd, "nope", buf.getvalue())["ok"] is False, "sfx event")
check(extras.list_sfx(sd) == ["tick"], "sfx list")
_tmp_classic = tempfile.mkdtemp()
r = extras.install_classic_sfx(_tmp_classic)
check(r["ok"] and r["count"] == 16 and sorted(extras.list_sfx(_tmp_classic)) == sorted(extras.SFX_EVENTS), "classic sfx")
with wave.open(os.path.join(_tmp_classic, "_hyper/sfx/launch.wav")) as w:
    check(w.getframerate() == 22050 and w.getnframes() == int(22050 * 0.4), "classic launch")

# photos from the PC
r = extras.install_photo(sd, "Tatil/../x", "IMG 1.HEIC", b"\xff\xd8" + b"x" * 100)
check(r["ok"] and r["status"] == "copied" and os.path.isfile(os.path.join(sd, "photos", "Tatil..x", "IMG 1.jpg")), "photo " + str(r))
check(extras.install_photo(sd, "Tatil/../x", "IMG 1.HEIC", b"\xff\xd8" + b"x" * 100)["status"] == "exists", "photo exists")
check(extras.install_photo(sd, "a", "b.png", b"\x89PNG")["ok"] is False, "photo jpeg only")
check(extras.list_photo_albums(sd) == [{"name": "Tatil..x", "count": 1}], "albums " + str(extras.list_photo_albums(sd)))

# profile picture
check(extras.install_avatar(sd, bytes([200, 30, 30, 255]) * 4096)["ok"], "avatar")
check(os.path.getsize(os.path.join(sd, "_hyper/avatar.bin")) == 8 + 64 * 64 * 2, "avatar size")
check(extras.avatar_png(sd)[:8] == b"\x89PNG\r\n\x1a\n", "avatar png")
check(extras.install_avatar(sd, b"x" * 10)["ok"] is False, "avatar bad")

# profiles: read-only view of a card from before profiles, then migration + management
lp = profiles.list_profiles(sd)
check(len(lp["profiles"]) == 1 and lp["profiles"][0]["name"] == "Ege" and lp["profiles"][0]["avatar"], "legacy profile " + str(lp))
check(not os.path.isdir(os.path.join(sd, "_hyper/profiles")), "listing does not migrate")
check(profiles.avatar_png(sd, 0)[:8] == b"\x89PNG\r\n\x1a\n", "legacy avatar png")
r = profiles.create_profile(sd, "Ayşe")
check(r["ok"] and r["slot"] == 1, "create " + str(r))
check(os.path.isfile(os.path.join(sd, "_hyper/profiles/p0/settings.bin")) and not os.path.exists(os.path.join(sd, "_hyper/settings.bin")), "migrated")
check(os.path.isfile(os.path.join(sd, "_hyper/profiles/p0/avatar.bin")), "avatar migrated")
p1 = profiles.read_profile(sd, 1)
check(p1["name"] == "Ayşe" and p1["color"] == 1, "p1 " + str(p1))
v = struct.unpack(runtime.SETTINGS_FMT, open(os.path.join(sd, "_hyper/profiles/p1/settings.bin"), "rb").read())
check(v[12].rstrip(b"\0").decode() == "Ayşe" and v[1] == 1, "p1 settings copy " + str(v))
check(profiles.install_wallpaper(sd, 1, bytes([10, 20, 30, 255]) * (256 * 192))["ok"], "wallpaper")
check(os.path.getsize(os.path.join(sd, "_hyper/profiles/p1/wallpaper.bin")) == 8 + 256 * 192 * 2, "wallpaper size")
check(profiles.wallpaper_png(sd, 1)[:4] == b"\x89PNG", "wallpaper png")
# a locked profile (written like the console does) can be reset from Studio
raw = bytearray(open(os.path.join(sd, "_hyper/profiles/p1/profile.bin"), "rb").read())
raw[25] = 4; raw[26:30] = bytes([1, 1, 3, 5])
open(os.path.join(sd, "_hyper/profiles/p1/profile.bin"), "wb").write(bytes(raw))
check(profiles.read_profile(sd, 1)["locked"], "locked")
profiles.update_profile(sd, 1, name="Ayşe K", color=4, reset_pin=True)
p1 = profiles.read_profile(sd, 1)
check(not p1["locked"] and p1["color"] == 4 and p1["name"] == "Ayşe K", "update " + str(p1))
check(len(open(os.path.join(sd, "_hyper/profiles/p1/profile.bin"), "rb").read()) == 92, "profile.bin size")
for nm in ("C", "D"):
    check(profiles.create_profile(sd, nm)["ok"], "create " + nm)
check(not profiles.create_profile(sd, "E")["ok"], "max 4")
check(profiles.delete_profile(sd, 3)["ok"] and profiles.read_profile(sd, 3) is None, "delete")
check(any(n.startswith("deleted_p3_") for n in os.listdir(os.path.join(sd, "_hyper/profiles"))), "deleted kept")
profiles.set_login_screen(sd, False)
check(profiles.list_profiles(sd)["login"] is False, "login flag")
st = runtime.card_status(sd)
check(st["settings"] and st["settings"]["profile"] == "Ege", "status settings from profile " + str(st["settings"]))
bk = profiles.backup_profile(sd, 1, tempfile.mkdtemp())
check(bk["count"] >= 3 and any(n.endswith("p1/wallpaper.bin") for n in zipfile.ZipFile(bk["zip"]).namelist()), "profile backup")

# saves backup / restore
bk = extras.backup_saves(sd, tempfile.mkdtemp())
names = zipfile.ZipFile(bk["zip"]).namelist()
check("roms/NDS/Pokemon/Pokemon Test.sav" in names and "_hyper/profiles/p0/settings.bin" in names, "backup names " + str(names))
check(not any(n.startswith("_nds/") for n in names), "no _nds in backup")
open(os.path.join(sd, "roms/NDS/Pokemon/Pokemon Test.sav"), "wb").write(b"X")
os.environ["HOME"] = tempfile.mkdtemp()
r = extras.restore_saves(sd, bk["zip"])
check(r["restored"] >= 2 and open(os.path.join(sd, "roms/NDS/Pokemon/Pokemon Test.sav"), "rb").read() == b"S" * 512, "restore")

# png codec: palette png from Pillow -> console png
try:
    from PIL import Image
except ImportError:
    Image = None
if Image:
    im = Image.new("P", (300, 200)); im.putpalette([i % 256 for i in range(768)])
    for x in range(300):
        for y in range(200): im.putpixel((x, y), (x + y) % 256)
    b = io.BytesIO(); im.save(b, "PNG")
    w, h, rgb = pngcodec.decode(b.getvalue())
    check((w, h) == (300, 200) and len(rgb) == 300 * 200 * 3, "decode pal")
    ref = im.convert("RGB").tobytes()
    check(rgb == ref, "decode exact")
    for mode in ("RGBA", "L", "LA", "RGB"):
        b = io.BytesIO(); Image.new(mode, (50, 40), (10, 200, 30, 128)[:len(mode)] if mode != "L" else 99).save(b, "PNG")
        pngcodec.decode(b.getvalue())
    w2, h2, small = pngcodec.resize_fit(w, h, rgb, 128)
    out = pngcodec.encode_rgb(w2, h2, small)
    im2 = Image.open(io.BytesIO(out)); check(im2.size == (128, 85) and im2.mode == "RGB", "encode " + str(im2.size))
    cp = pngcodec.to_console_png(b.getvalue()); check(Image.open(io.BytesIO(cp)).mode == "RGB", "console png")

# cover naming / urls
p = os.path.join(sd, "roms/NDS/Pokemon/Pokemon Test.nds")
check(covers.cover_name(p, "nds") == "####.png" or covers.cover_name(p, "nds").endswith(".png"), "cover name")
u = covers.candidate_urls(os.path.join(sd, "roms/GBA/Advance Wars (USA) [!].gba"), "gba")
check(u[0].startswith("https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/Advance%20Wars"), u[0])
check(systems.console_filename("ü" * 40) == "ü" * 31, "console fn")

# pre-made covers for the console (_hyper/covers/_covers.pak)
import struct, time
cd = os.path.join(sd, "_hyper", "covers"); os.makedirs(cd, exist_ok=True)
w8, h8 = 40, 30
open(os.path.join(cd, "ABCE.png"), "wb").write(pngcodec.encode_rgb(w8, h8, bytes([200, 30, 60]) * (w8 * h8)))
check(covers.build_pack(sd) >= 1, "pack built")
pk = open(os.path.join(cd, covers.PACK_NAME), "rb").read()
mg, ver, n, esz = struct.unpack_from("<IIII", pk, 0)
check(mg == 0x50564348 and ver == covers.PACK_VERSION and esz == 20, "pack header")
hs = [struct.unpack_from("<I", pk, 16 + i * 20)[0] for i in range(n)]
check(hs == sorted(hs) and covers.name_hash("ABCE.png") in hs, "pack sorted by hash")
i = hs.index(covers.name_hash("abce.PNG"))
off = struct.unpack_from("<I", pk, 16 + i * 20 + 16)[0]
px = struct.unpack_from("<H", pk, off + (48 * 96 + 48) * 2)[0]
check(struct.unpack_from("<H", pk, off)[0] == 0, "pack border transparent")
check(px == ((200 >> 3) | ((30 >> 3) << 5) | ((60 >> 3) << 10) | 0x8000), "pack pixel %x" % px)
t0 = time.time(); covers.build_pack(sd); check(time.time() - t0 < 2, "unchanged covers reused")
check(covers.count(sd) >= 1 and not any(n.startswith("_") for n in os.listdir(cd) if n.endswith(".png")), "pack not counted")
print("unit OK", ok)

# ------------------------------------------------------------ server
port = server._free_port(8799)
# cheat database (usrcheat.dat)
from studio import cheats as cheats_mod
check(cheats_mod.status(sd)["present"] is False, "no cheat db")
check(cheats_mod.install(sd, b"nope" * 100)["reason"] == "format", "cheat db header checked")
import struct as _st
_db = b"R4 CheatCode".ljust(0x100, b"\0") + _st.pack("<4sIQ", b"AMCE", 1, 0x130) + _st.pack("<4sIQ", b"ABCD", 2, 0x140) + b"\0" * 16 + b"\0" * 32
r = cheats_mod.install(sd, _db)
check(r["ok"] and r["games"] == 2 and cheats_mod.status(sd)["games"] == 2, "cheat db installed " + str(r))
_tmpdb = os.path.join(tmpd, "usrcheat.dat")
open(_tmpdb, "wb").write(_db)
r = cheats_mod.download(sd, urls=("file:///nonexistent/x.dat", "file://" + _tmpdb))
check(r["ok"] and r["source"].startswith("file://"), "cheat db download falls back " + str(r))
check(cheats_mod.download(sd, urls=("file:///nonexistent/x.dat",))["reason"] == "download", "cheat db download fails cleanly")
check(cheats_mod.remove(sd)["ok"] and not cheats_mod.status(sd)["present"], "cheat db removed")
# homebrew store (Universal-DB): catalog, install to the card, archives, removal
from studio import store as store_mod, pngcodec as _png
_sd2 = tempfile.mkdtemp()
_prog = os.path.join(tmpd, "Box.nds"); open(_prog, "wb").write(b"\1" * 4096)
_zip = os.path.join(tmpd, "pack.zip")
with zipfile.ZipFile(_zip, "w") as _z:
    _z.writestr("pack/Gravity.nds", b"\2" * 2048)
    _z.writestr("pack/readme.txt", "hi")
_raw = [
    {"slug": "box", "title": "Box", "systems": ["DS"], "categories": ["game"], "stars": 5, "version": "1.0",
     "downloads": {"[git] box.nds": {"url": "file:///nope"}, "box.nds": {"url": "file://" + _prog, "size": 4096}, "box.cia": {"url": "x"}}},
    {"slug": "gravity", "title": "Gravity", "systems": ["DS"], "categories": ["game"], "stars": 9,
     "downloads": {"gravity.zip": {"url": "file://" + _zip, "size": 300}}},
    {"slug": "hiyacfw", "title": "hiyaCFW", "systems": ["DS"], "downloads": {"h.nds": {"url": "x"}}},
    {"slug": "x3ds", "title": "3DS only", "systems": ["3DS"], "downloads": {"a.nds": {"url": "x"}}},
]
_items = store_mod.normalize(_raw)
check([i["slug"] for i in _items] == ["gravity", "box"], "store: DS only, safe ones, most stars first " + str([i["slug"] for i in _items]))
check(_items[1]["file"] == "box.nds" and _items[1]["url"].endswith("Box.nds"), "store: release file, not [git]")
store_mod._mem.update(items=_items, t=__import__("time").time())
r = store_mod.install_to_card(_sd2, "box")
check(r["ok"] and open(os.path.join(_sd2, "roms/NDS/Homebrew/box.nds"), "rb").read() == b"\1" * 4096, "store: installed to the card")
r = store_mod.install_to_card(_sd2, "gravity")
check(r["ok"] and r["files"] == ["Gravity.nds"] and os.path.isfile(os.path.join(_sd2, "roms/NDS/Homebrew/Gravity.nds")), "store: archive unpacked")
check(set(store_mod.installed_on_card(_sd2)) == {"box", "gravity"}, "store: installed.txt")
os.remove(os.path.join(_sd2, "roms/NDS/Homebrew/box.nds"))
check(set(store_mod.installed_on_card(_sd2)) == {"gravity"}, "store: deleted program not listed")
check(store_mod.remove_from_card(_sd2, "gravity")["ok"] and not os.path.exists(os.path.join(_sd2, "roms/NDS/Homebrew/Gravity.nds")), "store: removed")
_ic = os.path.join(tmpd, "ic.png"); open(_ic, "wb").write(_png.encode_rgb(16, 16, bytes([10, 200, 30]) * 256))
_it = dict(_items[1], icon="file://" + _ic, slug="box-icon-test")
_px = store_mod.icon_rgb555(_it)
check(_px is not None and len(_px) == 2048, "store: icon for the console")
_bin = store_mod.build_catalog_bin(_items, with_icons=False)
check(_bin[:4] == b"HSTR" and len(_bin) == 16 + 2 * 2444, "store: console catalog size")
check(store_mod.read_installed(store_mod.write_installed({"a": {"version": "1", "files": ["x.nds", "y.nds"]}}))["a"]["files"] == ["x.nds", "y.nds"], "store: installed.txt round trip")
httpd = server.ThreadingHTTPServer(("127.0.0.1", port), server.Handler)
threading.Thread(target=httpd.serve_forever, daemon=True).start()
def req(method, path, body=None, headers=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
    c.request(method, path, body=body, headers=headers or {})
    r = c.getresponse(); data = r.read(); c.close(); return r.status, data
s, page = req("GET", "/")
check(s == 200 and server.TOKEN.encode() in page and b"{{TOKEN}}" not in page, "index")
for f in ("app.js", "app.css", "i18n.js", "icon.svg"):
    check(req("GET", "/static/" + f)[0] == 200, f)
check(req("GET", "/static/../server.py")[0] == 404, "traversal")
check(req("GET", "/api/drives")[0] == 403, "no token")
H = {"X-Studio-Token": server.TOKEN}
check(req("GET", "/api/drives", headers=dict(H, Host="evil.com:%d" % port))[0] == 403, "bad host")
s, d = req("GET", "/api/drives", headers=H); check(s == 200 and json.loads(d)["payload"]["boot"], "drives")
s, d = req("GET", "/api/status?sd=" + urllib.parse.quote(sd), headers=H)
st = json.loads(d); check(s == 200 and st["library"]["total"] == 4 and st["sfx"] == ["tick"], "status " + str(st)[:200])
check(req("POST", "/api/install", json.dumps({"sd": sd}), dict(H))[0] == 400, "json content type required")
s, d = req("POST", "/api/backup_saves", b'{"sd": "%s"}' % sd.encode(), dict(H, **{"Content-Type": "application/json"}))
check(s == 200 and json.loads(d)["count"] >= 2, "backup api")
data = open(f"{src}/NES/Mega Man 2 (USA).nes", "rb").read()
s, d = req("POST", "/api/upload_game?" + urllib.parse.urlencode({"sd": sd, "name": "Mega Man 2 (USA).nes"}), data, dict(H, **{"Content-Type": "application/octet-stream"}))
check(s == 200 and json.loads(d)["results"][0]["status"] == "copied", "upload " + d.decode())
s, d = req("POST", "/api/format", b'{"sd": "%s", "confirm": "x"}' % sd.encode(), dict(H, **{"Content-Type": "application/json"}))
check(s == 400, "format refuses non-drive")
s, d = req("POST", "/api/install", b'{"sd": "%s", "lang": "de"}' % sd.encode(), dict(H, **{"Content-Type": "application/json"}))
jid = json.loads(d)["job"]
import time
for _ in range(200):
    s, d = req("GET", "/api/job?id=" + jid, headers=H); j = json.loads(d)
    if j["done"]: break
    time.sleep(0.1)
check(j["done"] and not j["error"] and j["result"]["extracted"] > 100, "install job " + str(j)[:300])
httpd.shutdown()
print("ALL OK", ok)

"""DSplay Studio local web server.

Security model
--------------
* Listens on 127.0.0.1 only.
* Every /api request must carry the per-launch secret token (X-Studio-Token) that is
  embedded into the page the server itself serves, and a Host header of
  127.0.0.1/localhost. This blocks other web sites (CSRF / DNS rebinding) from
  formatting cards or reading files through the local server.
* Paths sent by the page must be an existing directory; formatting additionally
  requires a drive the OS reports as removable and the typed volume name.
"""

import json
import os
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import traceback
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import STUDIO_VERSION, boot, cheats, store, covers, drives, extras, pad, profiles, roms, runtime, safety, wireless
from .systems import resource_root

WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
TOKEN = secrets.token_urlsafe(24)
MAX_UPLOAD = 4 * 1024 ** 3  # 4 GB (largest DS cartridge is 512 MB, archives can be bigger)

KILL_SW = """self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (e) => e.waitUntil((async () => {
  for (const k of await caches.keys()) await caches.delete(k);
  await self.registration.unregister();
  for (const c of await self.clients.matchAll({ type: 'window' })) c.navigate(c.url);
})()));
"""

_jobs = {}
_jobs_lock = threading.Lock()


def _job_start(kind, fn):
    jid = secrets.token_hex(6)
    state = {"id": jid, "kind": kind, "done": False, "progress": None, "result": None, "error": None}
    with _jobs_lock:
        _jobs[jid] = state

    def run():
        try:
            state["result"] = fn(lambda p: state.__setitem__("progress", p))
        except Exception as e:  # noqa: BLE001
            state["error"] = str(e)
            traceback.print_exc()
        finally:
            state["done"] = True

    threading.Thread(target=run, daemon=True).start()
    return jid


class Handler(BaseHTTPRequestHandler):
    server_version = "DSplayStudio/" + STUDIO_VERSION
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # keep the console quiet
        if os.environ.get("STUDIO_DEBUG"):
            super().log_message(fmt, *args)

    # ---- helpers ---------------------------------------------------------
    def _send(self, code, body, ctype="application/json; charset=utf-8", extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False).encode("utf-8")
        elif isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        if ctype.startswith("text/html"):
            self.send_header("Content-Security-Policy",
                             "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; style-src 'self' 'unsafe-inline'; "
                             "script-src 'self'; connect-src 'self'; frame-ancestors 'none'")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        if code >= 400:
            # the request body may not have been read: don't reuse this connection
            self.send_header("Connection", "close")
            self.close_connection = True
        self.end_headers()
        self.wfile.write(body)

    def _host_ok(self):
        host = (self.headers.get("Host") or "").split(":")[0]
        return host in ("127.0.0.1", "localhost")

    def _authorized(self):
        return self._host_ok() and secrets.compare_digest(self.headers.get("X-Studio-Token", ""), TOKEN)

    def _json_body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > 1024 * 1024:
            raise ValueError("body too large")
        if n and "application/json" not in (self.headers.get("Content-Type") or ""):
            raise ValueError("json expected")
        return json.loads(self.rfile.read(n) or b"{}") if n else {}

    def _sd(self, value):
        if not value or not isinstance(value, str) or not os.path.isdir(value):
            raise ValueError("SD kart yolu bulunamadı")
        return os.path.abspath(value)

    def _read_upload(self, limit):
        n = int(self.headers.get("Content-Length") or 0)
        if n <= 0 or n > limit:
            raise ValueError("geçersiz dosya boyutu")
        return n

    # ---- GET ---------------------------------------------------------------
    def do_GET(self):
        if not self._host_ok():
            return self._send(403, {"error": "forbidden"})
        url = urllib.parse.urlparse(self.path)
        if url.path == "/sw.js":
            # The old manager registered a caching service worker on this origin; replace it with
            # one that deletes its caches, unregisters itself and reloads the page.
            return self._send(200, KILL_SW, "application/javascript; charset=utf-8")
        if url.path in ("/", "/index.html"):
            with open(os.path.join(WEB_DIR, "index.html"), encoding="utf-8") as f:
                html = f.read().replace("{{TOKEN}}", TOKEN).replace("{{VERSION}}", STUDIO_VERSION)
            return self._send(200, html, "text/html; charset=utf-8", {"Clear-Site-Data": '"cache"'})
        if url.path.startswith("/static/"):
            name = os.path.basename(url.path)
            path = os.path.join(WEB_DIR, name)
            types = {".css": "text/css", ".js": "application/javascript", ".png": "image/png", ".svg": "image/svg+xml"}
            ext = os.path.splitext(name)[1]
            if ext in types and os.path.isfile(path):
                with open(path, "rb") as f:
                    return self._send(200, f.read(), types[ext] + ("; charset=utf-8" if ext in (".css", ".js") else ""))
            return self._send(404, {"error": "not found"})
        if not url.path.startswith("/api/"):
            return self._send(404, {"error": "not found"})
        if not self._authorized():
            return self._send(403, {"error": "forbidden"})
        q = dict(urllib.parse.parse_qsl(url.query))
        try:
            return self._api_get(url.path, q)
        except ValueError as e:
            return self._send(400, {"error": str(e)})
        except Exception as e:  # noqa: BLE001
            traceback.print_exc()
            return self._send(500, {"error": _friendly_error(e)})

    def _api_get(self, path, q):
        if path == "/api/drives":
            return self._send(200, {"drives": drives.list_drives(), "payload": runtime.payload_status(),
                                    "platform": sys.platform, "version": STUDIO_VERSION})
        if path == "/api/status":
            sd = self._sd(q.get("sd"))
            st = runtime.card_status(sd)
            st["drive"] = drives.describe_path(sd)
            st["library"] = extras.library_summary(sd)
            st["covers"] = covers.count(sd)
            st["sfx"] = extras.list_sfx(sd)
            st["albums"] = extras.list_photo_albums(sd)
            st["profiles"] = profiles.list_profiles(sd)
            return self._send(200, st)
        if path == "/api/profiles":
            return self._send(200, profiles.list_profiles(self._sd(q.get("sd"))))
        if path in ("/api/avatar", "/api/wallpaper"):
            sd = self._sd(q.get("sd"))
            slot = q.get("slot", profiles.active_slot(sd) or 0)
            png = (profiles.avatar_png if path == "/api/avatar" else profiles.wallpaper_png)(sd, slot)
            if not png:
                return self._send(404, {"error": "no picture"})
            return self._send(200, png, "image/png", {"Cache-Control": "no-store"})
        if path == "/api/pad":
            st = pad.service().status()
            sd = q.get("sd")
            st["wifi"] = pad.read_wifi(sd) if sd and os.path.isdir(sd) else None
            return self._send(200, st)
        if path == "/api/boot":
            return self._send(200, boot.status(self._sd(q.get("sd"))))
        if path == "/api/cheats":
            return self._send(200, cheats.status(self._sd(q.get("sd"))))
        if path == "/api/store":
            try:
                items = store.load_catalog(force=q.get("refresh") == "1")
            except ConnectionError as e:
                return self._send(200, {"offline": True, "error": str(e), "items": []})
            sd = q.get("sd")
            inst = store.installed_on_card(self._sd(sd)) if sd else {}
            return self._send(200, {"items": store.public(items, inst), "wifi": wireless.status()})
        if path == "/api/store_icon":
            it = store.find(q.get("slug", ""))
            data = store.icon_bytes(it) if it else None
            if not data:
                return self._send(404, {"error": "no icon"})
            ctype = "image/png" if data.startswith(b"\x89PNG") else "image/gif" if data.startswith(b"GIF") else "image/bmp"
            return self._send(200, data, ctype, {"Cache-Control": "max-age=86400"})
        if path == "/api/boot_png":
            png = boot.picture_png(self._sd(q.get("sd")), q.get("which"))
            if not png:
                return self._send(404, {"error": "no picture"})
            return self._send(200, png, "image/png", {"Cache-Control": "no-store"})
        if path == "/api/snapshots":
            sd = self._sd(q.get("sd"))
            info = safety.list_snapshots(sd)
            info["full_size"] = sum(max(0, v[0]) for v in safety.scan(sd).values())
            try:
                os.makedirs(safety.backup_root(), exist_ok=True)
                info["computer_free"] = shutil.disk_usage(safety.backup_root()).free
            except OSError:
                info["computer_free"] = None
            return self._send(200, info)
        if path == "/api/wifi":
            return self._send(200, wireless.status())
        if path == "/api/job":
            with _jobs_lock:
                job = _jobs.get(q.get("id", ""))
            return self._send(200 if job else 404, job or {"error": "unknown job"})
        return self._send(404, {"error": "unknown endpoint"})

    # ---- POST --------------------------------------------------------------
    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        if not url.path.startswith("/api/") or not self._authorized():
            return self._send(403, {"error": "forbidden"})
        q = dict(urllib.parse.parse_qsl(url.query))
        try:
            return self._api_post(url.path, q)
        except ValueError as e:
            return self._send(400, {"error": str(e)})
        except Exception as e:  # noqa: BLE001
            traceback.print_exc()
            return self._send(500, {"error": _friendly_error(e)})

    def _api_post(self, path, q):
        # ---- binary uploads (query string carries the parameters) ----
        if path == "/api/upload_game":
            sd = self._sd(q.get("sd"))
            name = q.get("name") or "game"
            n = self._read_upload(MAX_UPLOAD)
            tmpdir = tempfile.mkdtemp(prefix="hyperstudio_up_")
            try:
                tmp = os.path.join(tmpdir, "upload.bin")
                left = n
                with open(tmp, "wb") as f:
                    while left > 0:
                        chunk = self.rfile.read(min(left, 1 << 20))
                        if not chunk:
                            raise ValueError("yükleme yarıda kesildi")
                        f.write(chunk)
                        left -= len(chunk)
                results = roms.place_any(tmp, sd, name, q.get("mode", "flat"), q.get("overwrite") == "1")
            finally:
                shutil.rmtree(tmpdir, ignore_errors=True)
            return self._send(200, {"results": results})
        if path == "/api/wifi_upload":
            # a game for the console over Wi-Fi: received here, then copied in a background job
            name = q.get("name") or "game"
            n = self._read_upload(MAX_UPLOAD)
            tmpdir = tempfile.mkdtemp(prefix="hyperstudio_wifi_")
            tmp = os.path.join(tmpdir, "upload.bin")
            left = n
            with open(tmp, "wb") as f:
                while left > 0:
                    chunk = self.rfile.read(min(left, 1 << 20))
                    if not chunk:
                        shutil.rmtree(tmpdir, ignore_errors=True)
                        raise ValueError("yükleme yarıda kesildi")
                    f.write(chunk)
                    left -= len(chunk)
            mode = q.get("mode", "flat")

            def work(p):
                try:
                    return wireless.send_games([tmp], [name], mode, progress=p)
                finally:
                    shutil.rmtree(tmpdir, ignore_errors=True)
            return self._send(200, {"job": _job_start("wifi_games", work)})
        if path == "/api/cheats_upload":
            sd = self._sd(q.get("sd"))
            n = self._read_upload(cheats.MAX_SIZE)
            return self._send(200, cheats.install(sd, self.rfile.read(n)))
        if path == "/api/gba_bios":
            sd = self._sd(q.get("sd"))
            n = self._read_upload(1024 * 1024)
            return self._send(200, extras.install_gba_bios(sd, self.rfile.read(n)))
        if path in ("/api/avatar", "/api/wallpaper"):
            sd = self._sd(q.get("sd"))
            profiles.ensure_profiles(sd)
            slot = q.get("slot", profiles.active_slot(sd))
            n = self._read_upload(64 * 1024 if path == "/api/avatar" else 256 * 1024)
            fn = profiles.install_avatar if path == "/api/avatar" else profiles.install_wallpaper
            return self._send(200, fn(sd, slot, self.rfile.read(n)))
        if path == "/api/boot_pic":
            sd = self._sd(q.get("sd"))
            n = self._read_upload(256 * 1024)
            return self._send(200, boot.install_picture(sd, q.get("which"), self.rfile.read(n)))
        if path == "/api/boot_sound":
            sd = self._sd(q.get("sd"))
            n = self._read_upload(1024 * 1024)
            return self._send(200, boot.install_sound(sd, self.rfile.read(n)))
        if path == "/api/photo":
            sd = self._sd(q.get("sd"))
            n = self._read_upload(4 * 1024 * 1024)
            return self._send(200, extras.install_photo(sd, q.get("album", ""), q.get("name", ""), self.rfile.read(n)))
        if path == "/api/sfx":
            sd = self._sd(q.get("sd"))
            n = self._read_upload(16 * 1024 * 1024)
            return self._send(200, extras.install_sfx(sd, q.get("event", ""), self.rfile.read(n)))
        if path == "/api/restore_saves":
            sd = self._sd(q.get("sd"))
            n = self._read_upload(512 * 1024 * 1024)
            fd, tmp = tempfile.mkstemp(suffix=".zip")
            try:
                with os.fdopen(fd, "wb") as f:
                    f.write(self.rfile.read(n))
                return self._send(200, extras.restore_saves(sd, tmp))
            finally:
                os.remove(tmp)

        data = self._json_body()
        if path == "/api/install":
            sd = self._sd(data.get("sd"))
            lang, profile = data.get("lang", "tr"), data.get("profile", "")
            theme = data.get("theme")
            backup = "full" if data.get("full_backup") else "install"
            jid = _job_start("install", lambda p: runtime.install(sd, lang, profile, theme, progress=p, backup=backup))
            return self._send(200, {"job": jid})
        if path == "/api/format":
            sd = data.get("sd") or ""
            vol = drives.find_drive(sd)
            if not vol:
                raise ValueError("Sürücü bulunamadı")
            if (data.get("confirm") or "").strip() != (vol.get("name") or "").strip():
                raise ValueError("Onay için sürücü adını aynen yazın")
            # everything on the card is about to go: its saves are copied to this computer first
            # (and checked); no backup, no formatting
            snap = None
            if any(True for _ in os.scandir(sd)):
                snap = safety.snapshot(sd, "saves")
            ok, res = drives.format_card(sd)
            if not ok:
                return self._send(200, {"ok": False, "error": res})
            return self._send(200, {"ok": True, "path": res, "snapshot": snap})
        if path == "/api/eject":
            ok, msg = drives.eject_card(self._sd(data.get("sd")))
            return self._send(200, {"ok": ok, "message": msg})
        if path == "/api/covers":
            sd = self._sd(data.get("sd"))
            force = bool(data.get("force"))
            jid = _job_start("covers", lambda p: covers.sync(
                sd, force, progress=lambda d, t, n: p({"done": d, "total": t, "name": n})))
            return self._send(200, {"job": jid})
        if path == "/api/backup_saves":
            sd = self._sd(data.get("sd"))
            return self._send(200, extras.backup_saves(sd))
        if path == "/api/reveal":
            target = data.get("path") or ""
            if not os.path.exists(target):
                raise ValueError("bulunamadı")
            if sys.platform == "darwin":
                subprocess.Popen(["open", "-R", target])
            elif sys.platform == "win32":
                subprocess.Popen(["explorer", "/select,", os.path.normpath(target)])
            else:
                subprocess.Popen(["xdg-open", os.path.dirname(target)])
            return self._send(200, {"ok": True})
        if path == "/api/settings":
            sd = self._sd(data.get("sd"))
            runtime.update_settings(sd, data.get("lang"), data.get("profile"), data.get("theme"))
            return self._send(200, {"ok": True})
        if path in ("/api/avatar_clear", "/api/wallpaper_clear"):
            sd = self._sd(data.get("sd"))
            profiles.ensure_profiles(sd)
            slot = data.get("slot", profiles.active_slot(sd))
            kind = "avatar" if path == "/api/avatar_clear" else "wallpaper"
            return self._send(200, {"removed": profiles.clear_picture(sd, slot, kind)})
        if path == "/api/profile_create":
            sd = self._sd(data.get("sd"))
            return self._send(200, profiles.create_profile(sd, data.get("name", ""), data.get("color")))
        if path == "/api/profile_update":
            sd = self._sd(data.get("sd"))
            return self._send(200, profiles.update_profile(sd, data.get("slot"), data.get("name"), data.get("color"),
                                                           bool(data.get("reset_pin")), data.get("theme")))
        if path == "/api/profile_delete":
            sd = self._sd(data.get("sd"))
            return self._send(200, profiles.delete_profile(sd, data.get("slot")))
        if path == "/api/profile_backup":
            sd = self._sd(data.get("sd"))
            return self._send(200, profiles.backup_profile(sd, data.get("slot"), extras.default_backup_dir()))
        if path == "/api/login_screen":
            sd = self._sd(data.get("sd"))
            return self._send(200, profiles.set_login_screen(sd, bool(data.get("on"))))
        if path == "/api/pad_start":
            svc = pad.service()
            ok = svc.start()
            if ok and not svc.cfg.get("autostart"):
                svc.cfg["autostart"] = True
                svc.save()
            return self._send(200, svc.status())
        if path == "/api/pad_stop":
            svc = pad.service()
            svc.stop()
            svc.cfg["autostart"] = False
            svc.save()
            return self._send(200, svc.status())
        if path == "/api/pad_pair":
            return self._send(200, {"sent": pad.service().pair(data.get("code", 0), (data.get("ip") or "").strip() or None)})
        if path == "/api/pad_forget":
            pad.service().forget(data.get("id"))
            return self._send(200, {"ok": True})
        if path == "/api/pad_config":
            return self._send(200, pad.service().set_config(data.get("buttons"), data.get("layout"),
                                                            data.get("mouse_speed"), data.get("preset")))
        if path == "/api/pad_wifi":
            sd = self._sd(data.get("sd"))
            return self._send(200, pad.write_wifi(sd, data.get("ssid"), data.get("password")))
        if path == "/api/pad_install_gamepad":
            if sys.platform != "win32":
                raise ValueError("yalnızca Windows")

            def work(progress):
                r = subprocess.run([sys.executable, "-m", "pip", "install", "--user", "vgamepad"],
                                   capture_output=True, text=True)
                if r.returncode:
                    raise RuntimeError((r.stderr or r.stdout).strip()[-400:])
                return {"ok": True, "restart": True}
            return self._send(200, {"job": _job_start("pad_gamepad", work)})
        if path == "/api/boot_save":
            sd = self._sd(data.get("sd"))
            if data.get("clear_bottom"):
                boot.clear_picture(sd, "bottom")
            if data.get("clear_sound"):
                boot.clear_sound(sd)
            return self._send(200, boot.save_settings(sd, data.get("ms", 2500), data.get("style"), bool(data.get("use", True))))
        if path == "/api/store_install":
            sd, slug = self._sd(data.get("sd")), str(data.get("slug", ""))
            return self._send(200, {"job": _job_start("store", lambda p: store.install_to_card(sd, slug, progress=p))})
        if path == "/api/store_install_wifi":
            slug = str(data.get("slug", ""))
            return self._send(200, {"job": _job_start("store_wifi", lambda p: store.install_wireless(slug, progress=p))})
        if path == "/api/store_remove":
            return self._send(200, store.remove_from_card(self._sd(data.get("sd")), str(data.get("slug", ""))))
        if path == "/api/cheats_download":
            sd = self._sd(data.get("sd"))
            return self._send(200, {"job": _job_start("cheats", lambda p: cheats.download(sd, progress=p))})
        if path == "/api/cheats_remove":
            return self._send(200, cheats.remove(self._sd(data.get("sd"))))
        if path == "/api/boot_remove":
            return self._send(200, boot.remove(self._sd(data.get("sd"))))
        if path == "/api/wifi_saves":
            return self._send(200, {"job": _job_start("wifi_saves", lambda p: wireless.backup_saves(progress=p))})
        if path == "/api/wifi_covers":
            return self._send(200, {"job": _job_start("wifi_covers", lambda p: wireless.send_covers(progress=p))})
        if path == "/api/snapshot":
            sd = self._sd(data.get("sd"))
            kind = "full" if data.get("kind") == "full" else "saves"
            return self._send(200, {"job": _job_start("snapshot", lambda p: safety.snapshot(sd, kind, progress=p))})
        if path == "/api/snapshot_restore":
            sd = self._sd(data.get("sd"))
            sid = data.get("id")
            return self._send(200, {"job": _job_start("restore", lambda p: safety.restore(sd, sid, progress=p))})
        if path == "/api/sfx_classic":
            return self._send(200, extras.install_classic_sfx(self._sd(data.get("sd"))))
        if path == "/api/sfx_clear":
            return self._send(200, {"removed": extras.clear_sfx(self._sd(data.get("sd")))})
        return self._send(404, {"error": "unknown endpoint"})


def _friendly_error(e):
    """File-system damage on the card (Windows error 1392 / errno EIO) gets an explanation."""
    if isinstance(e, OSError) and (getattr(e, "winerror", None) in (1392, 1393) or e.errno == 5 and sys.platform != "win32"):
        name = getattr(e, "filename", "") or ""
        drive = os.path.splitdrive(name)[0] or "D:"
        return ("SD kartta bozuk bir dosya var (%s). Kartı onarın: Windows'ta komut istemine 'chkdsk %s /f' yazın "
                "veya sürücüye sağ tıklayıp Özellikler > Araçlar > Denetle'yi seçin. Sonra Studio'da yeniden deneyin."
                % (os.path.basename(name) or "?", drive))
    return str(e)


def _free_port(preferred=8770):
    for port in (preferred, 0):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            s.bind(("127.0.0.1", port))
            return s.getsockname()[1]
        except OSError:
            continue
        finally:
            s.close()
    return preferred


def open_window(url):
    """Opens the UI in a chromeless app window when Chrome / Edge is installed."""
    candidates = []
    if sys.platform == "darwin":
        candidates = ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                      "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
                      "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser"]
    elif sys.platform == "win32":
        for env in ("ProgramFiles(x86)", "ProgramFiles", "LocalAppData"):
            base = os.environ.get(env)
            if base:
                candidates += [os.path.join(base, "Microsoft", "Edge", "Application", "msedge.exe"),
                               os.path.join(base, "Google", "Chrome", "Application", "chrome.exe")]
    else:
        candidates = [shutil.which(n) or "" for n in ("google-chrome", "chromium", "chromium-browser")]
    for exe in candidates:
        if exe and os.path.exists(exe):
            try:
                subprocess.Popen([exe, f"--app={url}", "--window-size=1180,820"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return
            except OSError:
                continue
    webbrowser.open(url)


def main(open_ui=True, port=None):
    port = port or _free_port()
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    httpd.daemon_threads = True
    url = f"http://127.0.0.1:{port}/"
    print("=" * 60)
    print(f"  DSplay Studio {STUDIO_VERSION}")
    print(f"  {url}   (pencereyi kapatınca bu terminali de kapatabilirsiniz)")
    print("=" * 60)
    if not runtime.payload_status()["boot"]:
        print("  ! assets/payloads/BOOT.NDS yok - önce 'make' ile DSplay'i derleyin.")
    try:
        svc = pad.service()
        svc.on_store = store.handle_console          # the console's store asks Studio for files
        if svc.cfg.get("autostart"):
            svc.start()
    except Exception:  # noqa: BLE001 - Studio works without the controller
        traceback.print_exc()
    if open_ui:
        threading.Timer(0.3, open_window, args=(url,)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
        try:
            pad.service().stop()
        except Exception:  # noqa: BLE001
            pass


__all__ = ["main", "resource_root"]

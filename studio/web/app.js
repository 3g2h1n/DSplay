/* DSplay Studio front end */
(() => {
  "use strict";
  // remove the caching service worker the previous manager app installed
  if (navigator.serviceWorker && navigator.serviceWorker.getRegistrations) {
    navigator.serviceWorker.getRegistrations().then((rs) => rs.forEach((r) => r.unregister())).catch(() => {});
  }
  const TOKEN = document.querySelector('meta[name="studio-token"]').content;
  const $ = (id) => document.getElementById(id);
  const SFX_EVENTS = ["startup", "tick", "decide", "back", "launch", "popup", "toggle", "page", "error", "insert", "eject", "favorite", "key", "boot_snap", "lid_close", "lid_open"];
  const GAME_EXT = { nds: "NDS", dsi: "NDS", srl: "NDS", ids: "NDS", gba: "GBA", agb: "GBA", gb: "GB", sgb: "GB", gbc: "GBC",
    nes: "NES", fds: "NES", sfc: "SNES", smc: "SNES", gen: "MD", md: "MD", bin: "MD", sms: "SMS", gg: "GG" };
  const ARCHIVE_EXT = ["zip", "7z", "rar", "tar", "gz", "tgz"];

  let lang = "tr";
  let drives = [];
  let manualPaths = [];
  let current = null;      // selected SD path
  let status = null;       // last /api/status
  let cheatInfo = null;    // last /api/cheats
  let payload = null;
  let games = [];          // queued files
  let mode = "flat";
  let busy = false;

  // ------------------------------------------------------------------ i18n
  const S = () => window.STRINGS[lang] || window.STRINGS.en;
  function t(key, vars) {
    let s = S()[key] ?? window.STRINGS.en[key] ?? key;
    if (vars) for (const [k, v] of Object.entries(vars)) s = s.split("{" + k + "}").join(v);
    return s;
  }
  function applyLang() {
    document.documentElement.lang = lang;
    document.querySelectorAll("[data-t]").forEach((el) => { el.textContent = t(el.dataset.t); });
    document.querySelectorAll("[data-tp]").forEach((el) => { el.placeholder = t(el.dataset.tp); });
    renderAll();
    if (typeof padCfg !== "undefined" && padCfg) { renderPadConfig(); renderPadStatus(); }
  }
  function store(k, v) { try { localStorage.setItem(k, v); } catch (_) { /* ignore */ } }
  function load(k) { try { return localStorage.getItem(k); } catch (_) { return null; } }

  // ------------------------------------------------------------------ helpers
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  function fmtBytes(n) {
    if (!n && n !== 0) return "?";
    const u = ["B", "KB", "MB", "GB", "TB"]; let i = 0;
    while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
    return (i >= 2 ? n.toFixed(1) : Math.round(n)) + " " + u[i];
  }
  let toastTimer = null;
  function toast(msg, bad) {
    const el = $("toast");
    el.textContent = msg; el.className = "toast" + (bad ? " bad" : "");
    clearTimeout(toastTimer); toastTimer = setTimeout(() => el.classList.add("hidden"), bad ? 6000 : 3500);
  }
  async function api(path, opts = {}) {
    const headers = { "X-Studio-Token": TOKEN };
    let body = opts.body;
    if (opts.json !== undefined) { headers["Content-Type"] = "application/json"; body = JSON.stringify(opts.json); }
    const q = opts.query ? "?" + new URLSearchParams(opts.query).toString() : "";
    const res = await fetch(path + q, { method: opts.method || (body ? "POST" : "GET"), headers, body });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(data.error || res.status);
    return data;
  }
  function upload(path, query, file, onProgress) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open("POST", path + "?" + new URLSearchParams(query).toString());
      xhr.setRequestHeader("X-Studio-Token", TOKEN);
      xhr.setRequestHeader("Content-Type", "application/octet-stream");
      if (onProgress) xhr.upload.onprogress = (e) => e.lengthComputable && onProgress(e.loaded / e.total);
      xhr.onload = () => {
        let data = {}; try { data = JSON.parse(xhr.responseText); } catch (_) { /* ignore */ }
        xhr.status === 200 ? resolve(data) : reject(new Error(data.error || xhr.status));
      };
      xhr.onerror = () => reject(new Error("network"));
      xhr.send(file);
    });
  }
  async function pollJob(id, onProgress) {
    for (;;) {
      await new Promise((r) => setTimeout(r, 400));
      const j = await api("/api/job", { query: { id } });
      if (onProgress) onProgress(j.progress);
      if (j.done) { if (j.error) throw new Error(j.error); return j.result; }
    }
  }
  function setProgress(id, frac) {
    const el = $(id);
    if (frac === null) { el.classList.add("hidden"); return; }
    el.classList.remove("hidden");
    el.classList.toggle("indet", frac === undefined);
    el.firstElementChild.style.width = frac === undefined ? "" : Math.round(frac * 100) + "%";
  }

  // ------------------------------------------------------------------ navigation
  $("nav").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-page]"); if (!b) return;
    document.querySelectorAll(".nav button").forEach((x) => x.classList.toggle("active", x === b));
    document.querySelectorAll(".page").forEach((p) => p.classList.toggle("active", p.id === "page-" + b.dataset.page));
    store("studio.page", b.dataset.page);
  });

  // ------------------------------------------------------------------ drives
  async function loadDrives(selectPath) {
    try {
      const d = await api("/api/drives");
      drives = d.drives || []; payload = d.payload;
    } catch (e) { toast(t("error") + ": " + e.message, true); drives = []; }
    const sel = $("driveSelect");
    const all = drives.map((x) => ({ path: x.path, label: x.name + (x.total ? " · " + fmtBytes(x.total) : "") }))
      .concat(manualPaths.filter((p) => !drives.some((x) => x.path === p)).map((p) => ({ path: p, label: t("folder") + ": " + p })));
    sel.innerHTML = "";
    if (!all.length) {
      sel.innerHTML = `<option value="">${esc(t("no_drives"))}</option>`;
    }
    for (const o of all) {
      const opt = document.createElement("option"); opt.value = o.path; opt.textContent = o.label; sel.appendChild(opt);
    }
    const want = selectPath || current || load("studio.sd");
    if (want && all.some((o) => o.path === want)) sel.value = want;
    await selectDrive(sel.value || null);
  }
  async function selectDrive(path) {
    current = path || null; status = null;
    if (current) store("studio.sd", current);
    await refreshStatus();
  }
  let prefilledFor = null;
  async function refreshStatus() {
    if (current) {
      try {
        status = await api("/api/status", { query: { sd: current } });
        profileData = status.profiles || null;
        if (status.settings && prefilledFor !== current) {
          prefilledFor = current;
          $("profileName").value = status.settings.profile || "";
          $("consoleLang").value = status.settings.lang;
          $("consoleTheme").value = String(status.settings.theme);
        }
      }
      catch (e) { status = null; profileData = null; toast(t("error") + ": " + e.message, true); }
      loadSnaps();
      loadCheats();
    } else { snaps = null; cheatInfo = null; }
    renderAll();
  }
  $("driveSelect").addEventListener("change", (e) => selectDrive(e.target.value));
  $("btnRefresh").addEventListener("click", () => loadDrives());
  $("btnManual").addEventListener("click", () => {
    const p = $("manualPath").value.trim(); if (!p) return;
    if (!manualPaths.includes(p)) manualPaths.push(p);
    loadDrives(p);
  });

  // ------------------------------------------------------------------ render
  function renderAll() {
    const has = !!(current && status);
    const drive = status && status.drive;
    const isFat = drive && drive.fat_any !== false;
    $("driveDot").className = "dot" + (has ? (isFat ? " ok" : " warn") : "");
    ["btnInstall", "btnBios", "btnFullSnap", "btnCovers", "btnCoversForce", "btnSfx", "btnSfxClassic", "btnSfxClear", "btnPhotoFolder", "btnPhotoFiles", "btnBackup", "btnRestore", "btnEject", "btnCheatsDl", "btnCheatsFile"]
      .forEach((id) => { $(id).disabled = !has || busy; });
    $("btnFormat").disabled = !(drive && drive.formattable) || busy;

    // card page
    if (drive) {
      $("driveName").textContent = drive.name + (drive.device ? "  ·  " + drive.device : "");
      $("driveBar").style.width = drive.total ? Math.round((1 - drive.free / drive.total) * 100) + "%" : "0";
      $("driveSpace").textContent = drive.total ? t("free_of", { free: fmtBytes(drive.free), total: fmtBytes(drive.total) }) : "";
      const chip = $("fsChip");
      if (drive.fat === null || drive.fat_any === null) { chip.className = "chip"; chip.textContent = drive.fs && drive.fs !== "?" ? drive.fs : t("fs_unknown"); }
      else if (drive.fat_any) { chip.className = "chip ok"; chip.textContent = t("fs_ok"); }
      else { chip.className = "chip bad"; chip.textContent = t("fs_bad", { fs: drive.fs }); }
    } else {
      $("driveName").textContent = "—"; $("driveBar").style.width = "0"; $("driveSpace").textContent = ""; $("fsChip").textContent = "";
    }
    renderStatus();
    renderSetup();
    renderGames();
    renderLibrary();
    renderCovers();
    renderSfx();
    renderProfiles();
    renderThemes();
    renderSnaps();
    if (typeof bootFor !== "undefined") loadBoot();
  }

  function renderStatus() {
    const box = $("statusBox");
    if (!status) { box.innerHTML = `<div class="muted">${esc(t("no_card"))}</div>`; return; }
    const rows = [];
    const b = status.boot;
    let bootTxt, bootCls;
    if (!b.present) { bootTxt = t("boot_none"); bootCls = "bad-t"; }
    else if (b.hyper && b.current) { bootTxt = t("boot_current"); bootCls = "ok-t"; }
    else if (b.hyper) { bootTxt = t("boot_old"); bootCls = "warn-t"; }
    else { bootTxt = t("boot_other", { t: b.title || "?" }); bootCls = "warn-t"; }
    rows.push(`<div class="st"><b>BOOT.NDS</b><span class="${bootCls}">${esc(bootTxt)}</span></div>`);
    for (const c of status.components.slice(1)) {
      rows.push(`<div class="st"><span>${esc(c.name)}</span><span class="${c.ok ? "ok-t" : "bad-t"}">${c.ok ? "✓" : "✕"}</span></div>`);
    }
    rows.push(`<div class="st"><span>GBA BIOS</span><span class="${status.gba_bios ? "ok-t" : "warn-t"}">${esc(status.gba_bios ? t("bios_present") : t("bios_missing"))}</span></div>`);
    rows.push(`<div class="st"><span>${esc(t("lib_line"))}</span><b>${esc(t("games_n", { n: status.library.total }))} · ${fmtBytes(status.library.bytes)}</b></div>`);
    rows.push(`<div class="st"><span>${esc(t("covers_line"))}</span><b>${esc(t("covers_n", { n: status.covers }))}</b></div>`);
    if (status.runtime_version) rows.push(`<div class="st"><span>${esc(t("runtime_v"))}</span><span class="muted small">${esc(status.runtime_version.split("\n").slice(0, 2).join(" · "))}</span></div>`);
    if (status.stale_cache_files) rows.push(`<div class="st"><span class="warn-t">${esc(t("stale", { n: status.stale_cache_files }))}</span></div>`);
    box.innerHTML = rows.join("");
  }

  function renderSetup() {
    const note = $("payloadNote");
    if (payload && !(payload.boot && payload.runtime)) { note.className = "note bad"; note.textContent = t("payload_missing"); $("btnInstall").disabled = true; }
    else if (status && status.drive && status.drive.fat_any === false) { note.className = "note bad"; note.textContent = t("fs_warn_install"); }
    else { note.className = "note"; note.textContent = payload && payload.runtime_version ? t("payload_ok", { v: payload.runtime_version.split("\n").slice(0, 2).join(" · ") }) : ""; }
    const chip = $("biosChip");
    if (status) { chip.className = "chip " + (status.gba_bios ? "ok" : "warn"); chip.textContent = status.gba_bios ? t("bios_present") : t("bios_missing"); }
    else chip.textContent = "";
  }

  // ------------------------------------------------------------------ format / eject
  $("btnFormat").addEventListener("click", () => {
    const drive = status && status.drive; if (!drive) return;
    $("formatName").textContent = drive.name; $("formatConfirm").value = ""; $("btnFormatGo").disabled = true;
    $("formatDlg").showModal();
  });
  $("formatConfirm").addEventListener("input", (e) => { $("btnFormatGo").disabled = e.target.value.trim() !== $("formatName").textContent.trim(); });
  $("formatDlg").addEventListener("close", async () => {
    if ($("formatDlg").returnValue !== "go") return;
    busy = true; renderAll(); toast(t("formatting"));
    try {
      const r = await api("/api/format", { json: { sd: current, confirm: $("formatConfirm").value } });
      if (!r.ok) throw new Error(r.error);
      toast(t("format_done") + (r.snapshot ? "\n" + t("snap_made", { n: r.snapshot.saves }) : ""));
      busy = false; await loadDrives(r.path);
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    busy = false; renderAll();
  });
  $("btnEject").addEventListener("click", async () => {
    busy = true; renderAll();
    try {
      const r = await api("/api/eject", { json: { sd: current } });
      if (r.ok) { toast(t("ejected")); current = null; await loadDrives(); } else toast(t("eject_fail", { m: r.message }), true);
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    busy = false; renderAll();
  });

  // ------------------------------------------------------------------ install / bios
  $("btnInstall").addEventListener("click", async () => {
    busy = true; renderAll(); setProgress("installProg", undefined);
    $("btnInstall").textContent = t("installing");
    try {
      const full = $("fullBackup").checked;
      const { job } = await api("/api/install", { json: { sd: current, lang: $("consoleLang").value, profile: $("profileName").value.trim(), theme: Number($("consoleTheme").value), full_backup: full } });
      const r = await pollJob(job, (p) => {
        if (p && p.total) { setProgress("installProg", p.done / p.total); $("btnInstall").textContent = t("backing_up", { d: p.done, t: p.total }); }
        else if (p === "runtime" || p === "boot") { setProgress("installProg", undefined); $("btnInstall").textContent = t("installing"); }
      });
      let msg = t("install_done", { n: r.extracted });
      if (r.snapshot) msg += "\n" + t("snap_made", { n: r.snapshot.saves });
      if (r.backed_up && r.backed_up.length) msg += "\n" + t("backed_up", { p: r.backed_up.join(", ") });
      toast(msg);
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    busy = false; setProgress("installProg", null); $("btnInstall").textContent = t("install_btn");
    await refreshStatus();
  });
  $("btnBios").addEventListener("click", () => $("biosInput").click());
  $("biosInput").addEventListener("change", async (e) => {
    const f = e.target.files[0]; e.target.value = ""; if (!f) return;
    if (f.size !== 16384) { toast(t("bios_bad"), true); return; }
    try {
      const r = await upload("/api/gba_bios", { sd: current }, f);
      if (!r.ok) toast(t("bios_bad"), true); else toast(r.verified ? t("bios_ok") : t("bios_warn"), !r.verified);
    } catch (err) { toast(t("error") + ": " + err.message, true); }
    refreshStatus();
  });

  // ------------------------------------------------------------------ profiles
  const PCOLORS = ["#00c3e3", "#eb4646", "#ff8c1e", "#e8b600", "#28be64", "#8c5ae6", "#f050a0", "#4664eb"];
  let profileData = null, picRev = 0, picTarget = null;
  async function loadProfiles() {
    profileData = null;
    if (current && status) { try { profileData = await api("/api/profiles", { query: { sd: current } }); } catch (_) { profileData = null; } }
    renderProfiles();
  }
  function profilePicUrl(kind, slot) {
    return `/api/${kind}?` + new URLSearchParams({ sd: current, slot, r: picRev });
  }
  async function picBlobUrl(kind, slot) {
    const res = await fetch(profilePicUrl(kind, slot), { headers: { "X-Studio-Token": TOKEN } });
    if (!res.ok) return null;
    return URL.createObjectURL(await res.blob());
  }
  function fmtMinutes(m) { return m >= 60 ? t("hm", { h: Math.floor(m / 60), m: m % 60 }) : t("min", { m }); }
  function renderProfiles() {
    const grid = $("profileGrid");
    const ok = !!(current && status && profileData) && !busy;
    $("loginToggle").disabled = !ok;
    $("btnNewProfile").disabled = !ok || (profileData && profileData.profiles.length >= profileData.max);
    if (!profileData) { grid.innerHTML = `<div class="panel muted">${esc(t("no_card"))}</div>`; $("profileCount").textContent = ""; return; }
    $("loginToggle").checked = !!profileData.login;
    $("profileCount").textContent = t("profiles_n", { n: profileData.profiles.length, max: profileData.max });
    const dmg = profileData.damaged || [];
    const warn = dmg.length ? `<div class="panel note bad">${esc(t("damaged", { files: dmg.join(", ") }))}</div>` : "";
    const single = profileData.profiles.length <= 1;
    grid.innerHTML = warn + profileData.profiles.map((p) => `
      <div class="panel profile-card" data-slot="${p.slot}" style="--pc:${PCOLORS[p.color] || PCOLORS[0]}">
        <div class="row gap">
          <div class="pavatar" data-kind="avatar"><span>${esc((p.name[0] || "?").toUpperCase())}</span></div>
          <div class="grow">
            <div class="row gap"><input type="text" class="pname" maxlength="19" value="${esc(p.name)}">
              <button class="btn ghost small-btn" data-act="rename">${esc(t("save"))}</button></div>
            <div class="muted small">${esc(t("p_stats", { time: fmtMinutes(p.minutes), n: p.games }))}${p.last_title ? " · " + esc(t("p_last", { t: p.last_title })) : ""}</div>
            <div class="muted small">${p.locked ? "🔒 " + esc(t("p_locked")) : esc(t("p_unlocked"))}${p.last ? " · " + esc(t("p_lastused")) : ""}</div>
          </div>
        </div>
        <div class="swatches">${PCOLORS.map((c, i) => `<button class="sw${i === p.color ? " on" : ""}" style="background:${c}" data-act="color" data-color="${i}" title="${esc(t("color_" + i))}"></button>`).join("")}</div>
        <div class="row gap wrap">
          <button class="btn small-btn" data-act="avatar">${esc(t("avatar_btn"))}</button>
          ${p.avatar ? `<button class="btn ghost small-btn" data-act="avatar_clear">${esc(t("avatar_remove"))}</button>` : ""}
        </div>
        <div class="pwall" data-kind="wallpaper"><span class="muted small">${esc(t("wall_none"))}</span></div>
        <div class="row gap wrap">
          <button class="btn small-btn" data-act="wallpaper">${esc(t("wall_btn"))}</button>
          ${p.wallpaper ? `<button class="btn ghost small-btn" data-act="wallpaper_clear">${esc(t("avatar_remove"))}</button>` : ""}
        </div>
        <div class="row gap wrap end mt-s">
          ${p.locked ? `<button class="btn ghost small-btn" data-act="pin">${esc(t("pin_reset"))}</button>` : ""}
          <button class="btn ghost small-btn" data-act="backup">${esc(t("p_backup"))}</button>
          <button class="btn ghost small-btn danger-t" data-act="delete" ${single ? "disabled" : ""}>${esc(t("p_delete"))}</button>
        </div>
      </div>`).join("");
    // pictures
    for (const p of profileData.profiles) {
      const card = grid.querySelector(`.profile-card[data-slot="${p.slot}"]`);
      if (p.avatar) picBlobUrl("avatar", p.slot).then((u) => { if (u) { const el = card.querySelector(".pavatar"); el.style.backgroundImage = `url(${u})`; el.classList.add("has"); } });
      if (p.wallpaper) picBlobUrl("wallpaper", p.slot).then((u) => { if (u) { const el = card.querySelector(".pwall"); el.style.backgroundImage = `url(${u})`; el.classList.add("has"); } });
    }
  }
  async function profileAction(slot, act, btn) {
    const card = btn.closest(".profile-card");
    try {
      if (act === "rename") {
        const name = card.querySelector(".pname").value.trim(); if (!name) return;
        await api("/api/profile_update", { json: { sd: current, slot, name } }); toast(t("saved"));
      } else if (act === "color") {
        await api("/api/profile_update", { json: { sd: current, slot, color: +btn.dataset.color } });
      } else if (act === "pin") {
        if (!confirm(t("pin_reset_q"))) return;
        await api("/api/profile_update", { json: { sd: current, slot, reset_pin: true } }); toast(t("pin_reset_done"));
      } else if (act === "avatar" || act === "wallpaper") {
        picTarget = { slot, kind: act }; $("profilePicInput").click(); return;
      } else if (act === "avatar_clear" || act === "wallpaper_clear") {
        await api("/api/" + act, { json: { sd: current, slot } }); picRev++;
      } else if (act === "backup") {
        const r = await api("/api/profile_backup", { json: { sd: current, slot } });
        toast(t("backup_done", { n: r.count }));
        if (r.zip) api("/api/reveal", { json: { path: r.zip } }).catch(() => {});
      } else if (act === "delete") {
        const name = card.querySelector(".pname").value;
        if (!confirm(t("p_delete_q", { name }))) return;
        const r = await api("/api/profile_delete", { json: { sd: current, slot } });
        if (!r.ok) { toast(t("p_delete_last"), true); return; }
        toast(t("p_deleted"));
      }
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    loadProfiles();
  }
  $("profileGrid").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-act]"); if (!b || b.disabled) return;
    profileAction(+b.closest(".profile-card").dataset.slot, b.dataset.act, b);
  });
  $("profilePicInput").addEventListener("change", async (e) => {
    const f = e.target.files[0]; e.target.value = ""; if (!f || !picTarget) return;
    let bmp;
    try { bmp = await createImageBitmap(f); } catch (_) { toast(t("avatar_bad"), true); return; }
    const W = picTarget.kind === "avatar" ? 64 : 256, H = picTarget.kind === "avatar" ? 64 : 192;
    const cv = document.createElement("canvas"); cv.width = W; cv.height = H;
    const ctx = cv.getContext("2d");
    ctx.imageSmoothingQuality = "high";
    // cover: crop the centre to the target shape
    const scale = Math.max(W / bmp.width, H / bmp.height);
    const sw = W / scale, sh = H / scale;
    ctx.drawImage(bmp, (bmp.width - sw) / 2, (bmp.height - sh) / 2, sw, sh, 0, 0, W, H);
    const rgba = ctx.getImageData(0, 0, W, H).data;
    try {
      const r = await upload("/api/" + picTarget.kind, { sd: current, slot: picTarget.slot }, new Blob([rgba]));
      if (!r.ok) throw new Error(r.reason);
      picRev++;
      toast(t(picTarget.kind === "avatar" ? "avatar_done" : "wall_done"));
    } catch (err) { toast(t("error") + ": " + err.message, true); }
    loadProfiles();
  });
  $("btnNewProfile").addEventListener("click", async () => {
    const name = $("newProfileName").value.trim();
    if (!name) { $("newProfileName").focus(); return; }
    try {
      const r = await api("/api/profile_create", { json: { sd: current, name } });
      if (!r.ok) { toast(t("profiles_full"), true); return; }
      $("newProfileName").value = ""; toast(t("profile_added", { name }));
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    loadProfiles();
  });
  $("loginToggle").addEventListener("change", async (e) => {
    try { await api("/api/login_screen", { json: { sd: current, on: e.target.checked } }); } catch (err) { toast(t("error") + ": " + err.message, true); }
    loadProfiles();
  });

  // ------------------------------------------------------------------ theme store
  // Every wallpaper is drawn here in the browser (no pictures are downloaded), 256x192 like the top screen.
  const THEMES = [
    { id: "night", color: 7, theme: 0, draw(g) {
        grad(g, ["#0b1030", "#1d2b6b"]);
        for (let i = 0; i < 26; i++) { g.fillStyle = `rgba(150,180,255,${0.08 + (i % 5) * 0.03})`; circle(g, rnd(i, 1) * 256, rnd(i, 2) * 192, 6 + rnd(i, 3) * 26); }
        g.fillStyle = "rgba(255,255,255,.9)"; for (let i = 0; i < 40; i++) g.fillRect(rnd(i, 4) * 256, rnd(i, 5) * 120, 1, 1);
      } },
    { id: "sakura", color: 6, theme: 1, draw(g) {
        grad(g, ["#fff1f6", "#ffd3e3"]);
        for (let i = 0; i < 34; i++) { g.save(); g.translate(rnd(i, 1) * 256, rnd(i, 2) * 192); g.rotate(rnd(i, 3) * 6.3);
          g.fillStyle = `rgba(240,110,160,${0.25 + rnd(i, 4) * 0.35})`; g.beginPath(); g.ellipse(0, 0, 5 + rnd(i, 5) * 5, 3, 0, 0, 7); g.fill(); g.restore(); }
      } },
    { id: "ocean", color: 0, theme: 0, draw(g) {
        grad(g, ["#03263d", "#06608a"]);
        for (let k = 0; k < 9; k++) { g.strokeStyle = `rgba(120,220,255,${0.08 + k * 0.025})`; g.lineWidth = 2; g.beginPath();
          for (let x = 0; x <= 256; x += 4) g.lineTo(x, 40 + k * 18 + Math.sin(x / 22 + k) * 6); g.stroke(); }
      } },
    { id: "forest", color: 4, theme: 0, draw(g) {
        grad(g, ["#0d2a1c", "#1f5a35"]);
        const layers = ["#164a2b", "#123d24", "#0c2e1b"];
        layers.forEach((c, l) => { g.fillStyle = c; for (let i = 0; i < 12; i++) { const x = i * 24 + (l * 9) % 24, h = 50 + rnd(i + l * 20, 1) * 50 + l * 18;
          g.beginPath(); g.moveTo(x - 16, 192); g.lineTo(x, 192 - h); g.lineTo(x + 16, 192); g.fill(); } });
      } },
    { id: "sunset", color: 2, theme: 1, draw(g) {
        grad(g, ["#ffcf8b", "#ff8a65", "#b35aa8"]);
        g.fillStyle = "rgba(255,240,200,.85)"; circle(g, 190, 120, 34);
        g.fillStyle = "rgba(120,40,110,.35)"; for (let i = 0; i < 6; i++) g.fillRect(0, 132 + i * 10, 256, 3 + i);
      } },
    { id: "retro", color: 1, theme: 0, draw(g) {
        grad(g, ["#14061f", "#3a0b3d"]);
        g.strokeStyle = "rgba(255,80,120,.45)"; g.lineWidth = 1;
        for (let i = 0; i < 12; i++) { const y = 110 + i * i * 0.7; g.beginPath(); g.moveTo(0, y); g.lineTo(256, y); g.stroke(); }
        for (let i = -10; i <= 10; i++) { g.beginPath(); g.moveTo(128 + i * 6, 110); g.lineTo(128 + i * 40, 192); g.stroke(); }
        g.fillStyle = "#ff5a7a"; circle(g, 128, 82, 26);
      } },
    { id: "lavender", color: 5, theme: 1, draw(g) {
        grad(g, ["#f4efff", "#ddd0ff"]);
        g.fillStyle = "rgba(140,90,230,.10)"; for (let i = -6; i < 16; i++) { g.beginPath(); g.moveTo(i * 24, 0); g.lineTo(i * 24 + 12, 0); g.lineTo(i * 24 + 112, 192); g.lineTo(i * 24 + 100, 192); g.fill(); }
      } },
    { id: "gold", color: 3, theme: 0, draw(g) {
        grad(g, ["#16130c", "#2c2414"]);
        g.strokeStyle = "rgba(232,182,0,.35)"; g.lineWidth = 1;
        for (let r = 20; r < 300; r += 18) { g.beginPath(); g.arc(256, 0, r, 0, 7); g.stroke(); }
      } },
  ];
  function rnd(i, k) { const x = Math.sin(i * 12.9898 + k * 78.233) * 43758.5453; return x - Math.floor(x); }
  function circle(g, x, y, r) { g.beginPath(); g.arc(x, y, r, 0, 7); g.fill(); }
  function grad(g, stops) { const l = g.createLinearGradient(0, 0, 0, 192); stops.forEach((c, i) => l.addColorStop(i / (stops.length - 1), c)); g.fillStyle = l; g.fillRect(0, 0, 256, 192); }
  function themeCanvas(th) {
    const cv = document.createElement("canvas"); cv.width = 256; cv.height = 192;
    th.draw(cv.getContext("2d")); return cv;
  }
  function renderThemes() {
    const grid = $("themeGrid"), sel = $("themeProfile");
    const profs = (profileData && profileData.profiles) || [];
    const keep = sel.value;
    sel.innerHTML = profs.map((p) => `<option value="${p.slot}">${esc(p.name)}</option>`).join("");
    if (keep && profs.some((p) => String(p.slot) === keep)) sel.value = keep;
    const ok = !!(current && status && profs.length) && !busy;
    if (grid.dataset.built !== lang) {
      grid.innerHTML = THEMES.map((th) => `
        <div class="panel theme-card" data-id="${th.id}" style="--pc:${PCOLORS[th.color]}">
          <div class="theme-prev"></div>
          <div class="row gap"><b class="grow">${esc(t("th_" + th.id))}</b>
            <span class="chip">${esc(t(th.theme ? "theme_light" : "theme_dark"))}</span></div>
          <button class="btn primary small-btn" data-act="apply">${esc(t("th_apply"))}</button>
        </div>`).join("");
      THEMES.forEach((th) => {
        const box = grid.querySelector(`.theme-card[data-id="${th.id}"] .theme-prev`);
        const cv = themeCanvas(th);
        cv.className = "theme-canvas"; box.appendChild(cv);
        const pill = document.createElement("div"); pill.className = "theme-pill"; pill.textContent = "DSplay"; box.appendChild(pill);
      });
      grid.dataset.built = lang;
    }
    grid.querySelectorAll("button[data-act=apply]").forEach((b) => { b.disabled = !ok; });
  }
  $("themeGrid").addEventListener("click", async (e) => {
    const b = e.target.closest("button[data-act=apply]"); if (!b || b.disabled) return;
    const th = THEMES.find((x) => x.id === b.closest(".theme-card").dataset.id);
    const slot = +$("themeProfile").value;
    try {
      const rgba = themeCanvas(th).getContext("2d").getImageData(0, 0, 256, 192).data;
      const r = await upload("/api/wallpaper", { sd: current, slot }, new Blob([rgba]));
      if (!r.ok) throw new Error(r.reason);
      await api("/api/profile_update", { json: { sd: current, slot, color: th.color, theme: th.theme } });
      toast(t("th_done", { t: t("th_" + th.id) }));
    } catch (err) { toast(t("error") + ": " + err.message, true); }
    loadProfiles();
  });

  // ------------------------------------------------------------------ games
  function crc16(bytes) {
    let crc = 0xFFFF;
    for (const b of bytes) { crc ^= b; for (let i = 0; i < 8; i++) crc = crc & 1 ? (crc >>> 1) ^ 0xA001 : crc >>> 1; }
    return crc;
  }
  async function inspectNds(file) {
    const info = { code: "", title: "", icon: null, corrupt: "" };
    if (file.size < 0x200) { info.corrupt = "header"; return info; }
    const hdr = new Uint8Array(await file.slice(0, 0x200).arrayBuffer());
    const dv = new DataView(hdr.buffer);
    if (crc16(hdr.subarray(0, 0x15E)) !== dv.getUint16(0x15E, true)) info.corrupt = "header";
    info.code = String.fromCharCode(...hdr.subarray(0x0C, 0x10)).replace(/[^\x20-\x7e]/g, "");
    const used = dv.getUint32(0x80, true);
    if (used > 0x10000 && file.size + 0x10000 < used) info.corrupt = "truncated";
    const boff = dv.getUint32(0x68, true);
    if (boff >= 0x200 && boff + 0x440 <= file.size) {
      const ban = new DataView(await file.slice(boff, boff + 0x440).arrayBuffer());
      const pal = [];
      for (let i = 0; i < 16; i++) { const c = ban.getUint16(0x220 + i * 2, true); pal.push([(c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3]); }
      const px = new Uint8ClampedArray(32 * 32 * 4);
      for (let tile = 0; tile < 16; tile++) {
        for (let y = 0; y < 8; y++) for (let x = 0; x < 8; x++) {
          const b = ban.getUint8(0x20 + tile * 32 + y * 4 + (x >> 1));
          const idx = x & 1 ? b >> 4 : b & 15;
          const X = (tile & 3) * 8 + x, Y = (tile >> 2) * 8 + y, o = (Y * 32 + X) * 4;
          const c = pal[idx]; px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2]; px[o + 3] = idx ? 255 : 0;
        }
      }
      info.icon = px;
      let title = "";
      for (let i = 0; i < 128; i++) { const ch = ban.getUint16(0x340 + i * 2, true); if (!ch) break; title += String.fromCharCode(ch); }
      const lines = title.split("\n").map((s) => s.trim()).filter(Boolean);
      info.title = (lines.length > 1 ? lines.slice(0, -1) : lines).join(" ");
    }
    return info;
  }
  async function addFiles(list) {
    const files = Array.from(list).filter((f) => !f.name.startsWith("."));
    for (const f of files) {
      const ext = f.name.split(".").pop().toLowerCase();
      const sys = GAME_EXT[ext] || (ARCHIVE_EXT.includes(ext) ? "ZIP" : null);
      if (!sys) continue;
      if (games.some((g) => g.file.name === f.name && g.file.size === f.size)) continue;
      const g = { file: f, sys, title: f.name.replace(/\.[^.]+$/, ""), state: "ready", note: "", info: null };
      if (sys === "NDS") {
        g.info = await inspectNds(f);
        if (g.info.title) g.title = g.info.title;
        if (g.info.corrupt) g.state = "corrupt";
      }
      if (f.size === 0) g.state = "corrupt";
      games.push(g);
    }
    markDuplicates();
    renderGames();
  }
  function markDuplicates() {
    const seen = new Set();
    for (const g of games) {
      if (g.state === "dup") g.state = "ready";
      if (g.state !== "ready") continue;
      const key = g.sys === "NDS" && g.info && g.info.code && g.info.code !== "####" ? "code:" + g.info.code
        : "name:" + g.title.replace(/\s*[([][^)\]]*[)\]]/g, "").trim().toLowerCase() + ":" + g.sys;
      if (seen.has(key)) g.state = "dup"; else seen.add(key);
    }
  }
  function renderGames() {
    const wrap = $("tableWrap"), body = $("gameRows");
    wrap.classList.toggle("hidden", games.length === 0);
    body.innerHTML = "";
    for (const g of games) {
      const tr = document.createElement("tr");
      const cls = { ready: "", copied: "ok-t", exists: "ok-t", skipped: "warn-t", error: "bad-t", corrupt: "bad-t", dup: "warn-t", busy: "" }[g.state] || "";
      tr.innerHTML = `<td class="icon"></td><td><div class="t">${esc(g.title)}</div><div class="f">${esc(g.file.name)}</div></td>`
        + `<td>${esc(g.sys)}</td><td>${fmtBytes(g.file.size)}</td><td class="${cls}">${esc(t("st_" + g.state))}${g.note ? `<div class="f">${esc(g.note)}</div>` : ""}</td>`;
      const cell = tr.firstChild;
      if (g.info && g.info.icon) {
        const c = document.createElement("canvas"); c.width = 32; c.height = 32;
        c.getContext("2d").putImageData(new ImageData(g.info.icon, 32, 32), 0, 0); cell.appendChild(c);
      } else {
        const d = document.createElement("div"); d.className = "ph"; d.textContent = g.sys; cell.appendChild(d);
      }
      body.appendChild(tr);
    }
    const n = games.filter((g) => g.state === "ready").length;
    $("btnCopy").textContent = n ? t("copy_btn", { n }) : t("copy_none");
    $("btnCopy").disabled = !n || !current || busy;
  }
  function renderLibrary() {
    const box = $("libBox");
    if (!status) { box.innerHTML = `<span class="muted">${esc(t("no_card"))}</span>`; return; }
    const lib = status.library;
    if (!lib.total) { box.innerHTML = `<span class="muted">${esc(t("lib_empty"))}</span>`; return; }
    box.innerHTML = `<span class="chip ok">${esc(t("games_n", { n: lib.total }))}</span>`
      + Object.entries(lib.by_system).map(([k, v]) => `<span class="chip">${esc(k)} · ${v}</span>`).join("");
  }
  $("btnFiles").addEventListener("click", () => $("fileInput").click());
  $("btnFolder").addEventListener("click", () => $("folderInput").click());
  $("fileInput").addEventListener("change", (e) => { addFiles(e.target.files); e.target.value = ""; });
  $("folderInput").addEventListener("change", (e) => { addFiles(e.target.files); e.target.value = ""; });
  const drop = $("drop");
  ["dragenter", "dragover"].forEach((ev) => drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.add("over"); }));
  ["dragleave", "drop"].forEach((ev) => drop.addEventListener(ev, () => drop.classList.remove("over")));
  drop.addEventListener("drop", async (e) => {
    e.preventDefault();
    const items = Array.from(e.dataTransfer.items || []).map((i) => i.webkitGetAsEntry && i.webkitGetAsEntry()).filter(Boolean);
    if (!items.length) return addFiles(e.dataTransfer.files);
    const files = [];
    const walk = (entry) => new Promise((res) => {
      if (entry.isFile) entry.file((f) => { files.push(f); res(); }, () => res());
      else if (entry.isDirectory) {
        const reader = entry.createReader(); const all = [];
        const next = () => reader.readEntries(async (batch) => {
          if (!batch.length) { for (const b of all) await walk(b); res(); } else { all.push(...batch); next(); }
        }, () => res());
        next();
      } else res();
    });
    for (const it of items) await walk(it);
    addFiles(files);
  });
  $("modeSeg").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-mode]"); if (!b) return;
    mode = b.dataset.mode; document.querySelectorAll("#modeSeg button").forEach((x) => x.classList.toggle("on", x === b));
  });
  $("btnClear").addEventListener("click", () => { if (!busy) { games = []; renderGames(); } });
  $("btnRemoveBad").addEventListener("click", () => { if (!busy) { games = games.filter((g) => g.state !== "corrupt" && g.state !== "dup"); renderGames(); } });
  $("btnCopy").addEventListener("click", async () => {
    const todo = games.filter((g) => g.state === "ready");
    if (!todo.length || !current) return;
    busy = true; renderAll();
    const total = todo.reduce((a, g) => a + g.file.size, 0) || 1;
    let doneBytes = 0; const counts = { c: 0, e: 0, s: 0, x: 0 };
    setProgress("copyProg", 0);
    for (let i = 0; i < todo.length; i++) {
      const g = todo[i]; g.state = "busy"; renderGames();
      try {
        const r = await upload("/api/upload_game", { sd: current, name: g.file.name, mode }, g.file,
          (f) => setProgress("copyProg", (doneBytes + f * g.file.size) / total));
        const res = r.results || [];
        const first = res[0] || {};
        const stateMap = { copied: "copied", exists: "exists", skipped: "skipped", error: "error" };
        g.state = res.some((x) => x.status === "copied") ? "copied" : (stateMap[first.status] || "error");
        g.note = first.reason === "different-file-exists" ? t("st_diff") : (first.save === "kept" ? t("st_save_kept") : (res.length > 1 ? res.length + " ROM" : (first.reason && g.state !== "exists" ? first.reason : "")));
        for (const x of res) { if (x.status === "copied") counts.c++; else if (x.status === "exists") counts.e++; else if (x.status === "skipped") counts.s++; else counts.x++; }
      } catch (err) { g.state = "error"; g.note = err.message; counts.x++; }
      doneBytes += g.file.size; renderGames();
    }
    setProgress("copyProg", null); busy = false;
    toast(t("copy_done", counts), counts.x > 0);
    await refreshStatus();
  });

  // ------------------------------------------------------------------ covers
  function renderCovers() { $("coversChip").textContent = status ? t("covers_n", { n: status.covers }) : ""; }
  async function runCovers(force) {
    busy = true; renderAll(); setProgress("coversProg", undefined); $("coversNote").textContent = "";
    try {
      const { job } = await api("/api/covers", { json: { sd: current, force } });
      const r = await pollJob(job, (p) => {
        if (p) { setProgress("coversProg", p.done / p.total); $("coversNote").textContent = t("covers_prog", { d: p.done, t: p.total, name: p.name }); }
      });
      toast(t("covers_done", { d: r.downloaded, e: r.exists, m: r.missing }));
      $("coversNote").textContent = t("covers_done", { d: r.downloaded, e: r.exists, m: r.missing })
        + (r.missing_titles.length ? "\n" + t("covers_missing") + r.missing_titles.join(", ") : "");
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    busy = false; setProgress("coversProg", null); await refreshStatus();
  }
  $("btnCovers").addEventListener("click", () => runCovers(false));
  $("btnCoversForce").addEventListener("click", () => runCovers(true));

  // ------------------------------------------------------------------ cheats (usrcheat.dat)
  async function loadCheats() {
    try { cheatInfo = await api("/api/cheats", { query: { sd: current } }); } catch (_) { cheatInfo = null; }
    renderCheats();
  }
  function renderCheats() {
    const chip = $("cheatsChip");
    if (!cheatInfo) { chip.className = "chip"; chip.textContent = ""; return; }
    if (cheatInfo.present) { chip.className = "chip ok"; chip.textContent = t("cheats_have", { n: cheatInfo.games == null ? "?" : cheatInfo.games, s: fmtBytes(cheatInfo.size) }); }
    else { chip.className = "chip warn"; chip.textContent = t("cheats_none"); }
  }
  function cheatResult(r) {
    if (r && r.ok) { toast(t("cheats_ok", { n: r.games })); $("cheatsNote").textContent = ""; }
    else if (r && r.reason === "download") {
      toast(t("cheats_dl_fail"), true);
      $("cheatsNote").innerHTML = esc(t("cheats_dl_fail")) + ' <a href="' + esc(r.info_url) + '" target="_blank" rel="noopener">GBAtemp</a>';
    } else toast(t("cheats_bad"), true);
  }
  $("btnCheatsDl").addEventListener("click", async () => {
    busy = true; renderAll(); setProgress("cheatsProg", undefined);
    try {
      const { job } = await api("/api/cheats_download", { json: { sd: current } });
      cheatResult(await pollJob(job, (p) => { if (p && p.total) setProgress("cheatsProg", p.done / p.total); }));
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    busy = false; setProgress("cheatsProg", null); renderAll(); loadCheats();
  });
  $("btnCheatsFile").addEventListener("click", () => $("cheatsInput").click());
  $("cheatsInput").addEventListener("change", async (e) => {
    const f = e.target.files[0]; e.target.value = ""; if (!f) return;
    busy = true; renderAll(); setProgress("cheatsProg", 0);
    try { cheatResult(await upload("/api/cheats_upload", { sd: current }, f, (x) => setProgress("cheatsProg", x))); }
    catch (err) { toast(t("cheats_bad"), true); }
    busy = false; setProgress("cheatsProg", null); renderAll(); loadCheats();
  });

  // ------------------------------------------------------------------ store (Universal-DB homebrew)
  let storeItems = null, storeCat = 0, storeLoading = false, storeWifi = false;
  const storeIcons = {};
  async function loadStore(force) {
    if (storeLoading) return;
    storeLoading = true; setProgress("storeProg", undefined); $("storeChip").textContent = "";
    try {
      const q = { refresh: force ? "1" : "0" }; if (current) q.sd = current;
      const r = await api("/api/store", { query: q });
      if (r.offline) { storeItems = []; $("storeChip").className = "chip bad"; $("storeChip").textContent = t("store_offline"); }
      else { storeItems = r.items; storeWifi = !!(r.wifi && r.wifi.connected); $("storeChip").className = "chip"; $("storeChip").textContent = t("store_n", { n: storeItems.length }); }
    } catch (e) { storeItems = []; toast(t("error") + ": " + e.message, true); }
    storeLoading = false; setProgress("storeProg", null); renderStore();
  }
  async function storeIcon(img, slug) {
    if (storeIcons[slug] !== undefined) { if (storeIcons[slug]) img.src = storeIcons[slug]; return; }
    storeIcons[slug] = null;
    try {
      const res = await fetch("/api/store_icon?" + new URLSearchParams({ slug }), { headers: { "X-Studio-Token": TOKEN } });
      if (res.ok) { storeIcons[slug] = URL.createObjectURL(await res.blob()); img.src = storeIcons[slug]; }
    } catch (_) { /* no icon */ }
  }
  function renderStore() {
    const grid = $("storeGrid");
    if (!storeItems) { grid.innerHTML = ""; return; }
    const q = $("storeSearch").value.trim().toLowerCase();
    const list = storeItems.filter((it) => (storeCat === 0 || it.category === storeCat) &&
      (!q || it.title.toLowerCase().includes(q) || it.author.toLowerCase().includes(q) || it.description.toLowerCase().includes(q)));
    grid.innerHTML = list.map((it) => {
      const size = it.size ? fmtBytes(it.size) : "";
      const btns = [];
      if (current) btns.push(`<button class="btn ${it.installed ? "ghost" : "primary"}" data-act="card" ${busy ? "disabled" : ""}>${esc(t(it.installed ? "store_again" : "store_card"))}</button>`);
      if (storeWifi) btns.push(`<button class="btn ghost" data-act="wifi" ${busy ? "disabled" : ""}>${esc(t("store_wifi"))}</button>`);
      if (it.installed && current) btns.push(`<button class="btn ghost" data-act="remove" ${busy ? "disabled" : ""}>${esc(t("store_remove"))}</button>`);
      return `<div class="store-card" data-slug="${esc(it.slug)}">
        <div class="store-head"><img class="store-ico" alt="" data-slug="${esc(it.slug)}">
          <div><div class="store-title">${esc(it.title)}</div>
          <div class="store-meta">${esc(it.author)}${it.version ? " · " + esc(it.version) : ""}${size ? " · " + size : ""} · ★ ${it.stars}</div></div></div>
        <div class="store-desc">${esc(it.description)}</div>
        <div class="row gap wrap">${btns.join("")}${it.installed ? `<span class="chip ok">${esc(t("store_installed"))}</span>` : ""}
          ${it.website ? `<a class="small" href="${esc(it.website)}" target="_blank" rel="noopener">${esc(t("store_web"))}</a>` : ""}</div></div>`;
    }).join("") || `<div class="muted">${esc(t(storeItems.length ? "lib_empty" : "store_offline"))}</div>`;
    grid.querySelectorAll("img.store-ico").forEach((img) => storeIcon(img, img.dataset.slug));
  }
  $("storeSeg").addEventListener("click", (e) => {
    const b = e.target.closest("button[data-cat]"); if (!b) return;
    storeCat = Number(b.dataset.cat);
    document.querySelectorAll("#storeSeg button").forEach((x) => x.classList.toggle("on", x === b));
    renderStore();
  });
  $("storeSearch").addEventListener("input", () => renderStore());
  $("btnStoreRefresh").addEventListener("click", () => loadStore(true));
  $("storeGrid").addEventListener("click", async (e) => {
    const b = e.target.closest("button[data-act]"); if (!b) return;
    const slug = b.closest(".store-card").dataset.slug;
    const it = storeItems.find((x) => x.slug === slug);
    busy = true; renderAll(); renderStore(); setProgress("storeProg", 0);
    try {
      if (b.dataset.act === "remove") {
        await api("/api/store_remove", { json: { sd: current, slug } });
        toast(t("store_removed", { t: it.title }));
      } else {
        const path = b.dataset.act === "wifi" ? "/api/store_install_wifi" : "/api/store_install";
        const { job } = await api(path, { json: { sd: current, slug } });
        const r = await pollJob(job, (p) => { if (p && p.total) setProgress("storeProg", p.done / p.total); });
        if (r && r.ok) toast(t("store_done", { t: it.title }));
        else toast(t(r && r.reason === "space" ? "store_space" : "store_fail"), true);
      }
    } catch (err) { toast(t("store_fail") + ": " + err.message, true); }
    busy = false; setProgress("storeProg", null); renderAll(); await loadStore(false); refreshStatus();
  });
  document.querySelector('.nav button[data-page="store"]').addEventListener("click", () => { if (!storeItems) loadStore(false); });

  // ------------------------------------------------------------------ photos
  // every picture is scaled here (the browser opens JPEG, PNG, WebP, HEIC on Safari...) to at most
  // 640x480 and sent as a JPEG the console decodes quickly
  function photoNote() {
    const albums = (status && status.albums) || [];
    $("photoNote").textContent = albums.length ? t("photos_albums", { list: albums.map((a) => `${a.name} (${a.count})`).join(", ") }) : "";
  }
  async function photoToJpeg(file) {
    const bmp = await createImageBitmap(file);
    const k = Math.min(1, 640 / bmp.width, 480 / bmp.height);
    const w = Math.max(1, Math.round(bmp.width * k)), h = Math.max(1, Math.round(bmp.height * k));
    const cv = document.createElement("canvas");
    cv.width = w; cv.height = h;
    const cx = cv.getContext("2d");
    cx.imageSmoothingQuality = "high";
    cx.drawImage(bmp, 0, 0, w, h);
    if (bmp.close) bmp.close();
    return await new Promise((res) => cv.toBlob(res, "image/jpeg", 0.88));
  }
  async function sendPhotos(files) {
    const pics = files.filter((f) => /^image\//.test(f.type) || /\.(jpe?g|png|webp|heic|gif|bmp)$/i.test(f.name));
    if (!pics.length) { toast(t("photos_none"), true); return; }
    const album = ($("photoAlbum").value || "").trim() || (pics[0].webkitRelativePath ? pics[0].webkitRelativePath.split("/")[0] : "Studio");
    let sent = 0, same = 0, bad = 0;
    for (let i = 0; i < pics.length; i++) {
      setProgress("photoProg", i / pics.length);
      try {
        const jpg = await photoToJpeg(pics[i]);
        const r = await upload("/api/photo", { sd: current, album, name: pics[i].name }, jpg);
        if (r.ok && r.status === "exists") same++; else if (r.ok) sent++; else bad++;
      } catch (e) { bad++; }
    }
    setProgress("photoProg", null);
    toast(t("photos_done", { n: sent, s: same, b: bad }));
    refreshStatus();
  }
  $("btnPhotoFolder").addEventListener("click", () => $("photoFolderInput").click());
  $("btnPhotoFiles").addEventListener("click", () => $("photoFilesInput").click());
  $("photoFolderInput").addEventListener("change", (e) => { const f = Array.from(e.target.files); e.target.value = ""; sendPhotos(f); });
  $("photoFilesInput").addEventListener("change", (e) => { const f = Array.from(e.target.files); e.target.value = ""; sendPhotos(f); });

  // ------------------------------------------------------------------ sounds
  function renderSfx() {
    photoNote();
    const on = new Set((status && status.sfx) || []);
    $("sfxGrid").innerHTML = SFX_EVENTS.map((ev) => `<div class="sfx ${on.has(ev) ? "on" : ""}" title="${ev}.wav"><span>${esc(t("ev_" + ev))}</span>`
      + `<span class="muted small">${esc(on.has(ev) ? t("sfx_on") : t("sfx_default"))}</span></div>`).join("");
  }
  $("btnSfx").addEventListener("click", () => $("sfxInput").click());
  $("sfxInput").addEventListener("change", async (e) => {
    const files = Array.from(e.target.files); e.target.value = "";
    let n = 0;
    for (const f of files) {
      const ev = f.name.replace(/\.wav$/i, "").toLowerCase();
      if (!SFX_EVENTS.includes(ev)) { toast(t("sfx_bad_name", { f: f.name }), true); continue; }
      try { const r = await upload("/api/sfx", { sd: current, event: ev }, f); if (r.ok) n++; else toast(f.name + ": " + r.reason, true); }
      catch (err) { toast(t("error") + ": " + err.message, true); }
    }
    if (n) toast(t("sfx_done", { n }));
    refreshStatus();
  });
  $("btnSfxClassic").addEventListener("click", async () => {
    try { const r = await api("/api/sfx_classic", { json: { sd: current } }); if (r.ok) toast(t("sfx_classic_done")); } catch (e) { toast(t("error") + ": " + e.message, true); }
    refreshStatus();
  });
  $("btnSfxClear").addEventListener("click", async () => {
    try { await api("/api/sfx_clear", { json: { sd: current } }); toast(t("sfx_cleared")); } catch (e) { toast(t("error") + ": " + e.message, true); }
    refreshStatus();
  });

  // ------------------------------------------------------------------ saves
  $("btnBackup").addEventListener("click", async () => {
    try {
      const r = await api("/api/backup_saves", { json: { sd: current } });
      const note = $("savesNote");
      if (!r.count) { note.textContent = t("no_saves"); return; }
      note.innerHTML = `${esc(t("backup_done", { n: r.count }))}<br><code>${esc(r.zip)}</code> <button class="btn ghost" id="btnReveal">${esc(t("show"))}</button>`;
      $("btnReveal").onclick = () => api("/api/reveal", { json: { path: r.zip } }).catch(() => {});
      toast(t("backup_done", { n: r.count }));
    } catch (e) { toast(t("error") + ": " + e.message, true); }
  });
  $("btnRestore").addEventListener("click", () => { if (confirm(t("restore_confirm"))) $("restoreInput").click(); });
  $("restoreInput").addEventListener("change", async (e) => {
    const f = e.target.files[0]; e.target.value = ""; if (!f) return;
    try { const r = await upload("/api/restore_saves", { sd: current }, f); toast(t("restore_done", { n: r.restored })); }
    catch (err) { toast(t("error") + ": " + err.message, true); }
  });


  // ------------------------------------------------------------------ custom boot screen
  let bootSt = null, bootImg = [null, null], bootWav = null, bootAudio = null, bootFor = null, bootPicSlot = 0;
  let bootServerPics = [null, null];
  function coverDraw(g, img, x, y, w, h) {
    const s = Math.max(w / img.width, h / img.height), sw = w / s, sh = h / s;
    g.drawImage(img, (img.width - sw) / 2, (img.height - sh) / 2, sw, sh, x, y, w, h);
  }
  function renderBootPreview() {
    const top = $("bootTop").getContext("2d"), bot = $("bootBot").getContext("2d");
    const layout = $("bootLayout").value;
    top.fillStyle = bot.fillStyle = "#000"; top.fillRect(0, 0, 256, 192); bot.fillRect(0, 0, 256, 192);
    const a = bootImg[0] || bootServerPics[0], b = bootImg[1] || bootServerPics[1];
    if (layout === "span" && bootImg[0]) {
      const c = document.createElement("canvas"); c.width = 256; c.height = 384;
      coverDraw(c.getContext("2d"), bootImg[0], 0, 0, 256, 384);
      top.drawImage(c, 0, 0, 256, 192, 0, 0, 256, 192); bot.drawImage(c, 0, 192, 256, 192, 0, 0, 256, 192);
    } else {
      if (a) coverDraw(top, a, 0, 0, 256, 192);
      if (b && layout !== "top") coverDraw(bot, b, 0, 0, 256, 192);
    }
    $("btnBootPic2").classList.toggle("hidden", layout !== "two");
    $("btnBootSave").disabled = !current || !(a);
    $("btnBootRemove").disabled = !current || !(bootSt && (bootSt.top || bootSt.sound));
    $("btnBootPlay").classList.toggle("hidden", !(bootAudio || (bootSt && bootSt.sound)));
    $("btnBootNoSound").classList.toggle("hidden", !(bootWav || (bootSt && bootSt.sound)));
    $("bootSoundInfo").textContent = bootWav ? t("boot_sound_new", { s: bootWav.seconds }) : bootSt && bootSt.sound ? t("boot_sound_card", { s: bootSt.sound }) : t("boot_sound_none");
    const n = $("bootNote");
    n.textContent = bootSt && bootSt.top ? t("boot_on_card", { n: bootSt.profiles_using }) : t("boot_hint");
  }
  async function loadBoot() {
    if (!current) { bootSt = null; renderBootPreview(); return; }
    if (bootFor === current && bootSt) return;
    bootFor = current; bootImg = [null, null]; bootWav = null; bootAudio = null; bootServerPics = [null, null];
    try {
      bootSt = await api("/api/boot", { query: { sd: current } });
      $("bootMs").value = String([1500, 2500, 4000].reduce((p, v) => Math.abs(v - bootSt.ms) < Math.abs(p - bootSt.ms) ? v : p, 2500));
      $("bootStyle").value = bootSt.style;
      for (const [i, w] of [[0, "top"], [1, "bottom"]]) {
        if (!bootSt[w]) continue;
        const r = await fetch("/api/boot_png?" + new URLSearchParams({ sd: current, which: w }), { headers: { "X-Studio-Token": TOKEN } });
        if (r.ok) bootServerPics[i] = await createImageBitmap(await r.blob());
      }
      if (bootSt.bottom) $("bootLayout").value = "two";
    } catch (e) { bootSt = null; }
    renderBootPreview();
  }
  $("bootLayout").addEventListener("change", renderBootPreview);
  $("btnBootPic").addEventListener("click", () => { bootPicSlot = 0; $("bootPicInput").click(); });
  $("btnBootPic2").addEventListener("click", () => { bootPicSlot = 1; $("bootPicInput").click(); });
  $("bootPicInput").addEventListener("change", async (e) => {
    const f = e.target.files[0]; e.target.value = ""; if (!f) return;
    try { bootImg[bootPicSlot] = await createImageBitmap(f); } catch (_) { toast(t("avatar_bad"), true); return; }
    renderBootPreview();
  });
  // any audio the browser can play -> 16 kHz mono 16-bit WAV, at most 5 s, gentle end
  async function audioToWav(file) {
    const AC = window.AudioContext || window.webkitAudioContext;
    const ctx = new AC();
    const buf = await ctx.decodeAudioData(await file.arrayBuffer());
    ctx.close();
    const secs = Math.min(5, buf.duration), rate = 16000, n = Math.max(1, Math.floor(secs * rate));
    const off = new OfflineAudioContext(1, n, rate);
    const src = off.createBufferSource(); src.buffer = buf; src.connect(off.destination); src.start();
    const out = (await off.startRendering()).getChannelData(0);
    let peak = 0; for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(out[i]));
    const gain = peak > 0 ? Math.min(4, 0.9 / peak) : 1, fade = Math.min(n, Math.floor(rate * 0.08));
    const pcm = new DataView(new ArrayBuffer(44 + n * 2));
    const str = (o, s) => { for (let i = 0; i < s.length; i++) pcm.setUint8(o + i, s.charCodeAt(i)); };
    str(0, "RIFF"); pcm.setUint32(4, 36 + n * 2, true); str(8, "WAVEfmt "); pcm.setUint32(16, 16, true);
    pcm.setUint16(20, 1, true); pcm.setUint16(22, 1, true); pcm.setUint32(24, rate, true); pcm.setUint32(28, rate * 2, true);
    pcm.setUint16(32, 2, true); pcm.setUint16(34, 16, true); str(36, "data"); pcm.setUint32(40, n * 2, true);
    for (let i = 0; i < n; i++) {
      let v = out[i] * gain; if (i >= n - fade) v *= (n - i) / fade;
      pcm.setInt16(44 + i * 2, Math.max(-32768, Math.min(32767, Math.round(v * 32767))), true);
    }
    return { blob: new Blob([pcm.buffer], { type: "audio/wav" }), seconds: Math.round(secs * 10) / 10, cut: buf.duration > 5 };
  }
  $("btnBootSound").addEventListener("click", () => $("bootSoundInput").click());
  $("bootSoundInput").addEventListener("change", async (e) => {
    const f = e.target.files[0]; e.target.value = ""; if (!f) return;
    try {
      bootWav = await audioToWav(f);
      bootAudio = new Audio(URL.createObjectURL(bootWav.blob));
      if (bootWav.cut) toast(t("boot_sound_cut"));
    } catch (_) { toast(t("boot_sound_bad"), true); }
    renderBootPreview();
  });
  $("btnBootNoSound").addEventListener("click", () => { bootWav = null; bootAudio = null; if (bootSt) bootSt.sound = null; bootSt && (bootSt.clearSound = true); renderBootPreview(); });
  function bootPlay() {
    const style = $("bootStyle").value, pv = $("bootPreview").querySelectorAll("canvas");
    pv.forEach((c) => { c.style.transition = "none"; c.style.filter = style === "flash" ? "brightness(6)" : "brightness(0)"; });
    requestAnimationFrame(() => requestAnimationFrame(() => pv.forEach((c) => { c.style.transition = "filter " + (style === "flash" ? ".4s" : ".3s") + " ease-out"; c.style.filter = "brightness(1)"; })));
    const a = bootAudio || (bootSt && bootSt.sound ? null : null);
    if (a) { a.currentTime = 0; a.play().catch(() => {}); }
  }
  $("btnBootPlay").addEventListener("click", bootPlay);
  $("bootPreview").addEventListener("click", bootPlay);
  $("btnBootSave").addEventListener("click", async () => {
    const layout = $("bootLayout").value;
    busy = true; $("btnBootSave").disabled = true;
    try {
      const rgba = (id) => new Blob([$(id).getContext("2d").getImageData(0, 0, 256, 192).data]);
      if (bootImg[0] || layout === "span") await upload("/api/boot_pic", { sd: current, which: "top" }, rgba("bootTop"));
      if (layout !== "top" && (bootImg[0] || bootImg[1] || layout === "span")) await upload("/api/boot_pic", { sd: current, which: "bottom" }, rgba("bootBot"));
      if (bootWav) { const r = await upload("/api/boot_sound", { sd: current }, bootWav.blob); if (!r.ok) throw new Error(t("boot_sound_bad")); }
      const r = await api("/api/boot_save", { json: { sd: current, ms: +$("bootMs").value, style: $("bootStyle").value, clear_bottom: layout === "top",
        clear_sound: !bootWav && !!(bootSt && bootSt.clearSound), use: true } });
      toast(t("boot_saved", { n: r.profiles }));
      bootFor = null; await loadBoot();
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    busy = false; renderBootPreview();
  });
  $("btnBootRemove").addEventListener("click", async () => {
    if (!confirm(t("boot_remove_q"))) return;
    try { await api("/api/boot_remove", { json: { sd: current } }); toast(t("boot_removed")); bootFor = null; await loadBoot(); }
    catch (e) { toast(t("error") + ": " + e.message, true); }
  });

  // ------------------------------------------------------------------ card snapshots / going back
  let snaps = null;
  async function loadSnaps() {
    if (!current) { snaps = null; return; }
    try { snaps = await api("/api/snapshots", { query: { sd: current } }); } catch (e) { snaps = null; }
    renderSnaps(); renderFullBackupOption();
  }
  function snapOriginal() {
    const l = (snaps && snaps.snapshots) || [];
    return l.find((s) => s.kind === "original" && !s.on_card) || l.find((s) => s.kind === "original");
  }
  function renderFullBackupOption() {
    const cb = $("fullBackup"); if (!cb) return;
    const size = snaps ? snaps.full_size : 0;
    $("fullBackupLabel").textContent = t("full_backup_cb", { size: fmtBytes(size || 0) });
    const room = snaps && snaps.computer_free != null && snaps.computer_free > size * 1.1 + 100e6;
    cb.disabled = !room;
    if (!cb.dataset.touched) cb.checked = !!(room && status && status.boot && !status.boot.hyper && !snapOriginal());
  }
  $("fullBackup").addEventListener("change", (e) => { e.target.dataset.touched = "1"; });
  const SNAP_KINDS = { original: "snap_k_original", install: "snap_k_install", full: "snap_k_full", saves: "snap_k_saves" };
  function renderSnaps() {
    const list = $("snapList"); if (!list) return;
    const has = !!(current && snaps);
    $("btnUninstall").disabled = !has || busy || !snapOriginal();
    $("btnFullSnap").disabled = !has || busy;
    $("snapSize").textContent = has ? t("snap_full_size", { size: fmtBytes(snaps.full_size || 0) }) : "";
    if (!has) { list.innerHTML = ""; return; }
    const l = snaps.snapshots;
    if (!l.length) { list.innerHTML = `<div class="muted">${esc(t("snap_none"))}</div>`; return; }
    list.innerHTML = l.map((s, i) => {
      const d = (s.created || "").replace("_", " ").replace(/-(\d\d)-(\d\d)$/, ":$1:$2");
      return `<div class="snap"><b>${esc(t(SNAP_KINDS[s.kind] || s.kind))}${s.full && s.kind !== "full" ? " · " + esc(t("snap_k_full")) : ""}${s.on_card ? " · " + esc(t("snap_on_card")) : ""}</b>
        <span class="grow">${esc(d)} · ${esc(t("snap_saves", { n: s.saves }))} · ${esc(fmtBytes(s.size))}</span>
        ${s.on_card ? "" : `<button class="btn ghost small-btn" data-reveal="${i}">${esc(t("show"))}</button>`}
        <button class="btn small-btn" data-restore="${i}">${esc(t(s.kind === "saves" ? "snap_restore_saves" : "snap_restore"))}</button></div>`;
    }).join("");
    list.querySelectorAll("[data-reveal]").forEach((b) => { b.onclick = () => api("/api/reveal", { json: { path: l[+b.dataset.reveal].path } }).catch(() => {}); });
    list.querySelectorAll("[data-restore]").forEach((b) => { b.onclick = () => snapRestore(l[+b.dataset.restore]); });
  }
  async function snapRestore(s) {
    const q = s.kind === "saves" ? t("snap_restore_saves_q") : t("snap_restore_q", { k: t(SNAP_KINDS[s.kind] || s.kind) });
    if (!confirm(q)) return;
    busy = true; renderAll(); setProgress("snapProg", undefined);
    try {
      const { job } = await api("/api/snapshot_restore", { json: { sd: current, id: s.id } });
      const r = await pollJob(job, (p) => { if (p && p.total) setProgress("snapProg", p.done / p.total); });
      const lines = [t("snap_restored", { n: r.restored, m: r.removed })];
      if (r.conflicts.length) lines.push(t("snap_conflicts", { n: r.conflicts.length }));
      if (r.kept_saves.length) lines.push(t("snap_kept", { n: r.kept_saves.length }));
      if (r.errors.length) lines.push(t("snap_errors", { e: r.errors.slice(0, 3).join(", ") }));
      lines.push(t("snap_safety", { p: r.safety }));
      $("snapNote").className = "note" + (r.errors.length ? " bad" : "");
      $("snapNote").textContent = lines.join("\n");
      toast(lines[0]);
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    busy = false; setProgress("snapProg", null);
    await refreshStatus();
  }
  $("btnUninstall").addEventListener("click", () => { const o = snapOriginal(); if (o) snapRestore(o); });
  $("btnFullSnap").addEventListener("click", async () => {
    if (snaps && snaps.computer_free != null && snaps.computer_free < snaps.full_size * 1.1) { toast(t("snap_no_room"), true); return; }
    busy = true; renderAll(); setProgress("snapProg", 0);
    try {
      const { job } = await api("/api/snapshot", { json: { sd: current, kind: "full" } });
      const r = await pollJob(job, (p) => { if (p && p.total) { setProgress("snapProg", p.done / p.total); $("btnFullSnap").textContent = t("backing_up", { d: p.done, t: p.total }); } });
      toast(t("snap_full_done", { n: r.copied, size: fmtBytes(r.size) }));
    } catch (e) { toast(t("error") + ": " + e.message, true); }
    busy = false; setProgress("snapProg", null); $("btnFullSnap").textContent = t("full_snap_btn");
    await loadSnaps(); renderAll();
  });


  // ------------------------------------------------------------------ wireless transfer
  let wifiSt = null, wifiBusy = false;
  async function wifiRefresh() {
    try { wifiSt = await api("/api/wifi"); } catch (_) { wifiSt = null; }
    const on = !!(wifiSt && wifiSt.connected);
    $("wifiChip").className = "chip " + (on ? "ok" : "warn");
    $("wifiChip").textContent = on ? t("wifi_on", { name: wifiSt.name }) : t("wifi_off");
    $("wifiHint").textContent = on ? t("wifi_hint_on") : t("wifi_hint_off");
    ["btnWifiPick", "btnWifiSaves", "btnWifiCovers"].forEach((id) => { $(id).disabled = !on || wifiBusy; });
  }
  async function wifiJob(start, prog, note, done) {
    wifiBusy = true; wifiRefresh(); setProgress(prog, 0);
    try {
      const { job } = await start();
      const r = await pollJob(job, (p) => { if (p && p.total) { setProgress(prog, p.done / p.total); $(note).textContent = p.name || ""; } });
      $(note).className = "note"; $(note).textContent = done(r);
    } catch (e) { $(note).className = "note bad"; $(note).textContent = t("error") + ": " + e.message; }
    wifiBusy = false; setProgress(prog, null); wifiRefresh();
  }
  async function wifiSend(files) {
    if (!wifiSt || !wifiSt.connected || wifiBusy) return;
    const lines = [];
    for (const f of files) {
      await wifiJob(() => upload("/api/wifi_upload", { name: f.name, mode }, f, (fr) => setProgress("wifiProg", fr * 0.3)), "wifiProg", "wifiNote",
        (r) => { r.files.forEach((x) => lines.push(`${x.file}: ${t("wifi_st_" + x.status) || x.status}`)); return lines.join("\n"); });
    }
  }
  $("btnWifiPick").addEventListener("click", () => $("wifiInput").click());
  $("wifiInput").addEventListener("change", (e) => { const fs = [...e.target.files]; e.target.value = ""; wifiSend(fs); });
  $("wifiDrop").addEventListener("dragover", (e) => { e.preventDefault(); $("wifiDrop").classList.add("over"); });
  $("wifiDrop").addEventListener("dragleave", () => $("wifiDrop").classList.remove("over"));
  $("wifiDrop").addEventListener("drop", (e) => { e.preventDefault(); $("wifiDrop").classList.remove("over"); wifiSend([...e.dataTransfer.files]); });
  $("btnWifiSaves").addEventListener("click", () => wifiJob(() => api("/api/wifi_saves", { json: {} }), "wifiProg2", "wifiNote2",
    (r) => t("wifi_saves_done", { n: r.count, p: r.zip })));
  $("btnWifiCovers").addEventListener("click", () => wifiJob(() => api("/api/wifi_covers", { json: {} }), "wifiProg2", "wifiNote2",
    (r) => t("wifi_covers_done", { n: r.sent })));
  (function wifiTick() {
    if ($("page-wifi").classList.contains("active") && !document.hidden && !wifiBusy) wifiRefresh();
    setTimeout(wifiTick, 2000);
  })();

  // ------------------------------------------------------------------ controller (Kumanda)
  const PAD_COLORS = ["#00c3e3", "#eb4646", "#ff8c1e", "#e8b600", "#28be64", "#8c5ae6", "#f050a0", "#4664eb", "#60626c", "#3c3e46"];
  const PAD_KEYS = ["A", "B", "X", "Y", "L", "R", "UP", "DOWN", "LEFT", "RIGHT", "START", "SELECT", "MIC"];
  let padSt = null;          // last /api/pad
  let padCfg = null;         // edited copy of the config
  let padDirty = false;
  let padPage = 0;
  let padSel = -1;           // index of the selected widget on the page
  let padTimer = null;

  function padActionOptions(kind) {
    if (kind === "stick") return ["pad:ls", "pad:rs", "mouse", "keys:wasd", "keys:arrows", "scroll", "none"];
    if (kind === "trackpad") return ["mouse", "scroll", "none"];
    if (kind === "slider") return ["pad:lt", "pad:rt", "none"];
    const keys = (padSt && padSt.keys) || [];
    return ["none"].concat((padSt ? padSt.pad_buttons : []).map((b) => "pad:" + b), ["pad:lt", "pad:rt"],
      ["mouse:left", "mouse:right", "mouse:middle", "scroll:up", "scroll:down"], keys.map((k) => "key:" + k));
  }
  function padActionName(a) {
    if (a === "none") return t("pad_a_none");
    if (a === "mouse") return t("pad_a_mouse");
    if (a === "scroll") return t("pad_a_scroll");
    if (a === "keys:wasd") return "W A S D";
    if (a === "keys:arrows") return t("pad_a_arrows");
    const [k, v] = a.split(":");
    if (k === "pad") return (v === "ls" || v === "rs") ? t("pad_a_" + v) : "🎮 " + v.toUpperCase();
    if (k === "mouse") return "🖱 " + t("pad_a_" + v);
    if (k === "scroll") return "🖱 " + t("pad_a_scroll") + (v === "up" ? " ↑" : " ↓");
    if (k === "key") return "⌨ " + v.split("+").map((x) => x.length === 1 ? x.toUpperCase() : x).join(" + ");
    return a;
  }
  function padSelect(kind, value, onChange) {
    const sel = document.createElement("select");
    const opts = padActionOptions(kind);
    if (!opts.includes(value)) opts.push(value);
    const groups = {};
    for (const a of opts) {
      const g = a.split(":")[0];
      if (!groups[g]) { groups[g] = document.createElement("optgroup"); groups[g].label = t("pad_g_" + g) || g; sel.appendChild(groups[g]); }
      const o = document.createElement("option"); o.value = a; o.textContent = padActionName(a); groups[g].appendChild(o);
    }
    if (kind === "button") {
      const o = document.createElement("option"); o.value = "__combo"; o.textContent = t("pad_combo"); sel.appendChild(o);
    }
    sel.value = value;
    sel.addEventListener("change", () => {
      if (sel.value === "__combo") {
        const v = prompt(t("pad_combo_q"), value.startsWith("key:") ? value.slice(4) : "ctrl+c");
        if (!v) { sel.value = value; return; }
        const combo = "key:" + v.toLowerCase().replace(/\s+/g, "");
        const parts = combo.slice(4).split("+");
        if (!parts.length || parts.length > 4 || !parts.every((p) => padSt.keys.includes(p))) { toast(t("pad_combo_bad"), true); sel.value = value; return; }
        onChange(combo); return;
      }
      onChange(sel.value);
    });
    return sel;
  }
  function padMarkDirty() { padDirty = true; $("padDirty").textContent = t("pad_unsaved"); }

  async function padRefresh() {
    try {
      const st = await api("/api/pad", { query: current ? { sd: current } : {} });
      const first = !padSt;
      padSt = st;
      if (first || (!padDirty && JSON.stringify(st.config) !== JSON.stringify(padCfgSaved))) {
        padCfgSaved = st.config; padCfg = JSON.parse(JSON.stringify(st.config));
        if (padPage >= padCfg.layout.pages.length) padPage = 0;
        renderPadConfig();
      }
      if (first && st.wifi) { $("padSsid").value = st.wifi.ssid || ""; }
      renderPadStatus();
    } catch (e) { /* server busy */ }
  }
  let padCfgSaved = null;

  function renderPadStatus() {
    const st = padSt; if (!st) return;
    $("padRun").className = "chip " + (st.running ? "ok" : "warn");
    $("padRun").textContent = st.running ? t("pad_running", { pc: st.pc_name }) : (st.error ? t("pad_port_err") : t("pad_stopped"));
    $("btnPadRun").textContent = st.running ? t("pad_stop") : t("pad_start");
    const hints = [];
    if (st.hint) hints.push(t("pad_hint_" + st.hint));
    if (st.running) hints.push(t("pad_firewall"));
    $("padHint").innerHTML = hints.map((h) => esc(h)).join("<br><br>") +
      (st.hint === "gamepad_missing" || st.hint === "gamepad_driver" ? ` <button class="btn small-btn" id="btnPadGp">${esc(t("pad_gp_install"))}</button>` : "");
    const gp = $("btnPadGp");
    if (gp) gp.onclick = async () => {
      gp.disabled = true; gp.textContent = t("pad_gp_installing");
      try { const r = await api("/api/pad_install_gamepad", { json: {} }); await pollJob(r.job); toast(t("pad_gp_done")); }
      catch (e) { toast(t("error") + ": " + e.message, true); gp.disabled = false; gp.textContent = t("pad_gp_install"); }
    };
    $("btnPadPair").disabled = !st.running;
    $("padDiag").textContent = st.running ? t("pad_diag", { ips: (st.local_ips || []).join(", ") || "?", n: st.rx }) : "";
    $("padDevs").innerHTML = st.devices.length ? st.devices.map((d) => `<div class="pad-dev"><b>${esc(d.name)}</b>
      <span class="chip ${d.active ? "ok" : d.online ? "" : "warn"}">${esc(d.active ? t("pad_active") : d.online ? (d.paired ? t("pad_online") : t("pad_waiting_code")) : t("pad_offline"))}</span>
      ${d.paired ? `<button class="btn ghost small-btn" data-forget="${esc(d.id)}">${esc(t("pad_forget"))}</button>` : ""}</div>`).join("")
      : `<div class="muted">${esc(t(st.running ? "pad_no_dev" : "pad_off_hint"))}</div>`;
    $("padDevs").querySelectorAll("[data-forget]").forEach((b) => { b.onclick = async () => {
      if (!confirm(t("pad_forget_q"))) return;
      await api("/api/pad_forget", { json: { id: b.dataset.forget } }).catch(() => {}); padRefresh();
    }; });
    const a = st.active;
    const live = $("padLive");
    if (a) {
      const bat = a.battery == null ? "" : (a.battery & 0x80 ? "⚡" : "") + "🔋 " + Math.round(Math.min(15, a.battery & 0x7f) / 15 * 100) + "%";
      live.innerHTML = `<span class="chip ok">${esc(a.name)}</span><span class="chip">${esc(t("pad_ms", { ms: a.rtt == null ? "…" : a.rtt }))}</span>` +
        `<span class="chip">${a.rate}/s</span><span class="chip">📶 ${a.signal ?? "-"}/3</span>${bat ? `<span class="chip">${bat}</span>` : ""}`;
    } else live.innerHTML = "";
    // live highlight
    document.querySelectorAll("#padKeys .kname").forEach((el) => el.classList.toggle("on", !!(a && (a.keys.includes(el.dataset.k) || (el.dataset.k === "MIC" && a.mic >= 70)))));
    document.querySelectorAll("#padScreen .pw-w").forEach((el) => el.classList.toggle("live", !!(a && a.page === padPage && a.pressed.includes(+el.dataset.id))));
    let dot = $("padScreen").querySelector(".pad-touch");
    if (a && a.touch && a.page === padPage) {
      if (!dot) { dot = document.createElement("div"); dot.className = "pad-touch"; $("padScreen").appendChild(dot); }
      dot.style.left = a.touch[0] * 2 + "px"; dot.style.top = a.touch[1] * 2 + "px";
    } else if (dot) dot.remove();
  }

  function renderPadConfig() {
    if (!padCfg) return;
    $("padSpeed").value = padCfg.mouse_speed;
    $("padPresets").innerHTML = (padSt.presets || []).map((p) => `<button class="btn ghost small-btn${padCfg.preset === p ? " on-sel" : ""}" data-preset="${p}">${esc(t("pad_p_" + p))}</button>`).join("");
    $("padPresets").querySelectorAll("[data-preset]").forEach((b) => { b.onclick = async () => {
      if (padDirty && !confirm(t("pad_preset_q"))) return;
      if (b.dataset.preset === "gamepad" && padSt.platform !== "win32") toast(t("pad_hint_mac_no_gamepad"), true);
      try { padCfgSaved = await api("/api/pad_config", { json: { preset: b.dataset.preset } }); padCfg = JSON.parse(JSON.stringify(padCfgSaved));
        padDirty = false; $("padDirty").textContent = ""; padPage = 0; padSel = -1; renderPadConfig(); toast(t("pad_p_" + b.dataset.preset)); }
      catch (e) { toast(t("error") + ": " + e.message, true); }
    }; });
    const keys = $("padKeys"); keys.innerHTML = "";
    for (const k of PAD_KEYS) {
      const row = document.createElement("div"); row.className = "pad-key";
      row.innerHTML = `<span class="kname" data-k="${k}">${k === "MIC" ? esc(t("pad_mic")) : k}</span>`;
      row.appendChild(padSelect("button", padCfg.buttons[k] || "none", (v) => { padCfg.buttons[k] = v; padMarkDirty(); renderPadConfig(); }));
      keys.appendChild(row);
    }
    renderPadEditor();
  }

  function renderPadEditor() {
    const pages = padCfg.layout.pages;
    $("padPages").innerHTML = pages.map((p, i) => `<button class="${i === padPage ? "on" : ""}" data-pg="${i}">${esc(p.name || i + 1)}</button>`).join("");
    $("padPages").querySelectorAll("[data-pg]").forEach((b) => { b.onclick = () => { padPage = +b.dataset.pg; padSel = -1; renderPadEditor(); }; });
    $("btnPadPageAdd").disabled = pages.length >= 4;
    $("btnPadPageDel").disabled = pages.length <= 1;
    const scr = $("padScreen");
    scr.querySelectorAll(".pw-w").forEach((e) => e.remove());
    const ws = pages[padPage].widgets;
    ws.forEach((w, i) => {
      const el = document.createElement("div");
      el.className = `pw-w ${w.type}${w.shape ? " round" : ""}${i === padSel ? " sel" : ""}`;
      el.dataset.id = w.id;
      Object.assign(el.style, { left: w.x * 2 + "px", top: w.y * 2 + "px", width: w.w * 2 + "px", height: w.h * 2 + "px", background: PAD_COLORS[w.color] });
      el.style.setProperty("--wc", PAD_COLORS[w.color]);
      el.innerHTML = `<span>${esc(w.label || (w.type === "trackpad" ? "🖱" : ""))}</span>` + (i === padSel ? '<div class="rs"></div>' : "");
      el.addEventListener("pointerdown", (ev) => padDrag(ev, i, ev.target.classList.contains("rs")));
      scr.appendChild(el);
    });
    renderPadProps();
  }

  function padDrag(ev, i, resize) {
    ev.preventDefault();
    if (padSel !== i) { padSel = i; renderPadEditor(); }
    const w = padCfg.layout.pages[padPage].widgets[i];
    const sx = ev.clientX, sy = ev.clientY, o = { x: w.x, y: w.y, w: w.w, h: w.h };
    const move = (e) => {
      const dx = Math.round((e.clientX - sx) / 4) * 2, dy = Math.round((e.clientY - sy) / 4) * 2;
      if (resize) { w.w = Math.max(16, Math.min(256 - w.x, o.w + dx)); w.h = Math.max(16, Math.min(172 - w.y, o.h + dy)); if (w.type === "stick") w.h = w.w = Math.min(w.w, 172 - w.y, 256 - w.x); }
      else { w.x = Math.max(0, Math.min(256 - w.w, o.x + dx)); w.y = Math.max(0, Math.min(172 - w.h, o.y + dy)); }
      const el = $("padScreen").querySelector(`.pw-w[data-id="${w.id}"]`);
      Object.assign(el.style, { left: w.x * 2 + "px", top: w.y * 2 + "px", width: w.w * 2 + "px", height: w.h * 2 + "px" });
    };
    const up = () => { window.removeEventListener("pointermove", move); window.removeEventListener("pointerup", up);
      if (w.x !== o.x || w.y !== o.y || w.w !== o.w || w.h !== o.h) { padMarkDirty(); renderPadProps(); } };
    window.addEventListener("pointermove", move); window.addEventListener("pointerup", up);
  }

  function renderPadProps() {
    const box = $("padProps");
    const ws = padCfg.layout.pages[padPage].widgets;
    const w = ws[padSel];
    if (!w) { box.innerHTML = `<p class="muted">${esc(t("pad_select_hint"))}</p>`; return; }
    box.innerHTML = `<h2>${esc(t("pad_w_" + w.type))}</h2>
      <label><span>${esc(t("pad_label"))}</span><input type="text" id="pwLabel" maxlength="12" value="${esc(w.label)}"></label>
      <div class="label-like">${esc(t("pad_color"))}</div><div class="swatches" id="pwColors"></div>
      ${w.type === "button" ? `<label class="switch-row"><input type="checkbox" id="pwRound" ${w.shape ? "checked" : ""}> <span>${esc(t("pad_round"))}</span></label>` : ""}
      <label><span>${esc(t("pad_action"))}</span><div id="pwAction"></div></label>
      <div class="muted small">${w.x}, ${w.y} · ${w.w}×${w.h}</div>
      <div class="row gap mt"><button class="btn ghost small-btn" id="pwDup">${esc(t("pad_dup"))}</button>
      <button class="btn ghost small-btn danger-t" id="pwDel">${esc(t("p_delete"))}</button></div>`;
    $("pwLabel").oninput = (e) => { w.label = e.target.value; padMarkDirty(); const el = $("padScreen").querySelector(`.pw-w[data-id="${w.id}"] span`); if (el) el.textContent = w.label; };
    $("pwColors").innerHTML = PAD_COLORS.map((c, i) => `<button class="sw${i === w.color ? " on" : ""}" style="background:${c}" data-c="${i}"></button>`).join("");
    $("pwColors").querySelectorAll("[data-c]").forEach((b) => { b.onclick = () => { w.color = +b.dataset.c; padMarkDirty(); renderPadEditor(); }; });
    if ($("pwRound")) $("pwRound").onchange = (e) => { w.shape = e.target.checked ? 1 : 0; padMarkDirty(); renderPadEditor(); };
    $("pwAction").appendChild(padSelect(w.type, w.action, (v) => { w.action = v; padMarkDirty(); }));
    $("pwDel").onclick = () => { ws.splice(padSel, 1); padSel = -1; padRenumber(); padMarkDirty(); renderPadEditor(); };
    $("pwDup").onclick = () => {
      if (padCount() >= 32 || ws.length >= 16) { toast(t("pad_full"), true); return; }
      const c = JSON.parse(JSON.stringify(w)); c.x = Math.min(256 - c.w, c.x + 10); c.y = Math.min(172 - c.h, c.y + 10);
      ws.push(c); padRenumber(); padSel = ws.length - 1; padMarkDirty(); renderPadEditor();
    };
  }
  function padCount() { return padCfg.layout.pages.reduce((n, p) => n + p.widgets.length, 0); }
  function padRenumber() { let id = 0; padCfg.layout.pages.forEach((p) => p.widgets.forEach((w) => { w.id = id++; })); }

  document.querySelectorAll("[data-add]").forEach((b) => { b.addEventListener("click", () => {
    const ws = padCfg.layout.pages[padPage].widgets;
    if (padCount() >= 32 || ws.length >= 16) { toast(t("pad_full"), true); return; }
    const type = b.dataset.add;
    const size = { button: [56, 40], stick: [96, 96], trackpad: [140, 100], slider: [120, 28] }[type];
    const act = { button: "key:space", stick: "pad:ls", trackpad: "mouse", slider: "pad:lt" }[type];
    ws.push({ id: 0, type, x: 128 - size[0] / 2, y: 86 - size[1] / 2, w: size[0], h: size[1], color: type === "button" ? 0 : 9, shape: 0,
      label: type === "button" ? "?" : "", action: act });
    padRenumber(); padSel = ws.length - 1; padMarkDirty(); renderPadEditor();
  }); });
  $("btnPadPageAdd").addEventListener("click", () => {
    const n = prompt(t("pad_page_name"), t("pad_page") + " " + (padCfg.layout.pages.length + 1)); if (!n) return;
    padCfg.layout.pages.push({ name: n.slice(0, 12), widgets: [] }); padPage = padCfg.layout.pages.length - 1; padSel = -1; padMarkDirty(); renderPadEditor();
  });
  $("btnPadPageRen").addEventListener("click", () => {
    const p = padCfg.layout.pages[padPage]; const n = prompt(t("pad_page_name"), p.name); if (!n) return;
    p.name = n.slice(0, 12); padMarkDirty(); renderPadEditor();
  });
  $("btnPadPageDel").addEventListener("click", () => {
    if (!confirm(t("pad_page_del_q"))) return;
    padCfg.layout.pages.splice(padPage, 1); padPage = 0; padSel = -1; padRenumber(); padMarkDirty(); renderPadEditor();
  });
  $("padSpeed").addEventListener("change", (e) => { padCfg.mouse_speed = +e.target.value; padMarkDirty(); });
  $("btnPadSave").addEventListener("click", async () => {
    try {
      padCfgSaved = await api("/api/pad_config", { json: { buttons: padCfg.buttons, layout: padCfg.layout, mouse_speed: padCfg.mouse_speed } });
      padCfg = JSON.parse(JSON.stringify(padCfgSaved)); padDirty = false; $("padDirty").textContent = "";
      renderPadConfig(); toast(t(padSt && padSt.active ? "pad_sent" : "saved"));
    } catch (e) { toast(t("error") + ": " + e.message, true); }
  });
  $("btnPadRevert").addEventListener("click", () => {
    padCfg = JSON.parse(JSON.stringify(padCfgSaved)); padDirty = false; $("padDirty").textContent = ""; padSel = -1; renderPadConfig();
  });
  $("btnPadRun").addEventListener("click", async () => {
    try { padSt = await api(padSt && padSt.running ? "/api/pad_stop" : "/api/pad_start", { json: {} }); renderPadStatus(); }
    catch (e) { toast(t("error") + ": " + e.message, true); }
  });
  $("btnPadPair").addEventListener("click", async () => {
    const code = $("padCode").value.trim();
    if (!/^\d{4}$/.test(code)) { toast(t("pad_code_bad"), true); return; }
    try {
      const r = await api("/api/pad_pair", { json: { code: +code, ip: $("padIp").value.trim() } });
      if (!r.sent) { toast(t("pad_no_dev"), true); return; }
      const t0 = Date.now();
      while (Date.now() - t0 < 2500) { await new Promise((res) => setTimeout(res, 250)); await padRefresh(); if (padSt.active) break; }
      if (padSt.active) { toast(t("pad_paired", { name: padSt.active.name })); $("padCode").value = ""; }
      else toast(t("pad_pair_fail"), true);
    } catch (e) { toast(t("error") + ": " + e.message, true); }
  });
  $("btnPadWifi").addEventListener("click", async () => {
    try {
      const r = await api("/api/pad_wifi", { json: { sd: current, ssid: $("padSsid").value, password: $("padPass").value } });
      $("padPass").value = ""; toast(r.removed ? t("pad_wifi_removed") : t("pad_wifi_done", { ssid: r.ssid }));
    } catch (e) { toast(t("error") + ": " + e.message, true); }
  });
  function padTick() {
    const on = $("page-pad").classList.contains("active") && !document.hidden;
    $("btnPadWifi").disabled = !current;
    if (on) padRefresh();
    padTimer = setTimeout(padTick, on && padSt && padSt.active ? 250 : 1000);
  }
  padTick();

  // ------------------------------------------------------------------ start
  const sys = (navigator.language || "tr").slice(0, 2);
  lang = load("studio.lang") || (window.STRINGS[sys] ? sys : "en");
  $("uiLang").value = lang; if (["tr", "en", "es", "de", "fr", "az"].includes(lang)) $("consoleLang").value = lang;
  $("uiLang").addEventListener("change", (e) => { lang = e.target.value; store("studio.lang", lang); applyLang(); });
  const page = load("studio.page");
  if (page) { const b = document.querySelector(`.nav button[data-page="${page}"]`); if (b) b.click(); }
  applyLang();
  loadDrives();
})();

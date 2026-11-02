#include "launcher.h"
#include "cheats.h"
#include "stats.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ARM9
#include <nds.h>
#include <dvm.h>
#include <fat.h>
#include <calico/dev/blk.h>
#include <calico/dev/disc_io.h>
#include <calico/nds/pm.h>
#include <calico/nds/tlnc.h>
#include "nds_loader_arm9.h"
#endif

static char s_drv_override[128];
static bool s_drv_overridden = false;
static char s_nand_root[MAX_PATH_LEN];      // tests / simulator: a folder that looks like the NAND

void launcher_set_nand_root(const char *dir) { snprintf(s_nand_root, sizeof(s_nand_root), "%s", dir ? dir : ""); }

bool launcher_title_app(const char *title_dir, char *out, size_t len) {
    char tmd[MAX_PATH_LEN];
    snprintf(tmd, sizeof(tmd), "%s/content/title.tmd", title_dir);
    FILE *f = fopen(tmd, "rb");
    if (!f) return false;
    uint8_t id[4];
    bool ok = fseek(f, 0x1E4, SEEK_SET) == 0 && fread(id, 1, 4, f) == 4;   // content ID of the app (big endian)
    fclose(f);
    if (!ok) return false;
    unsigned long cid = (unsigned long)id[0] << 24 | (unsigned long)id[1] << 16 | (unsigned long)id[2] << 8 | id[3];
    snprintf(out, len, "%s/content/%08lx.app", title_dir, cid);
    FILE *a = fopen(out, "rb");
    if (!a) return false;
    fclose(a);
    return true;
}

void launcher_set_drive(const char *drv) {
    snprintf(s_drv_override, sizeof(s_drv_override), "%s", drv ? drv : "");
    s_drv_overridden = true;
}

const char *get_drive_prefix(void) {
    if (s_drv_overridden) return s_drv_override;
#ifdef ARM9
    static const char *cached = NULL;
    if (!cached) {
        struct stat st;
        if (stat("sd:/", &st) == 0) cached = "sd:";
        else if (stat("fat:/", &st) == 0) cached = "fat:";
        else cached = "sd:";
    }
    return cached;
#else
    return "";
#endif
}

static bool on_sd(void) {
    return strcmp(get_drive_prefix(), "sd:") == 0;
}

// "sd:/roms/x" -> "fat:/roms/x" (what DLDI-based homebrew sees through nds-bootstrap-hb)
static void to_fat(const char *path, char *out, size_t len) {
    if (strncmp(path, "sd:/", 4) == 0) snprintf(out, len, "fat:/%s", path + 4);
    else snprintf(out, len, "%s", path);
}

static bool exists_on_drive(char *out, size_t len, const char *rel) {
    snprintf(out, len, "%s%s", get_drive_prefix(), rel);
    return file_exists(out);
}

// First existing candidate (relative to the drive); out = first candidate when none exists
static bool pick(char *out, size_t len, const char *const *cands, int n) {
    for (int i = 0; i < n; i++) {
        if (exists_on_drive(out, len, cands[i])) return true;
    }
    snprintf(out, len, "%s%s", get_drive_prefix(), cands[0]);
    return false;
}

void save_path_for(const char *rom_path, int slot, char *out, size_t len) {
    snprintf(out, len, "%s", rom_path);
    const char *ext = (slot == 1) ? ".sav1" : (slot == 2) ? ".sav2" : ".sav";
    char *dot = strrchr(out, '.');
    char *slash = strrchr(out, '/');
    if (dot && (!slash || dot > slash) && (size_t)(dot - out) + strlen(ext) < len) strcpy(dot, ext);
    else if (strlen(out) + strlen(ext) < len) strcat(out, ext);
}

// Profile folder for saves/backups ("" = first profile: saves next to the game, backups in _hyper/)
static char s_profile_dir[MAX_PATH_LEN];

void launcher_set_profile_dir(const char *dir) {
    snprintf(s_profile_dir, sizeof(s_profile_dir), "%s", dir ? dir : "");
}

static bool profile_saves(const GameItem *item) {
    return s_profile_dir[0] && item && item->type == TYPE_NDS && !(item->flags & ITEM_FLAG_HOMEBREW);
}

// Nintendo DS saves are kept per profile; every other system keeps one save next to the game
void game_save_path(const GameItem *item, int slot, char *out, size_t len) {
    if (!profile_saves(item)) { save_path_for(item->path, slot, out, len); return; }
    const char *base = strrchr(item->path, '/');
    base = base ? base + 1 : item->path;
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s/saves/%s", s_profile_dir, base);
    save_path_for(tmp, slot, out, len);
}

static void backup_path_for(const GameItem *item, int slot, char *out, size_t len) {
    char dir[MAX_PATH_LEN];
    if (s_profile_dir[0]) snprintf(dir, sizeof(dir), "%s/backups", s_profile_dir);
    else snprintf(dir, sizeof(dir), "%s/_hyper/backups", get_drive_prefix());
    make_dirs(dir);
    snprintf(out, len, "%s/%s.slot%d.bak", dir, item->filename, slot + 1);
}

static bool same_file(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb");
    if (!fa) return false;
    FILE *fb = fopen(b, "rb");
    if (!fb) { fclose(fa); return false; }
    static uint8_t ba[2048], bb[2048];
    bool same = true;
    for (;;) {
        size_t na = fread(ba, 1, sizeof(ba), fa), nb = fread(bb, 1, sizeof(bb), fb);
        if (na != nb || memcmp(ba, bb, na) != 0) { same = false; break; }
        if (na == 0) break;
    }
    fclose(fa);
    fclose(fb);
    return same;
}

// <name>.slot1.bak is the newest copy, .bak2 and .bak3 the two before it: a save that went
// bad during a game is never the only copy left
#define SAVE_GENERATIONS 3

static void generation_path(const char *bak, int gen, char *out, size_t len) {
    if (gen <= 1) snprintf(out, len, "%s", bak);
    else snprintf(out, len, "%s%d", bak, gen);
}

bool backup_game_save(const GameItem *item, int slot) {
    if (!item || item->type == TYPE_SLOT1) return false;
    char sav[MAX_PATH_LEN], bak[MAX_PATH_LEN];
    game_save_path(item, slot, sav, sizeof(sav));
    if (!file_exists(sav)) return false;
    backup_path_for(item, slot, bak, sizeof(bak));
    if (file_exists(bak) && same_file(sav, bak)) return true;      // nothing new
    char from[MAX_PATH_LEN + 4], to[MAX_PATH_LEN + 4];
    for (int g = SAVE_GENERATIONS - 1; g >= 1; g--) {
        generation_path(bak, g, from, sizeof(from));
        generation_path(bak, g + 1, to, sizeof(to));
        if (!file_exists(from)) continue;
        remove(to);
        rename(from, to);
    }
    return copy_file(sav, bak);
}

bool restore_game_save_backup(const GameItem *item, int slot) {
    if (!item || item->type == TYPE_SLOT1) return false;
    char sav[MAX_PATH_LEN], bak[MAX_PATH_LEN], keep[MAX_PATH_LEN + 16];
    game_save_path(item, slot, sav, sizeof(sav));
    backup_path_for(item, slot, bak, sizeof(bak));
    if (!file_exists(bak)) return false;
    // the save being replaced is kept too (<name>.slotN.bak.before_restore)
    if (file_exists(sav) && !same_file(sav, bak)) {
        snprintf(keep, sizeof(keep), "%s.before_restore", bak);
        if (!copy_file(sav, keep)) return false;
    }
    return copy_file(bak, sav);
}

// The game is being deleted: its saves (every slot, this profile and next to the game) and
// notes go to _hyper/deleted_saves/ instead of being erased. Returns how many were kept.
int launcher_keep_saves(const GameItem *item) {
    if (!item) return 0;
    char dir[MAX_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/_hyper/deleted_saves", get_drive_prefix());
    make_dirs(dir);
    char cand[12][MAX_PATH_LEN];
    int nc = 0;
    for (int slot = 0; slot < 3; slot++) {
        game_save_path(item, slot, cand[nc++], MAX_PATH_LEN);
        save_path_for(item->path, slot, cand[nc++], MAX_PATH_LEN);
    }
    static const char *const extra[] = { ".srm", ".dsv", ".txt", ".pub", ".prv" };   // .pub/.prv: DSiWare
    for (int i = 0; i < 5; i++) {
        snprintf(cand[nc], MAX_PATH_LEN, "%s", item->path);
        char *dot = strrchr(cand[nc], '.'), *slash = strrchr(cand[nc], '/');
        if (dot && (!slash || dot > slash) && (size_t)(dot - cand[nc]) + 5 < MAX_PATH_LEN) { strcpy(dot, extra[i]); nc++; }
    }
    int kept = 0;
    for (int i = 0; i < nc; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) if (!strcmp(cand[i], cand[j])) dup = true;
        if (dup || !file_exists(cand[i])) continue;
        const char *base = strrchr(cand[i], '/');
        base = base ? base + 1 : cand[i];
        char dst[MAX_PATH_LEN + 16];
        snprintf(dst, sizeof(dst), "%s/%s", dir, base);
        for (int n = 2; file_exists(dst) && n < 100; n++) snprintf(dst, sizeof(dst), "%s/%d_%s", dir, n, base);
        if (rename(cand[i], dst) == 0 || copy_file(cand[i], dst)) kept++;
    }
    return kept;
}

// ---------------------------------------------------------------------------
// Per-game settings
// ---------------------------------------------------------------------------
static void map_path(const char *in, char *out, size_t len);
static void game_cfg_path(char *out, size_t len) {
    char p[MAX_PATH_LEN];
    snprintf(p, sizeof(p), "%s/_hyper/game_settings.txt", get_drive_prefix());
    map_path(p, out, len);       // host tests: under HL_SDROOT
}

void game_cfg_get(const char *filename, GameCfg *out) {
    memset(out, 0, sizeof(*out));
    if (!filename || !filename[0]) return;
    char p[MAX_PATH_LEN];
    game_cfg_path(p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[MAX_PATH_LEN + 32];
    size_t fl = strlen(filename);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, filename, fl) != 0 || line[fl] != '|') continue;
        int c = 0, m = 0, v = 0, w = 0;
        if (sscanf(line + fl + 1, "%d|%d|%d|%d", &c, &m, &v, &w) >= 1) {
            out->cpu = (uint8_t)(c >= 0 && c <= 2 ? c : 0);
            out->mode = (uint8_t)(m >= 0 && m <= 2 ? m : 0);
            out->vram = (uint8_t)(v == 1);
            out->dsw = (uint8_t)(w == 1);
        }
        break;
    }
    fclose(f);
}

bool game_cfg_set(const char *filename, const GameCfg *cfg) {
    if (!filename || !filename[0] || strchr(filename, '|')) return false;
    char p[MAX_PATH_LEN], tmp[MAX_PATH_LEN + 8];
    game_cfg_path(p, sizeof(p));
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FILE *in = fopen(p, "r"), *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return false; }
    size_t fl = strlen(filename);
    char line[MAX_PATH_LEN + 32];
    if (in) {
        while (fgets(line, sizeof(line), in)) {
            if (!strncmp(line, filename, fl) && line[fl] == '|') continue;
            fputs(line, out);
        }
        fclose(in);
    }
    if (cfg->dsw) fprintf(out, "%s|%d|%d|%d|%d\n", filename, cfg->cpu, cfg->mode, cfg->vram, cfg->dsw);
    else if (cfg->cpu || cfg->mode || cfg->vram) fprintf(out, "%s|%d|%d|%d\n", filename, cfg->cpu, cfg->mode, cfg->vram);
    bool ok = fclose(out) == 0;
    if (ok) { remove(p); ok = rename(tmp, p) == 0; }
    return ok;
}

bool gba_bios_present(void) {
    char p[MAX_PATH_LEN];
    return exists_on_drive(p, sizeof(p), "/_gba/bios.bin") || exists_on_drive(p, sizeof(p), "/gba/bios.bin") ||
           exists_on_drive(p, sizeof(p), "/bios.bin");
}

static void add_arg(LaunchPlan *p, const char *a) {
    if (p->argc >= 1 && p->argc <= 3) {
        snprintf(p->args[p->argc - 1], MAX_PATH_LEN, "%s", a);
        p->argc++;
    }
}

static const char *const k_bootstrap_hb[] = { "/_nds/nds-bootstrap-hb-release.nds", "/_nds/nds-bootstrap-hb-nightly.nds" };
static const char *const k_bootstrap[] = { "/_nds/nds-bootstrap-release.nds", "/_nds/nds-bootstrap-nightly.nds" };

// Route an emulator/homebrew through nds-bootstrap-hb
static LaunchResult plan_bootstrap_hb(LaunchPlan *p, const char *emu, const char *hb_arg, const char *rom_fat,
                                      int dsi_mode, int boost_cpu, char *detail, size_t dlen) {
    if (!pick(p->boot_path, sizeof(p->boot_path), k_bootstrap_hb, 2)) {
        snprintf(detail, dlen, "%s", p->boot_path);
        return LAUNCH_ERR_NO_BOOTSTRAP;
    }
    p->argc = 1;
    if (rom_fat && rom_fat[0]) add_arg(p, rom_fat);
    p->ini = INI_HOMEBREW;
    snprintf(p->ini_nds_path, sizeof(p->ini_nds_path), "%s", emu);
    snprintf(p->ini_homebrew_arg, sizeof(p->ini_homebrew_arg), "%s", hb_arg ? hb_arg : "");
    p->ini_dsi_mode = dsi_mode;
    p->ini_boost_cpu = boost_cpu;
    p->ini_boost_vram = 0;
    p->boost_cpu = true;
    return LAUNCH_OK;
}

static LaunchResult plan_direct(LaunchPlan *p, const char *const *cands, int n, const char *rom_arg, char *detail, size_t dlen) {
    if (!pick(p->boot_path, sizeof(p->boot_path), cands, n)) {
        snprintf(detail, dlen, "%s", p->boot_path);
        return LAUNCH_ERR_MISSING_EMU;
    }
    p->argc = 1;
    if (rom_arg) add_arg(p, rom_arg);
    p->boost_cpu = true;
    return LAUNCH_OK;
}

// "sd:/..." on the console; host tests map it onto HL_SDROOT
static FILE *open_mapped(const char *path, const char *mode) {
#ifndef ARM9
    const char *root = getenv("HL_SDROOT");
    const char *colon = strchr(path, ':');
    if (root && colon && colon - path <= 4) {
        char mapped[MAX_PATH_LEN * 2];
        snprintf(mapped, sizeof(mapped), "%s%s", root, colon + 1);
        return fopen(mapped, mode);
    }
#endif
    return fopen(path, mode);
}

bool launcher_is_dsiware(const char *path, uint32_t *pub_size, uint32_t *prv_size) {
    uint8_t h[0x240];
    FILE *f = open_mapped(path, "rb");
    if (!f) return false;
    bool ok = fread(h, 1, sizeof(h), f) == sizeof(h);
    fclose(f);
    if (!ok) return false;
    uint32_t tid_hi = (uint32_t)h[0x234] | (uint32_t)h[0x235] << 8 | (uint32_t)h[0x236] << 16 | (uint32_t)h[0x237] << 24;
    uint32_t access = (uint32_t)h[0x1B4] | (uint32_t)h[0x1B5] << 8 | (uint32_t)h[0x1B6] << 16 | (uint32_t)h[0x1B7] << 24;
    bool dsiware = (h[0x12] & 0x02) && (tid_hi == 0x00030004u || tid_hi == 0x00030005u) && (access & (1u << 4));
    if (pub_size) *pub_size = (uint32_t)h[0x238] | (uint32_t)h[0x239] << 8 | (uint32_t)h[0x23A] << 16 | (uint32_t)h[0x23B] << 24;
    if (prv_size) *prv_size = (uint32_t)h[0x23C] | (uint32_t)h[0x23D] << 8 | (uint32_t)h[0x23E] << 16 | (uint32_t)h[0x23F] << 24;
    return dsiware;
}

// Same geometry as TWiLight Menu++'s createDSiWareSave() (and the DSi's own formatter)
bool launcher_make_dsiware_save(const char *path, uint32_t size) {
    const uint32_t ss = 0x200;
    if (!path || size < ss || size > 0x1000000) return false;
    uint32_t max_sectors = size / ss, sector_count = 1, spt = 1, heads = 1, next = 0;
    while (next <= max_sectors) {
        next = spt * (heads + 1) * (heads + 1);
        if (next <= max_sectors) {
            heads++;
            sector_count = next;
            spt++;
            next = spt * heads * heads;
            if (next <= max_sectors) sector_count = next;
        }
    }
    next = (spt + 1) * heads * heads;
    if (next <= max_sectors) { spt++; sector_count = next; }
    uint32_t spc = sector_count > (8u << 10) ? 8 : (sector_count > (1u << 10) ? 4 : 1);
    uint32_t root_entries = size < 0x8C000 ? 0x20 : 0x200;
#define DSW_ALIGN(v, a) (((v) % (a)) ? ((v) + (a) - ((v) % (a))) : (v))
    uint32_t clusters = DSW_ALIGN(sector_count, spc) / spc;
    uint32_t fat_bytes = (DSW_ALIGN(clusters, 2) / 2) * 3;
    uint32_t fat_size = DSW_ALIGN(fat_bytes, ss) / ss;
#undef DSW_ALIGN
    static uint8_t b[0x200];
    memset(b, 0, sizeof(b));
    b[0] = 0xE9;
    memcpy(b + 3, "MSWIN4.1", 8);
    b[11] = ss & 0xFF; b[12] = ss >> 8;
    b[13] = (uint8_t)spc;
    b[14] = 1;                                   // reserved sectors
    b[16] = 2;                                   // FATs
    b[17] = root_entries & 0xFF; b[18] = root_entries >> 8;
    b[19] = sector_count & 0xFF; b[20] = (sector_count >> 8) & 0xFF;
    b[21] = 0xF8;
    b[22] = fat_size & 0xFF; b[23] = fat_size >> 8;
    b[24] = (uint8_t)spt; b[26] = (uint8_t)heads;
    b[36] = 0x05; b[38] = 0x29;
    b[39] = 0x78; b[40] = 0x56; b[41] = 0x34; b[42] = 0x12;
    memcpy(b + 43, "VOLUMELABEL", 11);
    memcpy(b + 54, "FAT12   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
    char tmp[MAX_PATH_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = open_mapped(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(b, 1, sizeof(b), f) == sizeof(b);
    static uint8_t z[0x200];
    memset(z, 0, sizeof(z));
    for (uint32_t done = sizeof(b); ok && done < size; done += sizeof(z)) ok = fwrite(z, 1, sizeof(z), f) == sizeof(z);
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
#ifndef ARM9
    {   // host: rename through the same mapping
        const char *root = getenv("HL_SDROOT");
        const char *c1 = strchr(path, ':');
        if (root && c1 && c1 - path <= 4) {
            char a[MAX_PATH_LEN * 2], bb[MAX_PATH_LEN * 2];
            snprintf(a, sizeof(a), "%s%s", root, strchr(tmp, ':') + 1);
            snprintf(bb, sizeof(bb), "%s%s", root, c1 + 1);
            return rename(a, bb) == 0;
        }
    }
#endif
    return rename(tmp, path) == 0;
}

// ---- DSiWare ---------------------------------------------------------------------------------
#define DSW_DIR "/_hyper/dsw"

// host tests map "sd:/..." onto HL_SDROOT (the console uses the path as it is)
static void map_path(const char *in, char *out, size_t len) {
#ifndef ARM9
    const char *root = getenv("HL_SDROOT");
    const char *colon = strchr(in, ':');
    if (root && colon && colon - in <= 4) { snprintf(out, len, "%s%s", root, colon + 1); return; }
#endif
    snprintf(out, len, "%s", in);
}

// "<path without extension><ext>"
static void with_ext(const char *path, const char *ext, char *out, size_t len) {
    save_path_for(path, 0, out, len);
    char *dot = strrchr(out, '.');
    char *slash = strrchr(out, '/');
    if (dot && (!slash || dot > slash) && (size_t)(dot - out) + strlen(ext) < len) strcpy(dot, ext);
}

// true when every part of the path is a plain 8.3 name, so its short name is the same in capitals
static bool sfn_clean(const char *path) {
    const char *s = strchr(path, ':');
    s = s ? s + 1 : path;
    if (*s == '/') s++;
    if (strlen(path) >= 62) return false;
    while (*s) {
        int base = 0, ext = -1;
        for (; *s && *s != '/'; s++) {
            char c = *s;
            if (c == '.') { if (ext >= 0 || base == 0) return false; ext = 0; continue; }
            bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr("_-!#$%&'()@^`{}~", c);
            if (!ok) return false;
            if (ext >= 0) ext++; else base++;
        }
        if (base < 1 || base > 8 || ext == 0 || ext > 3) return false;
        if (*s == '/') s++;
    }
    return true;
}

static void upper_path(char *s) {
    for (char *c = strchr(s, ':') ? strchr(s, ':') + 1 : s; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
}

static LaunchResult plan_dsiware(const GameItem *item, LaunchPlan *p, char *detail, size_t dlen) {
    GameCfg gc;
    game_cfg_get(item->filename, &gc);
    p->dsiware = true;
    p->dsw_bootstrap = gc.dsw == 1;
    p->argc = 1;
    p->ini = INI_NONE;
    snprintf(p->boot_path, sizeof(p->boot_path), "%s", item->path);
    // where the saves are kept
    char base[MAX_PATH_LEN];
    if (s_profile_dir[0]) snprintf(base, sizeof(base), "%s/saves/%s", s_profile_dir, item->filename);
    else snprintf(base, sizeof(base), "%s", item->path);
    if (p->pub_size) with_ext(base, ".pub", p->home_pub, sizeof(p->home_pub));
    if (p->prv_size) with_ext(base, ".prv", p->home_prv, sizeof(p->home_prv));
    if (!p->dsw_bootstrap) {
        // Unlaunch finds "<game>.pub" / "<game>.prv" next to the .nds
        if (p->pub_size) with_ext(item->path, ".pub", p->pub_path, sizeof(p->pub_path));
        if (p->prv_size) with_ext(item->path, ".prv", p->prv_path, sizeof(p->prv_path));
        return LAUNCH_OK;
    }
    // nds-bootstrap: short 8.3 paths under 62 characters (TWiLight Menu++ passes short names too)
    const char *drv = get_drive_prefix();
    if (p->pub_size) snprintf(p->pub_path, sizeof(p->pub_path), "%s" DSW_DIR "/SAVE.PUB", drv);
    if (p->prv_size) snprintf(p->prv_path, sizeof(p->prv_path), "%s" DSW_DIR "/SAVE.PRV", drv);
    if (sfn_clean(item->path)) {
        snprintf(p->app_path, sizeof(p->app_path), "%s", item->path);
        upper_path(p->app_path);
    } else {
        snprintf(p->app_path, sizeof(p->app_path), "%s" DSW_DIR "/APP.NDS", drv);
        p->app_copy = true;
    }
    if (!pick(p->boot_path, sizeof(p->boot_path), k_bootstrap, 2)) {
        snprintf(detail, dlen, "%s", p->boot_path);
        return LAUNCH_ERR_NO_BOOTSTRAP;
    }
    p->ini = INI_DSIWARE;
    snprintf(p->ini_nds_path, sizeof(p->ini_nds_path), "%s", item->path);
    snprintf(p->ini_sav_path, sizeof(p->ini_sav_path), "%s", p->pub_path);
    upper_path(p->ini_sav_path);
    p->ini_dsi_mode = 1;
    p->ini_boost_cpu = 1;
    p->ini_boost_vram = 1;
    p->boost_cpu = true;
    return LAUNCH_OK;
}

LaunchResult launcher_plan(const GameItem *item, const SystemSettings *cfg, LaunchPlan *p, char *detail, size_t dlen) {
    char dummy[8];
    if (!detail || !dlen) { detail = dummy; dlen = sizeof(dummy); }
    detail[0] = '\0';
    memset(p, 0, sizeof(*p));
    if (!item) return LAUNCH_ERR_UNSUPPORTED;
    if (item->type == TYPE_APP) {                // DSi application: Unlaunch boots it from the NAND
        char root[16];
        if (!launcher_nand_root(root, sizeof(root)) || !launcher_title_app(item->path, p->boot_path, sizeof(p->boot_path))) {
            snprintf(detail, dlen, "%s", item->path);
            return LAUNCH_ERR_NO_FILE;
        }
        p->argc = 1;
        p->ini = INI_NONE;
        return LAUNCH_OK;
    }
    if (item->type != TYPE_SLOT1 && !file_exists(item->path)) {
        snprintf(detail, dlen, "%s", item->path);
        return LAUNCH_ERR_NO_FILE;
    }
    int slot = cfg ? cfg->save_slot : 0;
    if (slot < 0 || slot > 2) slot = 0;

    char rom_fat[MAX_PATH_LEN];
    to_fat(item->path, rom_fat, sizeof(rom_fat));
    const bool sd = on_sd();

    switch ((ItemType)item->type) {
        case TYPE_NDS: {
            if (item->flags & ITEM_FLAG_HOMEBREW) {
                if (!sd || (item->flags & ITEM_FLAG_DSI)) {
                    // DSi-ready homebrew (or running from a flashcard): boot directly
                    snprintf(p->boot_path, sizeof(p->boot_path), "%s", item->path);
                    p->argc = 1;
                    p->boost_cpu = true;
                    return LAUNCH_OK;
                }
                // DS-only homebrew on the DSi SD card needs nds-bootstrap-hb for storage access
                return plan_bootstrap_hb(p, item->path, "", "", 0, 0, detail, dlen);
            }
            if (sd && launcher_is_dsiware(item->path, &p->pub_size, &p->prv_size))
                return plan_dsiware(item, p, detail, dlen);
            if (!pick(p->boot_path, sizeof(p->boot_path), k_bootstrap, 2)) {
                snprintf(detail, dlen, "%s", p->boot_path);
                return LAUNCH_ERR_NO_BOOTSTRAP;
            }
            p->argc = 1;
            p->ini = INI_RETAIL;
            snprintf(p->ini_nds_path, sizeof(p->ini_nds_path), "%s", item->path);
            game_save_path(item, slot, p->ini_sav_path, sizeof(p->ini_sav_path));
            if (profile_saves(item)) snprintf(p->mkdir_path, sizeof(p->mkdir_path), "%s/saves", s_profile_dir);
            snprintf(p->save_path, sizeof(p->save_path), "%s", p->ini_sav_path);
            p->ini_dsi_mode = 1;
            p->ini_boost_cpu = (item->flags & ITEM_FLAG_DSI) ? 1 : 0;
            p->ini_boost_vram = 0;
            p->boost_cpu = true;
            {   // what the player chose for this game (Options > Game settings)
                GameCfg gc;
                game_cfg_get(item->filename, &gc);
                if (gc.cpu) p->ini_boost_cpu = gc.cpu == 2;
                if (gc.mode) p->ini_dsi_mode = gc.mode == 2;
                if (gc.vram) p->ini_boost_vram = 1;
            }
            return LAUNCH_OK;
        }

        case TYPE_GBA: {
            if (!gba_bios_present()) return LAUNCH_ERR_GBA_BIOS;
            if (sd) {
                static const char *const dsi[] = { "/_nds/GBARunner2_arm7dldi_dsi.nds", "/_nds/GBARunner2_arm7dldi_nodsp_dsi.nds",
                                                   "/_nds/GBARunner2.nds" };
                char emu[MAX_PATH_LEN];
                if (!pick(emu, sizeof(emu), dsi, 3)) {
                    snprintf(detail, dlen, "%s", emu);
                    return LAUNCH_ERR_MISSING_EMU;
                }
                // TWiLight: HOMEBREW_ARG keeps the sd:/ path, argv[1] uses fat:/
                return plan_bootstrap_hb(p, emu, item->path, rom_fat, 0, 1, detail, dlen);
            }
            static const char *const ds[] = { "/_nds/GBARunner2_arm7dldi_ds.nds", "/_nds/GBARunner2_arm9dldi_ds.nds",
                                              "/_nds/GBARunner2.nds" };
            return plan_direct(p, ds, 3, item->path, detail, dlen);
        }

        case TYPE_GB:
        case TYPE_GBC: {
            static const char *const c[] = { "/_nds/TWiLightMenu/emulators/gameyob.nds", "/_nds/gameyob.nds" };
            return plan_direct(p, c, 2, item->path, detail, dlen);
        }

        case TYPE_NES: {
            static const char *const c[] = { "/_nds/TWiLightMenu/emulators/nesds.nds", "/_nds/TWiLightMenu/emulators/nesDS.nds",
                                             "/_nds/nesds.nds" };
            return plan_direct(p, c, 3, item->path, detail, dlen);
        }

        case TYPE_SMS:
        case TYPE_GG: {
            static const char *const c[] = { "/_nds/TWiLightMenu/emulators/S8DS.nds", "/_nds/s8ds.nds" };
            snprintf(p->mkdir_path, sizeof(p->mkdir_path), "%s/data/s8ds", get_drive_prefix());
            return plan_direct(p, c, 2, item->path, detail, dlen);
        }

        case TYPE_SNES: {
            if (!sd) {
                static const char *const c[] = { "/_nds/TWiLightMenu/emulators/SNEmulDS-legacy.nds", "/_nds/snemulds.nds" };
                return plan_direct(p, c, 2, item->path, detail, dlen);
            }
            // TWiLight picks the "-twlm-path" build only for ROMs in exactly sd:/roms/snes
            char folder[MAX_PATH_LEN];
            snprintf(folder, sizeof(folder), "%s", item->path);
            char *slash = strrchr(folder, '/');
            if (slash) *slash = '\0';
            const char *variant = (strcmp(folder, "sd:/roms/snes") == 0)
                ? "/_nds/TWiLightMenu/emulators/SNEmulDS-legacy-twlm-path.nds"
                : "/_nds/TWiLightMenu/emulators/SNEmulDS-legacy.nds";
            char emu[MAX_PATH_LEN];
            if (!exists_on_drive(emu, sizeof(emu), variant)) {
                snprintf(detail, dlen, "%s", emu);
                return LAUNCH_ERR_MISSING_EMU;
            }
            return plan_bootstrap_hb(p, emu, rom_fat, rom_fat, 0, 0, detail, dlen);
        }

        case TYPE_GENESIS: {
            bool big = item->file_size > (sd ? 0x780000u : 0x300000u);
            static const char *const pico[] = { "/_nds/TWiLightMenu/emulators/PicoDriveTWL.nds", "/_nds/picodrive.nds" };
            if (big || !sd) {
                char tmp[MAX_PATH_LEN];
                if (big && pick(tmp, sizeof(tmp), pico, 2)) return plan_direct(p, pico, 2, item->path, detail, dlen);
                static const char *const jen[] = { "/_nds/TWiLightMenu/emulators/jEnesisDS.nds" };
                if (!sd) return plan_direct(p, jen, 1, item->path, detail, dlen);
            }
            char emu[MAX_PATH_LEN];
            if (!exists_on_drive(emu, sizeof(emu), "/_nds/TWiLightMenu/emulators/jEnesisDS.nds")) {
                // fall back to PicoDrive TWL when jEnesisDS is not installed
                char tmp[MAX_PATH_LEN];
                if (pick(tmp, sizeof(tmp), pico, 2)) return plan_direct(p, pico, 2, item->path, detail, dlen);
                snprintf(detail, dlen, "%s", emu);
                return LAUNCH_ERR_MISSING_EMU;
            }
            return plan_bootstrap_hb(p, emu, rom_fat, rom_fat, 1, 1, detail, dlen);
        }

        default:
            return LAUNCH_ERR_UNSUPPORTED;
    }
}

int launcher_format_ini(const LaunchPlan *p, const SystemSettings *cfg, char *buf, size_t len) {
    static const char *const gui_lang[LANG_COUNT] = { "en", "tr", "es", "de", "fr", "tr" };   // nds-bootstrap has no Azerbaijani
    const char *lang = gui_lang[(cfg && cfg->language < LANG_COUNT) ? cfg->language : 0];
    bool hb = (p->ini == INI_HOMEBREW);
    int n;
    // "Quit" in the nds-bootstrap in-game menu (L+Down+Select) returns to DSplay
    char quit[MAX_PATH_LEN];
    if (!exists_on_drive(quit, sizeof(quit), "/BOOT.NDS") && !exists_on_drive(quit, sizeof(quit), "/HyperLauncher.nds"))
        snprintf(quit, sizeof(quit), "%s/BOOT.NDS", get_drive_prefix());
    n = snprintf(buf, len,
        "[NDS-BOOTSTRAP]\n"
        "NDS_PATH = %s\n"
        "SAV_PATH = %s\n"
        "AP_FIX_PATH = \n"
        "HOMEBREW_ARG = %s\n"
        "RAM_DRIVE_PATH = \n"
        "QUIT_PATH = %s\n"
        "GUI_LANGUAGE = %s\n"
        "LANGUAGE = -1\n"
        "REGION = -2\n"
        "DSI_MODE = %d\n"
        "BOOST_CPU = %d\n"
        "BOOST_VRAM = %d\n"
        "CARD_READ_DMA = 1\n"
        "ASYNC_CARD_READ = 1\n"
        "CONSOLE_MODEL = 0\n"
        "SLEEP_MODE = 1\n"
        "FORCE_SLEEP_PATCH = 0\n"
        "PRECISE_VOLUME_CONTROL = 0\n"
        "MACRO_MODE = 0\n"
        "LOGGING = 0\n",
        p->ini_nds_path,
        hb ? "" : p->ini_sav_path,
        p->ini_homebrew_arg,
        quit,
        lang,
        p->ini_dsi_mode, p->ini_boost_cpu, p->ini_boost_vram);
    if (p->ini == INI_DSIWARE && n > 0 && (size_t)n < len) {
        // the keys TWiLight Menu++ adds for a DSiWare title (short paths, see plan_dsiware)
        char prv[MAX_PATH_LEN], app[MAX_PATH_LEN];
        snprintf(prv, sizeof(prv), "%s", p->prv_path);
        upper_path(prv);
        snprintf(app, sizeof(app), "%s", p->app_path);
        upper_path(app);
        n += snprintf(buf + n, len - (size_t)n,
            "APP_PATH = %s\n"
            "PRV_PATH = %s\n"
            "BNR_PATH = \n"
            "DONOR_SDK_VER = 5\n"
            "GAME_SOFT_RESET = 1\n"
            "PATCH_MPU_REGION = 0\n"
            "PATCH_MPU_SIZE = 0\n",
            app, prv);
    }
    return n;
}

#ifdef ARM9
static void create_blank_save(const char *path) {
    if (file_exists(path)) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static uint8_t ff[4096];
    memset(ff, 0xFF, sizeof(ff));
    for (int i = 0; i < 128; i++) fwrite(ff, 1, sizeof(ff), f); // 512 KB, the size TWiLight uses for unknown titles
    fclose(f);
}
#endif

static bool write_ini(const LaunchPlan *p, const SystemSettings *cfg) {
    char path[MAX_PATH_LEN], mp[MAX_PATH_LEN * 2];
    snprintf(path, sizeof(path), "%s/_nds", get_drive_prefix());
    map_path(path, mp, sizeof(mp));
    make_dirs(mp);
    snprintf(path, sizeof(path), "%s/_nds/nds-bootstrap.ini", get_drive_prefix());
    static char buf[2048];
    int n = launcher_format_ini(p, cfg, buf, sizeof(buf));
    if (n <= 0 || (size_t)n >= sizeof(buf)) return false;
    FILE *f = open_mapped(path, "w");
    if (!f) return false;
    bool ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n;
    if (fclose(f) != 0) ok = false;
    return ok;
}

static LaunchResult launch_nand_app(const GameItem *item, const char *app, char *detail, size_t dlen);
static LaunchResult launch_dsiware(const LaunchPlan *p, char *detail, size_t dlen);

LaunchResult launcher_run(const GameItem *item, const SystemSettings *cfg, char *detail, size_t dlen, int *loader_code) {
    static LaunchPlan plan;
    if (loader_code) *loader_code = 0;
    LaunchResult r = launcher_plan(item, cfg, &plan, detail, dlen);
    if (r != LAUNCH_OK) return r;
    if (item->type == TYPE_APP) return launch_nand_app(item, plan.boot_path, detail, dlen);
    if (plan.dsiware) return launch_dsiware(&plan, detail, dlen);

    int slot = cfg ? cfg->save_slot : 0;
    backup_game_save(item, slot);
    if (plan.mkdir_path[0]) make_dirs(plan.mkdir_path);
    if (plan.ini == INI_RETAIL) cheats_prepare(item->path);   // chosen cheats -> cheatData.bin (or none)
    if (plan.ini != INI_NONE && !write_ini(&plan, cfg)) {
        snprintf(detail, dlen, "nds-bootstrap.ini");
        return LAUNCH_ERR_LOADER;
    }

#ifdef ARM9
    if (plan.save_path[0]) create_blank_save(plan.save_path);
    const char *argv[4];
    argv[0] = plan.boot_path;
    for (int i = 1; i < plan.argc; i++) argv[i] = plan.args[i - 1];
    setCpuClock(plan.boost_cpu);
    eRunNdsRetCode rc = runNdsFile(plan.boot_path, plan.argc, argv);
    // Only returns on failure
    setCpuClock(true);
    if (loader_code) *loader_code = (int)rc;
    snprintf(detail, dlen, "%s", plan.boot_path);
    return LAUNCH_ERR_LOADER;
#else
    printf("[sim] boot %s", plan.boot_path);
    for (int i = 1; i < plan.argc; i++) printf(" \"%s\"", plan.args[i - 1]);
    printf("\n");
    return LAUNCH_OK;
#endif
}

#ifdef ARM9
// Unlaunch "AutoLoadInfo" (0x02000800): tells Unlaunch which file to boot on the next
// reboot instead of the "NO BUTTON" default - the same mechanism TWiLight Menu++ uses.
static void unlaunch_set_boot(const char *path) {
    volatile u8 *base = (volatile u8 *)0x02000800;
    memcpy((void *)base, "AutoLoadInfo", 12);
    *(volatile u16 *)(base + 0x0C) = 0x3F0;            // length covered by the CRC
    *(volatile u16 *)(base + 0x0E) = 0;
    *(volatile u32 *)(base + 0x10) = (1u << 0) | (1u << 1); // load the file at +0x38, use colours
    *(volatile u16 *)(base + 0x14) = 0x0000;           // upper screen colour (black)
    *(volatile u16 *)(base + 0x16) = 0x0000;           // lower screen colour
    memset((void *)(base + 0x18), 0, 0x20 + 0x208 + 0x1C0);
    volatile u16 *name = (volatile u16 *)(base + 0x38);   // UTF-16, the path is UTF-8
    const unsigned char *s = (const unsigned char *)path;
    for (int i = 0; *s && i < 0x103; i++) {
        u32 c = *s++;
        if (c >= 0xE0 && s[0] && s[1]) { c = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F); s += 2; }
        else if (c >= 0xC0 && s[0]) { c = ((c & 0x1F) << 6) | (s[0] & 0x3F); s += 1; }
        name[i] = (u16)c;
    }
    *(volatile u16 *)(base + 0x0E) = swiCRC16(0xFFFF, (void *)(base + 0x10), 0x3F0);
}

// Read-only access to the DSi system memory, mounted only when "Return to DSi Menu"
// is chosen (mounting it at start-up made the boot several seconds slower)
static u8 s_nand_bounce[16 * 512] __attribute__((aligned(32)));
static bool nand_startup(void) { return blkDevInit(BlkDevice_TwlNandAes); }
static bool nand_inserted(void) { return blkDevIsPresent(BlkDevice_TwlNandAes); }
static bool nand_read(sec_t sector, sec_t count, void *buf) {
    u8 *out = (u8 *)buf;
    while (count) {
        sec_t n = count > 16 ? 16 : count;
        if (!blkDevReadSectors(BlkDevice_TwlNandAes, s_nand_bounce, sector, n)) return false;
        memcpy(out, s_nand_bounce, n * 512);
        out += n * 512; sector += n; count -= n;
    }
    return true;
}
static bool nand_write(sec_t sector, sec_t count, const void *buf) { (void)sector; (void)count; (void)buf; return false; }
static bool nand_nop(void) { return true; }
static const DISC_INTERFACE s_nand_iface = {
    0x444E414E /* "NAND" */, FEATURE_MEDIUM_CANREAD,
    nand_startup, nand_inserted, nand_read, nand_write, nand_nop, nand_nop,
};

static bool nand_mount(void) {
    static int state = 0; // 0 = not tried, 1 = mounted, 2 = failed
    if (state == 0) state = dvmProbeMountDiscIface("nand", &s_nand_iface, 8, 8) ? 1 : 2;
    return state == 1;
}

// nand:/title/00030017/484E41xx/content/000000yy.app of the DSi Menu, read from the
// console's own HWINFO_S.dat and title.tmd
static bool find_dsi_menu(char *out, size_t len) {
    if (!nand_mount()) return false;
    if (access("nand:/launcher.dsi", F_OK) == 0) { snprintf(out, len, "nand:/launcher.dsi"); return true; }
    FILE *f = fopen("nand:/sys/HWINFO_S.dat", "rb");
    if (!f) return false;
    u32 tid = 0;
    bool ok = fseek(f, 0xA0, SEEK_SET) == 0 && fread(&tid, 4, 1, f) == 1;
    fclose(f);
    if (!ok || (tid & 0xFFFFFF00u) != 0x484E4100u) return false;   // "HNA?"
    char dir[64];
    snprintf(dir, sizeof(dir), "nand:/title/00030017/%08lx", (unsigned long)tid);
    return launcher_title_app(dir, out, len);
}
#endif

bool launcher_nand_root(char *out, size_t len) {
    if (s_nand_root[0]) { snprintf(out, len, "%s", s_nand_root); return true; }
#ifdef ARM9
    if (systemIsTwlMode() && nand_mount()) { snprintf(out, len, "nand:"); return true; }
#endif
    return false;
}

// ".../title/00030004/4b475545" -> 0x000300044B475545
static uint64_t title_id_of(const char *path) {
    const char *t = strstr(path, "/title/");
    if (!t) return 0;
    unsigned long hi = 0, lo = 0;
    if (sscanf(t + 7, "%8lx/%8lx", &hi, &lo) != 2) return 0;
    return ((uint64_t)hi << 32) | lo;
}

static LaunchResult launch_nand_app(const GameItem *item, const char *app, char *detail, size_t dlen) {
    uint64_t tid = title_id_of(item->path);
#ifdef ARM9
    if (!systemIsTwlMode() || !tid) { snprintf(detail, dlen, "%s", item->path); return LAUNCH_ERR_UNSUPPORTED; }
    unlaunch_set_boot(app);                       // Unlaunch boots the .app on the next start
    TlncData t;                                   // and the DSi system software is asked for the title
    memset(&t, 0, sizeof(t));
    t.caller_tid = 0;
    t.target_tid = tid;
    t.valid = 1;
    t.app_type = TlncAppType_Nand;
    t.skip_intro = 1;
    tlncSetData(&t);
    armDCacheFlushAll();
    pmClearResetJumpTarget();
    exit(0);
#else
    (void)detail; (void)dlen;
    printf("[sim] boot DSi app %016llx %s\n", (unsigned long long)tid, app);
    return LAUNCH_OK;
#endif
}

void return_to_dsi_menu(void) {
#ifdef ARM9
    if (systemIsTwlMode()) {
        char menu[128];
        if (find_dsi_menu(menu, sizeof(menu))) unlaunch_set_boot(menu);
        TlncData t;
        memset(&t, 0, sizeof(t));
        t.caller_tid = 0;
        t.target_tid = 0;        // 0 = system menu (used when Unlaunch is not installed)
        t.valid = 1;
        t.app_type = TlncAppType_Unknown;
        t.skip_intro = 1;
        tlncSetData(&t);
        armDCacheFlushAll();
    }
    pmClearResetJumpTarget();
    exit(0);
#else
    printf("[sim] return to DSi Menu\n");
#endif
}

#ifdef ARM9
#define DSPLAY_PXI_POWER PxiChannel_User0   // arm7/main.c
static void unmount_all(void) {
    fflush(NULL);
    fatUnmount("sd");                    // writes back the FAT cache
    fatUnmount("fat");
}
static void clear_unlaunch_autoload(void) {   // the next start must be the normal one
    memset((void *)0x02000800, 0, 0x10);
    armDCacheFlushAll();
}
#endif

#ifdef ARM9
static volatile bool s_power_tap;
static volatile bool s_power_ok;          // DSplay's own ARM7 answered (it can turn the DSi off)
static volatile int s_level_vol = -1, s_level_bl = -1;
static volatile int s_boot_back = -1;       // 1 back from a game, 0 power on, -1 no answer (stock ARM7)
static volatile bool s_mark_ack;
static volatile unsigned s_level_seq;
static void power_pxi(void *user, u32 data) {
    (void)user;
    if (data == 2) s_power_tap = true;
    else if (data == 5) s_power_ok = true;
    else if (data == 7) s_mark_ack = true;
    else if ((data & 0xF000) == 0x2000) s_boot_back = (int)(data & 1);
    else if ((data & 0xF000) == 0x1000) {
        s_level_vol = (int)(data & 0x1F);
        s_level_bl = (int)((data >> 5) & 7);
        s_level_seq++;
    }
}
#endif

void power_button_init(void) {
#ifdef ARM9
    pxiSetHandler(DSPLAY_PXI_POWER, power_pxi, NULL);
    pxiSend(DSPLAY_PXI_POWER, 4);         // "are you DSplay's ARM7?" (the stock one ignores it)
#endif
}

#ifndef ARM9
static int s_sim_vol = 20, s_sim_bl = 3;
static unsigned s_sim_seq = 1;
void console_levels_sim(int vol, int bright) { s_sim_vol = vol; s_sim_bl = bright; s_sim_seq++; }
#endif

// 1: this start comes back from a game DSplay started, 0: the console was switched on,
// -1: unknown (the stock ARM7 cannot tell). Waits up to ~1/4 s for the ARM7's answer.
int launcher_boot_kind(void) {
#ifdef ARM9
    // the ARM7 may not have been listening yet when the first hello went out: ask again
    for (int i = 0; i < 40 && s_boot_back < 0; i++) {
        if ((i % 4) == 3) pxiSend(DSPLAY_PXI_POWER, 4);
        threadWaitForVBlank();
    }
    return s_boot_back;
#else
    const char *e = getenv("HL_BOOT_KIND");
    return e ? atoi(e) : -1;
#endif
}

// Before a game starts: the ARM7 notes it in the MCU (kept over the reset into the game)
void launcher_mark_game_start(void) {
#ifdef ARM9
    if (!s_power_ok) return;
    s_mark_ack = false;
    pxiSend(DSPLAY_PXI_POWER, 6);
    for (int i = 0; i < 10 && !s_mark_ack; i++) threadWaitForVBlank();
#endif
}

void launcher_mark_game_start_cancel(void) {
#ifdef ARM9
    if (s_power_ok) pxiSend(DSPLAY_PXI_POWER, 8);
#endif
}

unsigned console_levels(int *vol, int *bright) {
#ifdef ARM9
    if (vol) *vol = s_level_vol;
    if (bright) *bright = s_level_bl;
    return s_level_seq;
#else
    if (vol) *vol = s_sim_vol;
    if (bright) *bright = s_sim_bl;
    return s_sim_seq;
#endif
}

// called every frame: repeats the hello for a few seconds until DSplay's ARM7 answers
void power_button_poll(void) {
#ifdef ARM9
    static int frames;
    if (s_power_ok || frames > 60 * 5) return;
    if ((++frames % 20) == 0) pxiSend(DSPLAY_PXI_POWER, 4);
#endif
}

bool power_off_supported(void) {
#ifdef ARM9
    return s_power_ok;
#else
    return getenv("HL_NO_POWEROFF") == NULL;
#endif
}

bool power_button_tapped(void) {
#ifdef ARM9
    if (!s_power_tap) return false;
    s_power_tap = false;
    return true;
#else
    return false;
#endif
}

void power_off_console(void) {
#ifdef ARM9
    unmount_all();
    clear_unlaunch_autoload();
    pxiSend(DSPLAY_PXI_POWER, 1);
    for (int i = 0; i < 180; i++) threadWaitForVBlank();   // the ARM7 cuts the power right away
    pmClearResetJumpTarget();            // older ARM7 without the request: restart instead
    exit(0);
#else
    printf("[sim] power off\n");
#endif
}

void restart_console(void) {
#ifdef ARM9
    unmount_all();
    clear_unlaunch_autoload();
    pmClearResetJumpTarget();            // no jump target: the ARM7 resets the DSi through the MCU
    exit(0);
#else
    printf("[sim] restart\n");
#endif
}

// ---- DSiWare saves per profile --------------------------------------------------------------
// The game reads its saves from one place (next to the .nds for Unlaunch, sd:/_hyper/dsw/ for
// nds-bootstrap); each profile keeps its own copy. For a launch the profile's save is copied in
// and a file that was already there (the first profile's save) is put aside in hold.*; at the
// next start of DSplay the played save goes back to the profile and hold.* back in place.
// <drv>/_hyper/dsw/pending.txt drives it, every step is checked before anything is removed:
//   line 1  "P" prepared (not started yet)  /  "L" launched  /  "R" played save already kept
//   then    "<live>|<home>|<hold or empty>" for the public and the private save
#define DSW_MAX 2
typedef struct { char live[MAX_PATH_LEN], home[MAX_PATH_LEN], hold[MAX_PATH_LEN]; } DswEntry;

static void dsw_file(const char *name, char *out, size_t len) {
    snprintf(out, len, "%s" DSW_DIR "/%s", get_drive_prefix(), name);
}

static bool dsw_write_marker(char stage, const DswEntry *e, int n) {
    char p[MAX_PATH_LEN], mp[MAX_PATH_LEN * 2], tmp[MAX_PATH_LEN * 2 + 8];
    dsw_file("pending.txt", p, sizeof(p));
    map_path(p, mp, sizeof(mp));
    snprintf(tmp, sizeof(tmp), "%s.tmp", mp);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    fprintf(f, "%c\n", stage);
    for (int i = 0; i < n; i++) fprintf(f, "%s|%s|%s\n", e[i].live, e[i].home, e[i].hold);
    bool ok = fclose(f) == 0;
    if (ok) { remove(mp); ok = rename(tmp, mp) == 0; }
    if (!ok) remove(tmp);
    return ok;
}

// copy + compare: the source is only ever removed after this said yes
static bool dsw_copy(const char *src, const char *dst) {
    char a[MAX_PATH_LEN * 2], b[MAX_PATH_LEN * 2];
    map_path(src, a, sizeof(a));
    map_path(dst, b, sizeof(b));
    char dir[MAX_PATH_LEN * 2];
    snprintf(dir, sizeof(dir), "%s", b);
    char *sl = strrchr(dir, '/');
    if (sl) { *sl = '\0'; make_dirs(dir); }
    if (file_exists(b) && same_file(a, b)) return true;
    return copy_file(a, b) && same_file(a, b);
}

static bool dsw_same(const char *x, const char *y) {
    char a[MAX_PATH_LEN * 2], b[MAX_PATH_LEN * 2];
    map_path(x, a, sizeof(a));
    map_path(y, b, sizeof(b));
    return same_file(a, b);
}

static void dsw_remove(const char *path) {
    char a[MAX_PATH_LEN * 2];
    map_path(path, a, sizeof(a));
    remove(a);
}

// puts the files of the first profile back (stage P / R): hold -> live, or our copy removed
static bool dsw_restore(const DswEntry *e, int n) {
    bool ok = true;
    for (int i = 0; i < n; i++) {
        if (e[i].hold[0]) {
            if (!file_exists(e[i].hold)) continue;            // already back (or never set aside)
            if (dsw_copy(e[i].hold, e[i].live)) dsw_remove(e[i].hold);
            else ok = false;
        } else if (file_exists(e[i].live)) {
            // only a copy of the profile's save (or a blank one) is here: the profile has it
            if (!e[i].home[0] || !file_exists(e[i].home) || dsw_same(e[i].live, e[i].home)) dsw_remove(e[i].live);
            else ok = false;
        }
    }
    return ok;
}

bool launcher_dsiware_resume(void) {
    char p[MAX_PATH_LEN], mp[MAX_PATH_LEN * 2];
    dsw_file("pending.txt", p, sizeof(p));
    map_path(p, mp, sizeof(mp));
    FILE *f = fopen(mp, "r");
    if (!f) return true;
    static DswEntry e[DSW_MAX];
    int n = 0;
    char line[MAX_PATH_LEN * 3 + 8];
    char stage = 0;
    if (fgets(line, sizeof(line), f)) stage = line[0];
    while (n < DSW_MAX && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *a = line, *b = strchr(a, '|'), *c = b ? strchr(b + 1, '|') : NULL;
        if (!b || !c) continue;
        *b = '\0'; *c = '\0';
        snprintf(e[n].live, sizeof(e[n].live), "%s", a);
        snprintf(e[n].home, sizeof(e[n].home), "%s", b + 1);
        snprintf(e[n].hold, sizeof(e[n].hold), "%s", c + 1);
        n++;
    }
    fclose(f);
    if (stage != 'P' && stage != 'L' && stage != 'R') return false;   // unreadable: keep it, touch nothing
    if (stage == 'L') {
        // the game ran: its saves go back to the profile first
        for (int i = 0; i < n; i++)
            if (e[i].home[0] && file_exists(e[i].live) && !dsw_copy(e[i].live, e[i].home)) return false;
        if (!dsw_write_marker('R', e, n)) return false;
    }
    if (!dsw_restore(e, n)) return false;
    remove(mp);
    return true;
}

static bool dsw_make_live(const char *home, const char *live, uint32_t size) {
    if (home[0] && file_exists(home)) return dsw_copy(home, live);
    if (file_exists(live)) dsw_remove(live);
    return launcher_make_dsiware_save(live, size);
}

// one generation of the kept save, in the profile's backups folder
static void dsw_backup(const char *home) {
    if (!home[0] || !file_exists(home)) return;
    char dir[MAX_PATH_LEN], to[MAX_PATH_LEN * 2];
    if (s_profile_dir[0]) snprintf(dir, sizeof(dir), "%s/backups", s_profile_dir);
    else snprintf(dir, sizeof(dir), "%s/_hyper/backups", get_drive_prefix());
    const char *name = strrchr(home, '/');
    snprintf(to, sizeof(to), "%s/%s.bak", dir, name ? name + 1 : home);
    dsw_copy(home, to);
}

static LaunchResult launch_dsiware(const LaunchPlan *p, char *detail, size_t dlen) {
    // anything left from the last launch first; never start on top of it
    if (!launcher_dsiware_resume()) { dsw_file("pending.txt", detail, dlen); return LAUNCH_ERR_LOADER; }
    char dir[MAX_PATH_LEN], mdir[MAX_PATH_LEN * 2];
    dsw_file("", dir, sizeof(dir));
    map_path(dir, mdir, sizeof(mdir));
    make_dirs(mdir);
    DswEntry e[DSW_MAX];
    uint32_t sz[DSW_MAX];
    int n = 0;
    const char *const live[2] = { p->pub_path, p->prv_path }, *const home[2] = { p->home_pub, p->home_prv };
    const char *const hold[2] = { "hold.pub", "hold.prv" };
    const uint32_t size[2] = { p->pub_size, p->prv_size };
    for (int i = 0; i < 2; i++) {
        if (!live[i][0]) continue;
        memset(&e[n], 0, sizeof(e[n]));
        snprintf(e[n].live, sizeof(e[n].live), "%s", live[i]);
        snprintf(e[n].home, sizeof(e[n].home), "%s", home[i]);
        if (strcmp(e[n].live, e[n].home) != 0 && file_exists(e[n].live)) dsw_file(hold[i], e[n].hold, sizeof(e[n].hold));
        sz[n] = size[i];
        n++;
    }
    bool swap = false;
    for (int i = 0; i < n; i++) if (strcmp(e[i].live, e[i].home) != 0) swap = true;
    if (!swap) {
        // first profile with Unlaunch: the saves stay next to the game, made only when missing
        for (int i = 0; i < n; i++)
            if (!file_exists(e[i].live) && !launcher_make_dsiware_save(e[i].live, sz[i])) {
                snprintf(detail, dlen, "%s", e[i].live);
                return LAUNCH_ERR_LOADER;
            }
    } else {
        for (int i = 0; i < n; i++) dsw_backup(e[i].home);
        if (!dsw_write_marker('P', e, n)) { dsw_file("pending.txt", detail, dlen); return LAUNCH_ERR_LOADER; }
        for (int i = 0; i < n; i++) {
            bool ok = (!e[i].hold[0] || dsw_copy(e[i].live, e[i].hold)) && dsw_make_live(e[i].home, e[i].live, sz[i]);
            if (!ok) {
                launcher_dsiware_resume();         // stage P: puts everything back
                snprintf(detail, dlen, "%s", e[i].live);
                return LAUNCH_ERR_LOADER;
            }
        }
        if (!dsw_write_marker('L', e, n)) { launcher_dsiware_resume(); dsw_file("pending.txt", detail, dlen); return LAUNCH_ERR_LOADER; }
    }
    if (p->app_copy) {
        // long path: nds-bootstrap gets a copy under a short name (kept until another title needs it)
        char tag[MAX_PATH_LEN], mtag[MAX_PATH_LEN * 2], want[MAX_PATH_LEN + 32], have[MAX_PATH_LEN + 32] = "";
        dsw_file("app.txt", tag, sizeof(tag));
        map_path(tag, mtag, sizeof(mtag));
        char mrom[MAX_PATH_LEN * 2];
        map_path(p->ini_nds_path, mrom, sizeof(mrom));
        struct stat st;
        long rsz = stat(mrom, &st) == 0 ? (long)st.st_size : -1;
        snprintf(want, sizeof(want), "%s|%ld", p->ini_nds_path, rsz);
        FILE *t = fopen(mtag, "r");
        if (t) { if (!fgets(have, sizeof(have), t)) have[0] = '\0'; fclose(t); have[strcspn(have, "\r\n")] = '\0'; }
        if (strcmp(want, have) != 0 || !file_exists(p->app_path)) {
            remove(mtag);
            if (!dsw_copy(p->ini_nds_path, p->app_path)) {
                launcher_dsiware_resume();
                snprintf(detail, dlen, "%s", p->app_path);
                return LAUNCH_ERR_LOADER;
            }
            t = fopen(mtag, "w");
            if (t) { fprintf(t, "%s\n", want); fclose(t); }
        }
    }
#ifdef ARM9
    if (!systemIsTwlMode()) { launcher_dsiware_resume(); snprintf(detail, dlen, "%s", p->boot_path); return LAUNCH_ERR_UNSUPPORTED; }
    if (p->dsw_bootstrap) {
        if (!write_ini(p, NULL)) { launcher_dsiware_resume(); snprintf(detail, dlen, "nds-bootstrap.ini"); return LAUNCH_ERR_LOADER; }
        const char *argv[1] = { p->boot_path };
        setCpuClock(true);
        runNdsFile(p->boot_path, 1, argv);
        launcher_dsiware_resume();             // only returns on failure
        snprintf(detail, dlen, "%s", p->boot_path);
        return LAUNCH_ERR_LOADER;
    }
    char path[MAX_PATH_LEN + 8];
    if (strncmp(p->boot_path, "sd:", 3) == 0) snprintf(path, sizeof(path), "sdmc:%s", p->boot_path + 3);
    else snprintf(path, sizeof(path), "%s", p->boot_path);
    unmount_all();
    unlaunch_set_boot(path);
    armDCacheFlushAll();
    pmClearResetJumpTarget();             // the ARM7 resets the DSi; Unlaunch boots the file
    exit(0);
    return LAUNCH_ERR_LOADER;
#else
    (void)detail; (void)dlen;
    if (p->dsw_bootstrap) {
        if (!write_ini(p, NULL)) return LAUNCH_ERR_LOADER;
        printf("[sim] boot %s (dsiware)\n", p->boot_path);
    } else printf("[sim] unlaunch sdmc%s\n", strchr(p->boot_path, ':') ? strchr(p->boot_path, ':') : p->boot_path);
    return LAUNCH_OK;
#endif
}

void launch_slot1_card(void) {
#ifdef ARM9
    if (systemIsTwlMode()) {
        // Same launcher parameters TWiLight Menu++ uses for "Launch Slot-1 card"
        TlncData t;
        memset(&t, 0, sizeof(t));
        t.caller_tid = 0x43415254ull; // "TRAC"
        t.target_tid = 0x43415254ull;
        t.valid = 1;
        t.app_type = TlncAppType_Card;
        t.skip_intro = 1;
        tlncSetData(&t);
        pmClearResetJumpTarget();
        exit(0);
    }
#else
    printf("[sim] boot Slot-1 card\n");
#endif
}

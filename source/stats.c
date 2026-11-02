#include "stats.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

static char s_drv[128] = "";

void stats_set_drive(const char *drv) {
    snprintf(s_drv, sizeof(s_drv), "%s", drv ? drv : "");
}

const char *stats_drive(void) { return s_drv; }

static int s_profile = -1;   // -1: files directly in _hyper/ (tests, before profiles are loaded)

void stats_set_profile(int slot) { s_profile = slot; }

void stats_path(char *out, size_t len, const char *name) {
    if (s_profile >= 0) snprintf(out, len, "%s/_hyper/profiles/p%d/%s", s_drv, s_profile, name);
    else snprintf(out, len, "%s/_hyper/%s", s_drv, name);
}

const char *path_without_drive(const char *path) {
    if (!path) return "";
    const char *colon = strchr(path, ':');
    const char *slash = strchr(path, '/');
    if (colon && (!slash || colon < slash)) return colon + 1;
    return path;
}

bool file_exists(const char *path) {
    struct stat st;
    if (!path) return false;
#ifndef ARM9
    // Host tests: map "sd:/..." / "fat:/..." onto a folder given in HL_SDROOT
    const char *root = getenv("HL_SDROOT");
    const char *colon = strchr(path, ':');
    if (root && colon && colon - path <= 4) {
        char mapped[MAX_PATH_LEN * 2];
        snprintf(mapped, sizeof(mapped), "%s%s", root, colon + 1);
        return stat(mapped, &st) == 0;
    }
#endif
    return stat(path, &st) == 0;
}

// Copies through "<dst>.hltmp", checks the length, then swaps it in. The old file is renamed
// out of the way first, so a power cut leaves either the old or the new file (never a half one).
bool copy_file(const char *src, const char *dst) {
    char tmp[MAX_PATH_LEN + 8], old[MAX_PATH_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.hltmp", dst);
    snprintf(old, sizeof(old), "%s.hlold", dst);
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(tmp, "wb");
    if (!out) { fclose(in); return false; }
    static uint8_t buf[8192];
    size_t n;
    long total = 0;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
        total += (long)n;
    }
    if (ferror(in)) ok = false;
    fseek(in, 0, SEEK_END);
    if (ftell(in) != total) ok = false;
    if (fclose(out) != 0) ok = false;
    fclose(in);
    if (ok) {
        FILE *chk = fopen(tmp, "rb");
        if (!chk) ok = false;
        else { fseek(chk, 0, SEEK_END); if (ftell(chk) != total) ok = false; fclose(chk); }
    }
    if (!ok) { remove(tmp); return false; }
    remove(old);
    bool had = rename(dst, old) == 0;
    if (rename(tmp, dst) != 0) {
        if (had) rename(old, dst);
        remove(tmp);
        return false;
    }
    if (had) remove(old);
    return true;
}

void make_dirs(const char *path) {
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' && *(p - 1) != ':') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

// --------------------------------------------------------------------------
// Settings
// --------------------------------------------------------------------------
void settings_defaults(SystemSettings *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = SETTINGS_MAGIC;
    cfg->theme = 0;
    cfg->splash = 1;          // SPLASH_ON: boot animation by default
    cfg->language = LANG_TR;
    cfg->sfx_pack = SFX_PACK_HYPER;
    cfg->volume = 4;
    cfg->brightness = 4;
    cfg->sort_mode = SORT_RECENT;
    cfg->save_slot = 0;
    cfg->anim_off = 0;
    cfg->show_hidden = 0;
    snprintf(cfg->profile_name, sizeof(cfg->profile_name), "%s", "Oyuncu");
}

bool settings_load(SystemSettings *cfg) {
    settings_defaults(cfg);
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "settings.bin");
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    SystemSettings tmp = *cfg;            // fields a shorter (older) file does not have keep the defaults
    size_t r = fread(&tmp, 1, sizeof(tmp), f);
    fclose(f);
    if (r < SETTINGS_SIZE_V1 || tmp.magic != SETTINGS_MAGIC) return false;
    if (r < sizeof(tmp)) memcpy((uint8_t *)&tmp + r, (const uint8_t *)cfg + r, sizeof(tmp) - r);
    tmp.home_btns &= HOMEBTN_POWER | HOMEBTN_PAD | HOMEBTN_NEARBY | HOMEBTN_PHOTOS | HOMEBTN_CARTBOX;
    tmp.beta = tmp.beta ? 1 : 0;
    tmp.photo_opts &= PHOTOOPT_SAVER_MASK | PHOTOOPT_FAVS_ONLY | PHOTOOPT_NO_FRAME | PHOTOOPT_NO_POD;
    if ((tmp.photo_opts & PHOTOOPT_SAVER_MASK) > 4) tmp.photo_opts &= (uint8_t)~PHOTOOPT_SAVER_MASK;
    memset(tmp.reserved, 0, sizeof(tmp.reserved));
    if (tmp.theme > 1) tmp.theme = 0;
    if (tmp.language >= LANG_COUNT) tmp.language = LANG_TR;
    if (tmp.sfx_pack != SFX_PACK_MUTE) tmp.sfx_pack = SFX_PACK_HYPER;   // "classic" is now a Studio download
    if (tmp.volume > 5) tmp.volume = 4;
    if (tmp.brightness < 1 || tmp.brightness > 5) tmp.brightness = 4;
    if (tmp.sort_mode >= SORT_COUNT) tmp.sort_mode = SORT_RECENT;
    if (tmp.save_slot > 2) tmp.save_slot = 0;
    tmp.anim_off = tmp.anim_off ? 1 : 0;
    tmp.show_hidden = tmp.show_hidden ? 1 : 0;
    tmp.covers = tmp.covers ? 1 : 0;
    tmp.splash = (tmp.splash == 4 || tmp.splash == 2) ? tmp.splash : 1;   // OFF / CUSTOM / ON (older animation numbers mean on)
    tmp.home_hide &= 0x7F;
    tmp.home_show &= HOMESHOW_PAD | HOMEOPT_NO_PHOTOS | HOMEOPT_NO_CLOCK | HOMEOPT_NO_STORE | HOMEOPT_NO_NEARBY | GAMEINFO_MASK;
    if ((tmp.home_show & GAMEINFO_MASK) == GAMEINFO_MASK) tmp.home_show &= ~GAMEINFO_MASK;
    if (tmp.home_games != 4 && tmp.home_games != 8 && tmp.home_games != HOME_GAMES_NONE) tmp.home_games = 0;
    tmp.profile_name[sizeof(tmp.profile_name) - 1] = '\0';
    if (!tmp.profile_name[0]) snprintf(tmp.profile_name, sizeof(tmp.profile_name), "%s", "Oyuncu");
    *cfg = tmp;
    return true;
}

bool settings_save(const SystemSettings *cfg) {
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "settings.bin");
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(cfg, sizeof(*cfg), 1, f) == 1;
    fclose(f);
    return ok;
}

// --------------------------------------------------------------------------
// Play time
// --------------------------------------------------------------------------
typedef struct {
    uint32_t magic;
    char     filename[MAX_TITLE_LEN];
    uint32_t start_ts;
} SessionFile;

void playtime_load(PlaytimeDB *db) {
    memset(db, 0, sizeof(*db));
    db->magic = PLAYTIME_MAGIC;
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "playtime.bin");
    FILE *f = fopen(path, "rb");
    if (!f) return;
    size_t r = fread(db, sizeof(*db), 1, f);
    fclose(f);
    if (r != 1 || db->magic != PLAYTIME_MAGIC || db->count < 0 || db->count > MAX_PLAYTIME_ENTRIES) {
        memset(db, 0, sizeof(*db));
        db->magic = PLAYTIME_MAGIC;
    }
}

bool playtime_save(const PlaytimeDB *db) {
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "playtime.bin");
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(db, sizeof(*db), 1, f) == 1;
    fclose(f);
    return ok;
}

const PlaytimeEntry *playtime_get(const PlaytimeDB *db, const char *filename) {
    if (!db || !filename || !filename[0]) return NULL;
    for (int i = 0; i < db->count; i++) {
        if (strcasecmp(db->entries[i].filename, filename) == 0) return &db->entries[i];
    }
    return NULL;
}

static PlaytimeEntry *playtime_entry(PlaytimeDB *db, const char *filename) {
    for (int i = 0; i < db->count; i++) {
        if (strcasecmp(db->entries[i].filename, filename) == 0) return &db->entries[i];
    }
    int slot = db->count;
    if (slot >= MAX_PLAYTIME_ENTRIES) {
        // replace the least recently played entry
        slot = 0;
        for (int i = 1; i < db->count; i++) {
            if (db->entries[i].last_played_ts < db->entries[slot].last_played_ts) slot = i;
        }
    } else {
        db->count++;
    }
    PlaytimeEntry *e = &db->entries[slot];
    memset(e, 0, sizeof(*e));
    snprintf(e->filename, sizeof(e->filename), "%s", filename);
    return e;
}

void playtime_begin_session(PlaytimeDB *db, const GameItem *item) {
    if (!db || !item || !item->filename[0]) return;
    uint32_t now = (uint32_t)time(NULL);
    PlaytimeEntry *e = playtime_entry(db, item->filename);
    e->launch_count++;
    e->last_played_ts = now;
    playtime_save(db);

    SessionFile sf;
    memset(&sf, 0, sizeof(sf));
    sf.magic = PLAYTIME_MAGIC;
    snprintf(sf.filename, sizeof(sf.filename), "%s", item->filename);
    sf.start_ts = now;
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "session.bin");
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(&sf, sizeof(sf), 1, f);
        fclose(f);
    }
}

void playtime_end_session(PlaytimeDB *db) {
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "session.bin");
    FILE *f = fopen(path, "rb");
    if (!f) return;
    SessionFile sf;
    size_t r = fread(&sf, sizeof(sf), 1, f);
    fclose(f);
    unlink(path);
    if (r != 1 || sf.magic != PLAYTIME_MAGIC || !sf.filename[0]) return;
    sf.filename[sizeof(sf.filename) - 1] = '\0';
    uint32_t now = (uint32_t)time(NULL);
    if (now <= sf.start_ts) return;
    uint32_t minutes = (now - sf.start_ts) / 60;
    if (minutes < 1) minutes = 1;
    if (minutes > 300) minutes = 300; // console was probably switched off mid-game
    PlaytimeEntry *e = playtime_entry(db, sf.filename);
    e->total_minutes += minutes;
    if (!e->last_played_ts) e->last_played_ts = sf.start_ts;
    playtime_save(db);
    daylog_add(sf.start_ts, minutes);
}

// --------------------------------------------------------------------------
// Minutes played per day (profile statistics: last 7 days)
// --------------------------------------------------------------------------
#define DAYLOG_MAGIC 0x31474C44u // "DLG1"
#define DAYLOG_N     32
typedef struct {
    uint32_t magic;
    uint32_t day[DAYLOG_N];      // days since 1970 (local time from the console clock)
    uint32_t minutes[DAYLOG_N];
} DayLog;

static void daylog_read(DayLog *d) {
    memset(d, 0, sizeof(*d));
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "daylog.bin");
    FILE *f = fopen(path, "rb");
    if (f) {
        if (fread(d, sizeof(*d), 1, f) != 1 || d->magic != DAYLOG_MAGIC) memset(d, 0, sizeof(*d));
        fclose(f);
    }
    d->magic = DAYLOG_MAGIC;
}

void daylog_add(uint32_t ts, uint32_t minutes) {
    DayLog d;
    daylog_read(&d);
    uint32_t day = ts / 86400u;
    int slot = -1, oldest = 0;
    for (int i = 0; i < DAYLOG_N; i++) {
        if (d.day[i] == day && d.minutes[i]) { slot = i; break; }
        if (d.day[i] < d.day[oldest]) oldest = i;
    }
    if (slot < 0) { slot = oldest; d.day[slot] = day; d.minutes[slot] = 0; }
    d.minutes[slot] += minutes;
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "daylog.bin");
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(&d, sizeof(d), 1, f); fclose(f); }
}

void daylog_last_days(uint32_t now, uint32_t out[7]) {
    DayLog d;
    daylog_read(&d);
    uint32_t today = now / 86400u;
    for (int k = 0; k < 7; k++) {
        out[k] = 0;
        uint32_t day = today - (uint32_t)(6 - k);
        for (int i = 0; i < DAYLOG_N; i++) if (d.minutes[i] && d.day[i] == day) out[k] += d.minutes[i];
    }
}

// --------------------------------------------------------------------------
// Favorites / hidden path sets
// --------------------------------------------------------------------------
#define MAX_SET_ITEMS 256
typedef struct {
    char (*paths)[MAX_PATH_LEN];
    int count;
    const char *file;
} PathSet;

static char s_fav_buf[MAX_SET_ITEMS][MAX_PATH_LEN];
static char s_hid_buf[MAX_SET_ITEMS][MAX_PATH_LEN];
static PathSet s_fav = { s_fav_buf, 0, "favorites.txt" };
static PathSet s_hid = { s_hid_buf, 0, "hidden.txt" };

static void set_load(PathSet *ps) {
    ps->count = 0;
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), ps->file);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_PATH_LEN];
    while (ps->count < MAX_SET_ITEMS && fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
        if (!l) continue;
        snprintf(ps->paths[ps->count++], MAX_PATH_LEN, "%s", path_without_drive(line));
    }
    fclose(f);
}

static void set_save(const PathSet *ps) {
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), ps->file);
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < ps->count; i++) fprintf(f, "%s\n", ps->paths[i]);
    fclose(f);
}

static int set_find(const PathSet *ps, const char *full) {
    const char *p = path_without_drive(full);
    const char *base = strrchr(p, '/');
    base = base ? base + 1 : p;
    for (int i = 0; i < ps->count; i++) {
        if (strcasecmp(ps->paths[i], p) == 0) return i;
        if (!strchr(ps->paths[i], '/') && strcasecmp(ps->paths[i], base) == 0) return i; // legacy entries
    }
    return -1;
}

static void set_put(PathSet *ps, const char *full, bool on) {
    int i = set_find(ps, full);
    if (on && i < 0 && ps->count < MAX_SET_ITEMS) {
        snprintf(ps->paths[ps->count++], MAX_PATH_LEN, "%s", path_without_drive(full));
        set_save(ps);
    } else if (!on && i >= 0) {
        for (int k = i; k < ps->count - 1; k++) memcpy(ps->paths[k], ps->paths[k + 1], MAX_PATH_LEN);
        ps->count--;
        set_save(ps);
    }
}

void pathset_load(void) {
    set_load(&s_fav);
    set_load(&s_hid);
}

bool pathset_is_favorite(const char *path) { return set_find(&s_fav, path) >= 0; }
bool pathset_is_hidden(const char *path)   { return set_find(&s_hid, path) >= 0; }
void pathset_set_favorite(const char *path, bool on) { set_put(&s_fav, path, on); }
void pathset_set_hidden(const char *path, bool on)   { set_put(&s_hid, path, on); }
void pathset_forget(const char *path) {
    set_put(&s_fav, path, false);
    set_put(&s_hid, path, false);
}

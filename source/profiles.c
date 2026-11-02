// User profiles: see include/profiles.h for the folder layout.
#include "profiles.h"
#include "stats.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char           s_drv[128];
static ProfileInfo    s_info[MAX_PROFILES];
static bool           s_have[MAX_PROFILES];
static ProfilesGlobal s_glob;
static int            s_active = -1;

void profiles_dir(int slot, char *out, size_t len) {
    snprintf(out, len, "%s/_hyper/profiles/p%d", s_drv, slot);
}

void profiles_file(int slot, const char *name, char *out, size_t len) {
    snprintf(out, len, "%s/_hyper/profiles/p%d/%s", s_drv, slot, name);
}

static void global_path(char *out, size_t len) {
    snprintf(out, len, "%s/_hyper/profiles/profiles.bin", s_drv);
}

static bool read_info(int slot, ProfileInfo *out) {
    char p[MAX_PATH_LEN];
    profiles_file(slot, "profile.bin", p, sizeof(p));
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    ProfileInfo t;
    memset(&t, 0, sizeof(t));
    size_t r = fread(&t, 1, sizeof(t), f);   // older / shorter files are fine
    fclose(f);
    if (r < 40 || t.magic != PROFILE_MAGIC) return false;
    t.name[sizeof(t.name) - 1] = '\0';
    t.last_title[sizeof(t.last_title) - 1] = '\0';
    if (t.color >= PROFILE_COLORS) t.color = 0;
    if (t.pin_len > PIN_MAX) t.pin_len = 0;
    if (!t.name[0]) snprintf(t.name, sizeof(t.name), "%s", "Oyuncu");
    *out = t;
    return true;
}

static bool write_info(int slot, const ProfileInfo *info) {
    char p[MAX_PATH_LEN];
    profiles_dir(slot, p, sizeof(p));
    make_dirs(p);
    profiles_file(slot, "profile.bin", p, sizeof(p));
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    bool ok = fwrite(info, sizeof(*info), 1, f) == 1;
    fclose(f);
    return ok;
}

bool profiles_global_save(void) {
    char p[MAX_PATH_LEN];
    global_path(p, sizeof(p));
    FILE *f = fopen(p, "wb");
    if (!f) return false;
    bool ok = fwrite(&s_glob, sizeof(s_glob), 1, f) == 1;
    fclose(f);
    return ok;
}

static void move_file(const char *from, const char *to) {
    if (!file_exists(from)) return;
    if (rename(from, to) == 0) return;
    if (copy_file(from, to)) unlink(from);
}

// Before profiles existed everything lived directly in _hyper/: it becomes profile 1
static void migrate_legacy(void) {
    static const char *const files[] = { "settings.bin", "favorites.txt", "hidden.txt", "playtime.bin",
                                         "session.bin", "avatar.bin", "daylog.bin" };
    char dir[MAX_PATH_LEN];
    profiles_dir(0, dir, sizeof(dir));
    make_dirs(dir);
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char from[MAX_PATH_LEN], to[MAX_PATH_LEN];
        snprintf(from, sizeof(from), "%s/_hyper/%s", s_drv, files[i]);
        profiles_file(0, files[i], to, sizeof(to));
        move_file(from, to);
    }
    // name from the old settings file
    ProfileInfo info;
    memset(&info, 0, sizeof(info));
    info.magic = PROFILE_MAGIC;
    info.created = (uint32_t)time(NULL);
    snprintf(info.name, sizeof(info.name), "%s", "Oyuncu");
    char sp[MAX_PATH_LEN];
    profiles_file(0, "settings.bin", sp, sizeof(sp));
    FILE *f = fopen(sp, "rb");
    if (f) {
        SystemSettings cfg;
        if (fread(&cfg, sizeof(cfg), 1, f) == 1 && cfg.magic == SETTINGS_MAGIC && cfg.profile_name[0]) {
            cfg.profile_name[sizeof(cfg.profile_name) - 1] = '\0';
            snprintf(info.name, sizeof(info.name), "%s", cfg.profile_name);
        }
        fclose(f);
    }
    write_info(0, &info);
}

void profiles_init(const char *drv) {
    snprintf(s_drv, sizeof(s_drv), "%s", drv ? drv : "");
    memset(s_have, 0, sizeof(s_have));
    memset(&s_glob, 0, sizeof(s_glob));
    char gp[MAX_PATH_LEN];
    global_path(gp, sizeof(gp));
    FILE *f = fopen(gp, "rb");
    bool have_glob = false;
    if (f) {
        have_glob = fread(&s_glob, sizeof(s_glob), 1, f) == 1 && s_glob.magic == PROFILES_MAGIC;
        fclose(f);
    }
    for (int i = 0; i < MAX_PROFILES; i++) s_have[i] = read_info(i, &s_info[i]);
    if (profiles_count() == 0) migrate_legacy();
    for (int i = 0; i < MAX_PROFILES; i++) if (!s_have[i]) s_have[i] = read_info(i, &s_info[i]);
    if (!have_glob) {
        memset(&s_glob, 0, sizeof(s_glob));
        s_glob.magic = PROFILES_MAGIC;
        s_glob.login = 1;
        profiles_global_save();
    }
    if (s_glob.last >= MAX_PROFILES || !s_have[s_glob.last]) s_glob.last = (uint8_t)profiles_slot(0);
    s_glob.login = s_glob.login ? 1 : 0;
}

int profiles_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROFILES; i++) n += s_have[i] ? 1 : 0;
    return n;
}

int profiles_slot(int idx) {
    for (int i = 0; i < MAX_PROFILES; i++) {
        if (!s_have[i]) continue;
        if (idx-- == 0) return i;
    }
    return 0;
}

bool profiles_exists(int slot) { return slot >= 0 && slot < MAX_PROFILES && s_have[slot]; }

const ProfileInfo *profiles_get(int slot) { return profiles_exists(slot) ? &s_info[slot] : NULL; }

bool profiles_update(int slot, const ProfileInfo *info) {
    if (slot < 0 || slot >= MAX_PROFILES) return false;
    s_info[slot] = *info;
    s_info[slot].magic = PROFILE_MAGIC;
    s_have[slot] = true;
    return write_info(slot, &s_info[slot]);
}

int profiles_free_color(void) {
    for (int c = 0; c < PROFILE_COLORS; c++) {
        bool used = false;
        for (int i = 0; i < MAX_PROFILES; i++) if (s_have[i] && s_info[i].color == c) used = true;
        if (!used) return c;
    }
    return 0;
}

int profiles_create(const char *name) {
    int slot = -1;
    for (int i = 0; i < MAX_PROFILES; i++) if (!s_have[i]) { slot = i; break; }
    if (slot < 0) return -1;
    // a folder left behind by an earlier profile in the same slot starts clean
    char dir[MAX_PATH_LEN];
    profiles_dir(slot, dir, sizeof(dir));
    if (file_exists(dir)) {
        char old[MAX_PATH_LEN];
        snprintf(old, sizeof(old), "%s/_hyper/profiles/deleted_p%d_%lu", s_drv, slot, (unsigned long)time(NULL));
        rename(dir, old);
    }
    ProfileInfo info;
    memset(&info, 0, sizeof(info));
    info.magic = PROFILE_MAGIC;
    info.created = (uint32_t)time(NULL);
    info.color = (uint8_t)profiles_free_color();
    snprintf(info.name, sizeof(info.name), "%s", (name && name[0]) ? name : "Oyuncu");
    if (!profiles_update(slot, &info)) { s_have[slot] = false; return -1; }
    return slot;
}

bool profiles_delete(int slot) {
    if (!profiles_exists(slot) || profiles_count() <= 1) return false;
    char dir[MAX_PATH_LEN], old[MAX_PATH_LEN];
    profiles_dir(slot, dir, sizeof(dir));
    snprintf(old, sizeof(old), "%s/_hyper/profiles/deleted_p%d_%lu", s_drv, slot, (unsigned long)time(NULL));
    if (rename(dir, old) != 0) {
        // could not move the folder: at least make the profile disappear
        char p[MAX_PATH_LEN];
        profiles_file(slot, "profile.bin", p, sizeof(p));
        unlink(p);
    }
    s_have[slot] = false;
    if (s_glob.last == slot) { s_glob.last = (uint8_t)profiles_slot(0); profiles_global_save(); }
    if (s_active == slot) s_active = -1;
    return true;
}

void profiles_activate(int slot) {
    if (!profiles_exists(slot)) slot = profiles_slot(0);
    s_active = slot;
    stats_set_profile(slot);
}

int profiles_active(void) { return s_active; }

ProfilesGlobal *profiles_global(void) { return &s_glob; }

bool profiles_has_pin(int slot) { return profiles_exists(slot) && s_info[slot].pin_len > 0; }

bool profiles_check_pin(int slot, const uint8_t *pin, int len) {
    if (!profiles_has_pin(slot)) return true;
    const ProfileInfo *p = &s_info[slot];
    return len == p->pin_len && memcmp(pin, p->pin, (size_t)len) == 0;
}

void profiles_set_pin(int slot, const uint8_t *pin, int len) {
    if (!profiles_exists(slot)) return;
    ProfileInfo t = s_info[slot];
    if (len < 0) len = 0;
    if (len > PIN_MAX) len = PIN_MAX;
    memset(t.pin, 0, sizeof(t.pin));
    if (len) memcpy(t.pin, pin, (size_t)len);
    t.pin_len = (uint8_t)len;
    profiles_update(slot, &t);
}

void profiles_note_launch(int slot, const char *title) {
    if (!profiles_exists(slot)) return;
    ProfileInfo t = s_info[slot];
    snprintf(t.last_title, sizeof(t.last_title), "%s", title ? title : "");
    t.last_ts = (uint32_t)time(NULL);
    profiles_update(slot, &t);
}

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) & 0x1F) | ((((g) >> 3) & 0x1F) << 5) | ((((b) >> 3) & 0x1F) << 10) | 0x8000))

void profile_color(int idx, uint16_t *accent, uint16_t *hi, uint16_t *sel_light, uint16_t *sel_dark) {
    static const uint16_t pal[PROFILE_COLORS][4] = {
        { RGB(0, 195, 227),   RGB(120, 236, 255), RGB(0, 140, 210),  RGB(25, 210, 245) },   // blue (DSplay)
        { RGB(235, 70, 70),   RGB(255, 150, 150), RGB(200, 40, 40),  RGB(255, 120, 120) },  // red
        { RGB(255, 140, 30),  RGB(255, 200, 120), RGB(205, 100, 0),  RGB(255, 170, 80) },   // orange
        { RGB(232, 182, 0),   RGB(255, 225, 110), RGB(160, 120, 0),  RGB(255, 215, 60) },   // yellow
        { RGB(40, 190, 100),  RGB(140, 235, 170), RGB(20, 140, 70),  RGB(90, 220, 140) },   // green
        { RGB(140, 90, 230),  RGB(200, 170, 255), RGB(110, 60, 200), RGB(185, 150, 255) },  // purple
        { RGB(240, 80, 160),  RGB(255, 170, 215), RGB(200, 40, 120), RGB(255, 130, 190) },  // pink
        { RGB(70, 100, 235),  RGB(160, 180, 255), RGB(50, 80, 210),  RGB(140, 165, 255) },  // navy
    };
    if (idx < 0 || idx >= PROFILE_COLORS) idx = 0;
    if (accent) *accent = pal[idx][0];
    if (hi) *hi = pal[idx][1];
    if (sel_light) *sel_light = pal[idx][2];
    if (sel_dark) *sel_dark = pal[idx][3];
}

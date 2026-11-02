// DSi applications added to DSplay (see include/apps.h)
#include "apps.h"
#include "launcher.h"
#include "romdb.h"
#include "stats.h"
#include "i18n.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef ARM9
#include <nds.h>
#endif

#define APPS_MAGIC   0x50504148u   // "HAPP"
#define APPS_VERSION 1

static GameItem s_apps[APPS_MAX];
static int      s_n;

// Title folders that hold applications with a menu icon
static const char *const k_groups[] = {
    "00030004",   // DSiWare (Flipnote Studio, DSi Browser, downloaded games...)
    "00030005",   // built-in applications (DSi Camera, DSi Sound, PictoChat, Download Play)
    "00030015",   // system applications (System Settings, DSi Shop)
};

bool apps_available(void) {
#ifdef ARM9
    return systemIsTwlMode();
#else
    return true;
#endif
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// banner language for the menu language: JP, EN, FR, DE, IT, ES
static int banner_lang(uint8_t lang) {
    switch (lang) {
        case LANG_FR: return 2;
        case LANG_DE: return 3;
        case LANG_ES: return 5;
        default: return 1;
    }
}

static bool read_app(const char *title_dir, GameItem *it, uint8_t lang) {
    char app[MAX_PATH_LEN];
    if (!launcher_title_app(title_dir, app, sizeof(app))) return false;
    FILE *f = fopen(app, "rb");
    if (!f) return false;
    static uint8_t header[0x200];
    static uint8_t banner[0x840];
    size_t blen = 0;
    bool ok = fread(header, 1, sizeof(header), f) == sizeof(header);
    if (ok) {
        uint32_t boff = rd32(&header[0x68]);
        if (boff >= 0x200 && fseek(f, (long)boff, SEEK_SET) == 0) blen = fread(banner, 1, sizeof(banner), f);
    }
    fclose(f);
    if (!ok || blen < 0x240 + 0x100 * 6) return false;            // no banner: not a menu application
    memset(it, 0, sizeof(*it));
    snprintf(it->path, sizeof(it->path), "%s", title_dir);
    const char *tid = strstr(title_dir, "/title/");
    snprintf(it->filename, sizeof(it->filename), "app-%s", tid ? tid + 7 : title_dir);
    for (char *p = it->filename; *p; p++) if (*p == '/') *p = '-';
    romdb_parse_nds_buffers(it, header, banner, blen);
    // the title in the menu language when the banner has it
    uint16_t utf16[128];
    memcpy(utf16, banner + 0x240 + banner_lang(lang) * 0x100, sizeof(utf16));
    char title[MAX_DISPLAY_TITLE], sub[MAX_TITLE_LEN];
    romdb_banner_to_utf8(utf16, title, sizeof(title), sub, sizeof(sub));
    if (strlen(title) >= 2) {
        snprintf(it->title, sizeof(it->title), "%s", title);
        snprintf(it->subtitle, sizeof(it->subtitle), "%s", sub);
    }
    it->type = TYPE_APP;
    it->flags = ITEM_FLAG_DSI;
    return it->title[0] != 0;
}

static int cmp_title(const void *a, const void *b) {
    return strcasecmp(((const GameItem *)a)->title, ((const GameItem *)b)->title);
}

int apps_scan(GameItem *out, int max, uint8_t lang) {
    char root[MAX_PATH_LEN];
    if (!launcher_nand_root(root, sizeof(root))) return -1;
    int n = 0;
    for (size_t g = 0; g < sizeof(k_groups) / sizeof(k_groups[0]) && n < max; g++) {
        char dir[MAX_PATH_LEN];
        snprintf(dir, sizeof(dir), "%s/title/%s", root, k_groups[g]);
        DIR *d = opendir(dir);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && n < max) {
            if (e->d_name[0] == '.' || strlen(e->d_name) != 8) continue;
            char td[MAX_PATH_LEN];
            snprintf(td, sizeof(td), "%s/%s", dir, e->d_name);
            for (char *p = td + strlen(dir) + 1; *p; p++) if (*p >= 'A' && *p <= 'F') *p = (char)(*p + 32);
            if (read_app(td, &out[n], lang)) n++;
        }
        closedir(d);
    }
    qsort(out, n, sizeof(GameItem), cmp_title);
    return n;
}

static void file_path(char *out, size_t len) { snprintf(out, len, "%s/_hyper/apps.bin", get_drive_prefix()); }

void apps_load(void) {
    s_n = 0;
    char p[MAX_PATH_LEN];
    file_path(p, sizeof(p));
    FILE *f = fopen(p, "rb");
    if (!f) return;
    uint32_t h[4];
    if (fread(h, 4, 4, f) == 4 && h[0] == APPS_MAGIC && h[1] == APPS_VERSION && h[3] == sizeof(GameItem) && h[2] <= APPS_MAX)
        s_n = (int)fread(s_apps, sizeof(GameItem), h[2], f);
    fclose(f);
}

bool apps_save(void) {
    char p[MAX_PATH_LEN], tmp[MAX_PATH_LEN];
    file_path(p, sizeof(p));
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    uint32_t h[4] = { APPS_MAGIC, APPS_VERSION, (uint32_t)s_n, (uint32_t)sizeof(GameItem) };
    bool ok = fwrite(h, 4, 4, f) == 4 && (s_n == 0 || fwrite(s_apps, sizeof(GameItem), (size_t)s_n, f) == (size_t)s_n);
    fclose(f);
    if (!ok) { remove(tmp); return false; }
    remove(p);
    return rename(tmp, p) == 0;
}

int apps_count(void) { return s_n; }
const GameItem *apps_get(int i) { return (i >= 0 && i < s_n) ? &s_apps[i] : NULL; }

bool apps_has(const char *path) {
    for (int i = 0; i < s_n; i++) if (strcasecmp(s_apps[i].path, path) == 0) return true;
    return false;
}

bool apps_add(const GameItem *it) {
    if (!it || it->type != TYPE_APP || apps_has(it->path) || s_n >= APPS_MAX) return false;
    s_apps[s_n++] = *it;
    return true;
}

bool apps_remove(const char *path) {
    for (int i = 0; i < s_n; i++) {
        if (strcasecmp(s_apps[i].path, path) != 0) continue;
        memmove(&s_apps[i], &s_apps[i + 1], sizeof(GameItem) * (size_t)(s_n - i - 1));
        s_n--;
        return true;
    }
    return false;
}

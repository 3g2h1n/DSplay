#include "library.h"
#include "gfx.h"
#include "romdb.h"
#include "apps.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef ARM9
#include <nds.h>
#endif

#define LIB_MAGIC   0x42494C48 // "HLIB"
#define LIB_VERSION 3

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t item_size;
} LibHeader;

static GameItem *s_items = NULL;
static int       s_count = 0;
static char      s_drv[128] = "";

// Sort keys (folded titles), rebuilt lazily
static char    (*s_keys)[48] = NULL;
static bool      s_keys_valid = false;
static uint32_t  s_generation = 0;

uint32_t library_generation(void) { return s_generation; }
int library_capacity(void);

bool library_init(void) {
    if (!s_items) s_items = (GameItem *)malloc(sizeof(GameItem) * (size_t)library_capacity());
    if (!s_keys) s_keys = malloc(sizeof(*s_keys) * (size_t)library_capacity());
    s_count = 0;
    s_keys_valid = false;
    return s_items && s_keys;
}

int library_count(void) { return s_count; }

GameItem *library_get(int idx) {
    return (idx >= 0 && idx < s_count) ? &s_items[idx] : NULL;
}

int library_find_path(const char *path) {
    for (int i = 0; i < s_count; i++) {
        if (strcasecmp(s_items[i].path, path) == 0) return i;
    }
    return -1;
}

// --------------------------------------------------------------------------
// Folding: lower-case ASCII with Turkish / Latin accents removed
// --------------------------------------------------------------------------
static uint32_t next_cp(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    uint32_t cp = s[0];
    int n = 1;
    if (cp >= 0xF0 && s[1] && s[2] && s[3]) { cp = ((cp & 7) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); n = 4; }
    else if (cp >= 0xE0 && s[1] && s[2]) { cp = ((cp & 0xF) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); n = 3; }
    else if (cp >= 0xC0 && s[1]) { cp = ((cp & 0x1F) << 6) | (s[1] & 0x3F); n = 2; }
    *ps += n;
    return cp;
}

static char fold_cp(uint32_t cp) {
    if (cp < 0x80) {
        char c = (char)cp;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        return c;
    }
    switch (cp) {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5:
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';
        case 0xC7: case 0xE7: return 'c';
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: case 0xEC: case 0xED: case 0xEE: case 0xEF:
        case 0x130: case 0x131: return 'i';
        case 0xD1: case 0xF1: return 'n';
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
        case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
        case 0x11E: case 0x11F: return 'g';
        case 0x15E: case 0x15F: return 's';
        case 0xDF: return 's';
        default: return 0;
    }
}

void library_fold(const char *utf8, char *out, size_t out_len) {
    size_t o = 0;
    if (!out_len) return;
    while (utf8 && *utf8 && o + 1 < out_len) {
        char c = fold_cp(next_cp(&utf8));
        if (c) out[o++] = c;
    }
    out[o] = '\0';
}

static void build_keys(void) {
    if (s_keys_valid) return;
    for (int i = 0; i < s_count; i++) {
        const char *t = s_items[i].title;
        // ignore a leading article for sorting ("The Legend of Zelda" -> "legend of zelda")
        if (strncasecmp(t, "the ", 4) == 0) t += 4;
        library_fold(t, s_keys[i], sizeof(s_keys[i]));
    }
    s_keys_valid = true;
}

// --------------------------------------------------------------------------
// Cache file
// --------------------------------------------------------------------------
static void cache_path(char *out, size_t len) {
    snprintf(out, len, "%s/_hyper/library.bin", s_drv);
}

static int cache_load(void) {
    char path[MAX_PATH_LEN];
    cache_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    LibHeader h;
    int n = 0;
    if (fread(&h, sizeof(h), 1, f) == 1 && h.magic == LIB_MAGIC && h.version == LIB_VERSION &&
        h.item_size == sizeof(GameItem) && h.count <= (uint32_t)library_capacity()) {
        n = (int)fread(s_items, sizeof(GameItem), h.count, f);
    }
    fclose(f);
    // titles the font cannot draw (cached by an older version) are read again by the next scan
    int w = 0;
    for (int i = 0; i < n; i++) if (s_items[i].type != TYPE_NDS || gfx_text_renderable(s_items[i].title)) s_items[w++] = s_items[i];
    n = w;
    // cached paths may use another drive prefix ("fat:" vs "sd:")
    for (int i = 0; i < n && strchr(s_drv, ':'); i++) {
        char tmp[MAX_PATH_LEN];
        snprintf(tmp, sizeof(tmp), "%s%s", s_drv, path_without_drive(s_items[i].path));
        memcpy(s_items[i].path, tmp, sizeof(tmp));
    }
    return n;
}

static void cache_save(void) {
    char path[MAX_PATH_LEN];
    cache_path(path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int n = 0;
    for (int i = 0; i < s_count; i++) n += s_items[i].type != TYPE_APP;     // DSi applications live in apps.bin
    LibHeader h = { LIB_MAGIC, LIB_VERSION, (uint32_t)n, (uint32_t)sizeof(GameItem) };
    fwrite(&h, sizeof(h), 1, f);
    for (int i = 0; i < s_count; i++) if (s_items[i].type != TYPE_APP) fwrite(&s_items[i], sizeof(GameItem), 1, f);
    fclose(f);
}

static int compare_type_title(const void *a, const void *b);

// The DSi applications the user added (apps.bin) are part of the library too
void library_merge_apps(void) {
    if (!s_items) return;
    int w = 0;
    for (int i = 0; i < s_count; i++) if (s_items[i].type != TYPE_APP) { if (w != i) s_items[w] = s_items[i]; w++; }
    s_count = w;
    int cap = library_capacity();
    for (int i = 0; apps_available() && i < apps_count() && s_count < cap; i++) s_items[s_count++] = *apps_get(i);
    qsort(s_items, s_count, sizeof(GameItem), compare_type_title);
    s_keys_valid = false;
    library_refresh_flags();
}

// --------------------------------------------------------------------------
// Incremental directory scan
//
// The HOME Menu appears straight from the cache; the card is then walked a few
// directory entries per frame (library_scan_step) and new titles are read one or
// two per frame, so the menu stays responsive while the library updates.
// --------------------------------------------------------------------------
static bool skip_dir(const char *name) {
    if (name[0] == '.' || name[0] == '_') return true;
    return strcasecmp(name, "saves") == 0 || strcasecmp(name, "System Volume Information") == 0 ||
           strcasecmp(name, "Nintendo 3DS") == 0 || strcasecmp(name, "luma") == 0 ||
           strcasecmp(name, "title") == 0 || strcasecmp(name, "ticket") == 0 ||
           strcasecmp(name, "private") == 0 || strcasecmp(name, "shared1") == 0 || strcasecmp(name, "sys") == 0 ||
           strcasecmp(name, "photo") == 0 || strcasecmp(name, "DCIM") == 0 || strcasecmp(name, "gm9") == 0 ||
           strcasecmp(name, "$RECYCLE.BIN") == 0;
}

static bool skip_root_file(const char *name) {
    return strcasecmp(name, "BOOT.NDS") == 0 || strcasecmp(name, "HyperLauncher.nds") == 0 ||
           strcasecmp(name, "ntrboot.nds") == 0 || strcasecmp(name, "hiya.dsi") == 0 ||
           strcasecmp(name, "boot.firm") == 0 || strcasecmp(name, "bios.bin") == 0;
}

// Simple open-addressing hash of library paths for fast reconciliation
static uint32_t hash_path(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) {
        char c = *s++;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        h = (h ^ (uint8_t)c) * 16777619u;
    }
    return h;
}

static int compare_type_title(const void *a, const void *b) {
    const GameItem *x = (const GameItem *)a, *y = (const GameItem *)b;
    return strcasecmp(x->path, y->path);
}

#define SCAN_MAX_DEPTH 5

typedef struct {
    DIR  *d;
    char  path[MAX_PATH_LEN];
    int   depth;
} ScanFrame;

typedef enum { SCAN_IDLE = 0, SCAN_WALK, SCAN_ADD, SCAN_DONE } ScanPhase;

static struct {
    ScanPhase phase;
    bool      full;
    int       root;                 // 0 = /roms, 1 = /games, 2 = card root (not recursive), 3 = finished
    ScanFrame stack[SCAN_MAX_DEPTH + 1];
    int       sp;
    char    (*found)[MAX_PATH_LEN]; // every title path seen on the card
    int       found_n;
    int      *table;                // hash of found paths
    int       hsize;
    bool     *matched;
    int       add_i;                // next found path to check in SCAN_ADD
    GameItem *fresh;                // new titles read during SCAN_ADD
    int       fresh_n, fresh_cap;
    bool      truncated;
    LibraryProgressFn progress;
} S;

static int s_capacity = 0;

int library_capacity(void) {
    if (!s_capacity) {
#ifdef ARM9
        s_capacity = isDSiMode() ? MAX_LIBRARY_ITEMS : 1024;
#else
        s_capacity = MAX_LIBRARY_ITEMS;
#endif
    }
    return s_capacity;
}

bool library_truncated(void) { return S.truncated; }
bool library_scanning(void) { return S.phase == SCAN_WALK || S.phase == SCAN_ADD; }

static void scan_free(void) {
    while (S.sp > 0) { closedir(S.stack[S.sp - 1].d); S.sp--; }
    free(S.found); S.found = NULL;
    free(S.table); S.table = NULL;
    free(S.matched); S.matched = NULL;
    free(S.fresh); S.fresh = NULL;
    S.found_n = S.fresh_n = S.fresh_cap = 0;
}

static bool scan_push(const char *path, int depth) {
    if (S.sp > SCAN_MAX_DEPTH) return false;
    DIR *d = opendir(path);
    if (!d) return false;
    ScanFrame *f = &S.stack[S.sp++];
    f->d = d;
    f->depth = depth;
    snprintf(f->path, sizeof(f->path), "%s", path);
    return true;
}

void library_load_cache(const char *drv) {
    snprintf(s_drv, sizeof(s_drv), "%s", drv ? drv : "");
    if (!s_items && !library_init()) return;
    s_count = cache_load();
    s_keys_valid = false;
    library_merge_apps();
}

bool library_scan_begin(const char *drv, bool full, LibraryProgressFn progress) {
    snprintf(s_drv, sizeof(s_drv), "%s", drv ? drv : "");
    if (!s_items && !library_init()) return false;
    scan_free();
    memset(&S.stack, 0, sizeof(S.stack));
    S.full = full;
    S.progress = progress;
    S.truncated = false;
    S.found = malloc(sizeof(*S.found) * (size_t)library_capacity());
    if (!S.found) { S.phase = SCAN_IDLE; return false; }
    S.root = 0;
    S.phase = SCAN_WALK;
    return true;
}

// one directory entry; returns false when the walk is complete
static bool walk_one(void) {
    while (S.sp == 0) {
        if (S.root >= 3) return false;
        char root[MAX_PATH_LEN];
        if (S.root == 0) snprintf(root, sizeof(root), "%s/roms", s_drv);
        else if (S.root == 1) snprintf(root, sizeof(root), "%s/games", s_drv);
        else snprintf(root, sizeof(root), "%s/", s_drv);
        scan_push(root, S.root == 2 ? 0 : 1);
        S.root++;
    }
    ScanFrame *f = &S.stack[S.sp - 1];
    struct dirent *e = readdir(f->d);
    if (!e) {
        closedir(f->d);
        S.sp--;
        return true;
    }
    const char *name = e->d_name;
    if (name[0] == '.') return true;
    bool recursive = f->depth > 0;
    char full[MAX_PATH_LEN];
    size_t dl = strlen(f->path);
    const char *sep = (dl && f->path[dl - 1] == '/') ? "" : "/";
    if ((size_t)snprintf(full, sizeof(full), "%s%s%s", f->path, sep, name) >= sizeof(full)) return true;
    bool is_dir;
#ifdef DT_DIR
    if (e->d_type == DT_DIR) is_dir = true;
    else if (e->d_type == DT_REG) is_dir = false;
    else
#endif
    {
        struct stat st;
        if (stat(full, &st) != 0) return true;
        is_dir = S_ISDIR(st.st_mode);
    }
    if (is_dir) {
        if (recursive && f->depth < 4 && !skip_dir(name)) scan_push(full, f->depth + 1);
        return true;
    }
    if (!recursive && skip_root_file(name)) return true;
    if (romdb_type_from_name(name) == TYPE_UNKNOWN) return true;
    if (strcasecmp(name, "usrcheat.dat") == 0) return true;
    if (S.found_n >= library_capacity()) { S.truncated = true; return true; }
    memcpy(S.found[S.found_n++], full, MAX_PATH_LEN);
    if (S.progress && (S.found_n % 32) == 0) S.progress(S.found_n, NULL);
    return true;
}

static bool found_lookup(const char *path) {
    if (!S.table) return false;
    uint32_t h = hash_path(path) & (uint32_t)(S.hsize - 1);
    while (S.table[h] >= 0) {
        if (strcasecmp(S.found[S.table[h]], path) == 0) return true;
        h = (h + 1) & (uint32_t)(S.hsize - 1);
    }
    return false;
}

static bool cached_lookup(const char *path, int *table, int hsize) {
    uint32_t h = hash_path(path) & (uint32_t)(hsize - 1);
    while (table[h] >= 0) {
        if (strcasecmp(s_items[table[h]].path, path) == 0) return true;
        h = (h + 1) & (uint32_t)(hsize - 1);
    }
    return false;
}

static void begin_add_phase(void) {
    // hash of found paths (used at the end to drop titles that disappeared)
    S.hsize = 1;
    while (S.hsize < S.found_n * 2 + 1) S.hsize <<= 1;
    S.table = malloc(sizeof(int) * (size_t)S.hsize);
    S.matched = calloc((size_t)S.found_n + 1, sizeof(bool));
    if (!S.table || !S.matched) { S.phase = SCAN_DONE; return; }
    for (int i = 0; i < S.hsize; i++) S.table[i] = -1;
    for (int i = 0; i < S.found_n; i++) {
        uint32_t h = hash_path(S.found[i]) & (uint32_t)(S.hsize - 1);
        while (S.table[h] >= 0) h = (h + 1) & (uint32_t)(S.hsize - 1);
        S.table[h] = i;
    }
    // mark found paths that are already in the library
    if (!S.full && s_count > 0) {
        int csize = 1;
        while (csize < s_count * 2 + 1) csize <<= 1;
        int *ct = malloc(sizeof(int) * (size_t)csize);
        if (ct) {
            for (int i = 0; i < csize; i++) ct[i] = -1;
            for (int i = 0; i < s_count; i++) {
                uint32_t h = hash_path(s_items[i].path) & (uint32_t)(csize - 1);
                while (ct[h] >= 0) h = (h + 1) & (uint32_t)(csize - 1);
                ct[h] = i;
            }
            for (int i = 0; i < S.found_n; i++) S.matched[i] = cached_lookup(S.found[i], ct, csize);
            free(ct);
        }
    }
    S.add_i = 0;
    S.phase = SCAN_ADD;
}

// reads one new title; returns false when there is nothing left to add
static bool add_one(void) {
    while (S.add_i < S.found_n && S.matched[S.add_i]) S.add_i++;
    if (S.add_i >= S.found_n) return false;
    const char *p = S.found[S.add_i++];
    if (S.fresh_n >= S.fresh_cap) {
        int ncap = S.fresh_cap ? S.fresh_cap * 2 : 32;
        GameItem *n = realloc(S.fresh, sizeof(GameItem) * (size_t)ncap);
        if (!n) return false;
        S.fresh = n;
        S.fresh_cap = ncap;
    }
    const char *name = strrchr(p, '/');
    name = name ? name + 1 : p;
    struct stat st;
    if (stat(p, &st) != 0) return true; // deleted meanwhile
    if (romdb_fill_item(&S.fresh[S.fresh_n], p, name, (uint32_t)st.st_size, (uint32_t)st.st_mtime)) {
        S.fresh_n++;
        if (S.progress && (S.fresh_n % 8) == 0) S.progress(s_count + S.fresh_n, name);
    }
    return true;
}

// merges the scan result into the library; returns true when the library changed
static bool finish_scan(void) {
    bool changed = S.full;
    int cap = library_capacity();
    if (S.full) {
        s_count = 0;
    } else if (S.table) {
        int w = 0;
        for (int i = 0; i < s_count; i++) {
            if (s_items[i].type == TYPE_APP) continue;              // merged again below
            if (found_lookup(s_items[i].path)) {
                if (w != i) s_items[w] = s_items[i];
                w++;
            } else {
                changed = true;
            }
        }
        s_count = w;
    }
    for (int i = 0; i < S.fresh_n && s_count < cap; i++) {
        s_items[s_count++] = S.fresh[i];
        changed = true;
    }
    if (changed) {
        qsort(s_items, s_count, sizeof(GameItem), compare_type_title);
        s_keys_valid = false;
        cache_save();
    }
    library_merge_apps();
    return changed;
}

LibraryScanResult library_scan_step(int walk_budget, int add_budget) {
    if (S.phase == SCAN_WALK) {
        for (int i = 0; i < walk_budget; i++) {
            if (!walk_one()) { begin_add_phase(); break; }
        }
        return LIBRARY_SCAN_BUSY;
    }
    if (S.phase == SCAN_ADD) {
        for (int i = 0; i < add_budget; i++) {
            if (!add_one()) { S.phase = SCAN_DONE; break; }
        }
        if (S.phase != SCAN_DONE) return LIBRARY_SCAN_BUSY;
    }
    if (S.phase == SCAN_DONE) {
        bool changed = finish_scan();
        scan_free();
        S.phase = SCAN_IDLE;
        return changed ? LIBRARY_SCAN_CHANGED : LIBRARY_SCAN_UNCHANGED;
    }
    return LIBRARY_SCAN_UNCHANGED;
}

int library_sync(const char *drv, bool full, LibraryProgressFn progress) {
    if (!full) library_load_cache(drv);
    else { snprintf(s_drv, sizeof(s_drv), "%s", drv ? drv : ""); if (!s_items && !library_init()) return 0; }
    if (!library_scan_begin(drv, full, progress)) return s_count;
    while (library_scan_step(256, 64) == LIBRARY_SCAN_BUSY) {}
    library_refresh_flags();
    return s_count;
}

void library_refresh_flags(void) {
    s_generation++;
    for (int i = 0; i < s_count; i++) {
        uint32_t f = s_items[i].flags & ~(ITEM_FLAG_FAVORITE | ITEM_FLAG_HIDDEN);
        if (pathset_is_favorite(s_items[i].path)) f |= ITEM_FLAG_FAVORITE;
        if (pathset_is_hidden(s_items[i].path)) f |= ITEM_FLAG_HIDDEN;
        s_items[i].flags = f;
    }
}

void library_remove(int idx) {
    if (idx < 0 || idx >= s_count) return;
    for (int i = idx; i < s_count - 1; i++) s_items[i] = s_items[i + 1];
    s_count--;
    s_keys_valid = false;
    s_generation++;
    cache_save();
}

// --------------------------------------------------------------------------
// Views
// --------------------------------------------------------------------------
static const PlaytimeDB *s_sort_pt;
static SortMode          s_sort_mode;

static int cmp_alpha(int a, int b) {
    int r = strcmp(s_keys[a], s_keys[b]);
    if (r) return r;
    return strcasecmp(s_items[a].path, s_items[b].path);
}

static int cmp_view(const void *pa, const void *pb) {
    int a = *(const int *)pa, b = *(const int *)pb;
    if (s_sort_mode == SORT_ALPHA || !s_sort_pt) return cmp_alpha(a, b);
    const PlaytimeEntry *ea = playtime_get(s_sort_pt, s_items[a].filename);
    const PlaytimeEntry *eb = playtime_get(s_sort_pt, s_items[b].filename);
    uint32_t va, vb;
    if (s_sort_mode == SORT_PLAYTIME) {
        va = ea ? ea->total_minutes : 0;
        vb = eb ? eb->total_minutes : 0;
    } else {
        va = ea ? ea->last_played_ts : 0;
        vb = eb ? eb->last_played_ts : 0;
    }
    if (va != vb) return (vb > va) ? 1 : -1;
    // never played: favorites first, then newest files
    bool fa = (s_items[a].flags & ITEM_FLAG_FAVORITE) != 0, fb = (s_items[b].flags & ITEM_FLAG_FAVORITE) != 0;
    if (fa != fb) return fa ? -1 : 1;
    return cmp_alpha(a, b);
}

int library_view(int *out, int max, ItemType filter, SortMode sort, bool include_hidden, const PlaytimeDB *pt) {
    build_keys();
    int n = 0;
    for (int i = 0; i < s_count && n < max; i++) {
        const GameItem *it = &s_items[i];
        if (!include_hidden && (it->flags & ITEM_FLAG_HIDDEN)) continue;
        if (filter == TYPE_FOLDER) {
            if (!(it->flags & ITEM_FLAG_FAVORITE)) continue;
        } else if (filter != TYPE_UNKNOWN) {
            ItemType t = (ItemType)it->type;
            if (filter == TYPE_GB && t == TYPE_GBC) t = TYPE_GB; // GB tab shows GB + GBC
            if (t != filter) continue;
        }
        out[n++] = i;
    }
    s_sort_pt = pt;
    s_sort_mode = sort;
    qsort(out, n, sizeof(int), cmp_view);
    return n;
}

int library_systems(ItemType *out, int max, bool include_hidden) {
    static const ItemType order[] = { TYPE_NDS, TYPE_GBA, TYPE_GB, TYPE_NES, TYPE_SNES, TYPE_GENESIS, TYPE_SMS, TYPE_GG };
    int n = 0;
    for (size_t k = 0; k < sizeof(order) / sizeof(order[0]) && n < max; k++) {
        for (int i = 0; i < s_count; i++) {
            if (!include_hidden && (s_items[i].flags & ITEM_FLAG_HIDDEN)) continue;
            ItemType t = (ItemType)s_items[i].type;
            if (t == TYPE_GBC) t = TYPE_GB;
            if (t == order[k]) { out[n++] = order[k]; break; }
        }
    }
    return n;
}

int library_search(const char *query, int *out, int max, bool include_hidden) {
    char q[48];
    library_fold(query, q, sizeof(q));
    if (!q[0]) return 0;
    build_keys();
    int n = 0;
    // pass 0: title starts with query, pass 1: contains
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < s_count && n < max; i++) {
            if (!include_hidden && (s_items[i].flags & ITEM_FLAG_HIDDEN)) continue;
            char t[MAX_DISPLAY_TITLE];
            library_fold(s_items[i].title, t, sizeof(t));
            bool starts = strncmp(t, q, strlen(q)) == 0;
            bool hit = starts;
            if (!hit && pass == 1) {
                hit = strstr(t, q) != NULL;
                if (!hit) {
                    char fn[MAX_TITLE_LEN];
                    library_fold(s_items[i].filename, fn, sizeof(fn));
                    hit = strstr(fn, q) != NULL;
                }
            }
            if (pass == 0 ? starts : (hit && !starts)) out[n++] = i;
        }
    }
    return n;
}

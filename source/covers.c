#include "covers.h"
#include "upng.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef ARM9
#include <nds.h>
#endif

#define MAX_INDEX   8192       // a full TWiLight box art pack holds several thousand files
#define SCAN_PER_FRAME 48      // directory entries read per frame (the scan never blocks the start)
#define COST_PACK   1          // per-frame budget units: a pre-made cover is one read,
#define COST_DECODE 4          // a PNG/BMP is decoded and scaled
#define FRAME_BUDGET 4

typedef struct {
    uint32_t hash;
    uint8_t  dir;
} IndexEntry;

static const char *s_dirs[] = { "/_hyper/covers", "/_nds/TWiLightMenu/boxart", "/_hyper/boxart" };
#define NUM_DIRS (int)(sizeof(s_dirs) / sizeof(s_dirs[0]))

static char       s_drv[128];
static IndexEntry *s_index = NULL;
static int        s_index_count = 0;

typedef struct {
    int      lib_idx;
    uint32_t stamp;
    Color    px[COVER_SIZE * COVER_SIZE];
} Slot;

static Slot    *s_slots = NULL;
static int      s_slot_n = 0;
static uint32_t s_stamp = 1;
static uint8_t  s_state[MAX_LIBRARY_ITEMS]; // 0 = unknown, 1 = none, 2 = failed
static int      s_budget = FRAME_BUDGET;
static uint32_t s_generation = 0;
static bool     s_enabled = false;

// Background scan of the cover folders (a TWiLight box art pack can hold thousands of files)
static int      s_scan_dir = 0;             // NUM_DIRS = finished
static DIR     *s_scan_h = NULL;
static bool     s_scan_done = true;

// Pre-made covers from DSplay Studio: <drv>/_hyper/covers/_covers.pak (see covers.h)
typedef struct {
    uint32_t hash;
    uint8_t  dir, pad[3];
    uint32_t src_size, src_mtime;
    uint32_t offset;
} PackEntry;
static PackEntry *s_pe = NULL;
static int        s_pe_n = 0;
static FILE      *s_pack = NULL;

uint32_t covers_generation(void) { return s_generation; }

static uint32_t hash_name(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) {
        char c = *s++;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        h = (h ^ (uint8_t)c) * 16777619u;
    }
    return h;
}

static int cmp_entry(const void *a, const void *b) {
    uint32_t x = ((const IndexEntry *)a)->hash, y = ((const IndexEntry *)b)->hash;
    return (x > y) - (x < y);
}

static void pack_close(void) {
    if (s_pack) fclose(s_pack);
    s_pack = NULL;
    free(s_pe);
    s_pe = NULL;
    s_pe_n = 0;
}

static void pack_open(void) {
    pack_close();
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/_hyper/covers/" COVERS_PACK_NAME, s_drv);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint32_t h[4];
    if (fread(h, 4, 4, f) == 4 && h[0] == COVERS_PACK_MAGIC && h[1] == COVERS_PACK_VERSION && h[2] <= MAX_INDEX && h[3] == sizeof(PackEntry)) {
        s_pe = malloc(sizeof(PackEntry) * (h[2] ? h[2] : 1));
        if (s_pe && fread(s_pe, sizeof(PackEntry), h[2], f) == h[2]) {
            s_pe_n = (int)h[2];
            s_pack = f;
            return;
        }
        free(s_pe);
        s_pe = NULL;
    }
    fclose(f);
}

static void scan_stop(void) {
    if (s_scan_h) closedir(s_scan_h);
    s_scan_h = NULL;
}

static void scan_step(int budget) {
    while (!s_scan_done && budget > 0) {
        if (!s_scan_h) {
            if (s_scan_dir >= NUM_DIRS) {
                qsort(s_index, s_index_count, sizeof(IndexEntry), cmp_entry);
                s_scan_done = true;
                s_generation++;              // tiles waiting for the scan try again
                return;
            }
            char path[MAX_PATH_LEN];
            snprintf(path, sizeof(path), "%s%s", s_drv, s_dirs[s_scan_dir]);
            s_scan_h = opendir(path);
            if (!s_scan_h) { s_scan_dir++; continue; }
        }
        struct dirent *e = readdir(s_scan_h);
        budget--;
        if (!e || s_index_count >= MAX_INDEX) { scan_stop(); s_scan_dir++; continue; }
        if (e->d_name[0] == '.' || e->d_name[0] == '_') continue;
        s_index[s_index_count].hash = hash_name(e->d_name);
        s_index[s_index_count].dir = (uint8_t)s_scan_dir;
        s_index_count++;
    }
}

static int slot_count(void) {
#ifdef ARM9
    return isDSiMode() ? 64 : 24;       // 18 KB each: 1.2 MB of the DSi's 16 MB, less in DS mode
#else
    return 64;
#endif
}

void covers_init(const char *drv) {
    snprintf(s_drv, sizeof(s_drv), "%s", drv ? drv : "");
    if (!s_index) s_index = malloc(sizeof(IndexEntry) * MAX_INDEX);
    if (!s_slots) {
        s_slot_n = slot_count();
        s_slots = calloc((size_t)s_slot_n, sizeof(Slot));
        if (!s_slots) { s_slot_n = 12; s_slots = calloc((size_t)s_slot_n, sizeof(Slot)); }
        if (!s_slots) s_slot_n = 0;
    }
    s_index_count = 0;
    scan_stop();
    s_enabled = s_index && s_slots;
    s_scan_dir = 0;
    s_scan_done = !s_enabled;
    if (s_enabled) pack_open();
    covers_reset();
}

// A cover file was just written to _hyper/covers (Photos > Set as game cover)
static int index_find(const char *name);
void covers_add_file(const char *name) {
    if (!s_index || !s_enabled || !name) return;
    bool known = s_scan_done && index_find(name) == 0;
    if (!known && s_index_count < MAX_INDEX) {
        s_index[s_index_count].hash = hash_name(name);
        s_index[s_index_count].dir = 0;
        s_index_count++;
        if (s_scan_done) qsort(s_index, s_index_count, sizeof(IndexEntry), cmp_entry);
    }
    covers_reset();
    s_generation++;
}

void covers_disable(void) {
    s_enabled = false;
    scan_stop();
    s_scan_done = true;
    s_index_count = 0;
    pack_close();
    covers_reset();
}

void covers_reset(void) {
    memset(s_state, 0, sizeof(s_state));
    if (s_slots) for (int i = 0; i < s_slot_n; i++) { s_slots[i].lib_idx = -1; s_slots[i].stamp = 0; }
    if (s_enabled) pack_open();               // Studio may have sent a new pack
}

int covers_indexed_count(void) { return s_index_count > s_pe_n ? s_index_count : s_pe_n; }

void covers_frame_begin(void) {
    s_budget = FRAME_BUDGET;
    if (!s_scan_done) scan_step(SCAN_PER_FRAME);
}

bool covers_scan_done(void) { return s_scan_done; }

bool covers_known_missing(int lib_idx) {
    return lib_idx >= 0 && lib_idx < MAX_LIBRARY_ITEMS && s_state[lib_idx] != 0;
}

static int pack_find(const char *name) {
    uint32_t h = hash_name(name);
    int lo = 0, hi = s_pe_n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s_pe[mid].hash == h) {
            int best = mid, k = mid;
            while (k > 0 && s_pe[k - 1].hash == h) k--;
            for (; k < s_pe_n && s_pe[k].hash == h; k++) if (s_pe[k].dir < s_pe[best].dir) best = k;
            return best;
        }
        if (s_pe[mid].hash < h) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

static bool pack_read(int e, Color *dst) {
    if (!s_pack || e < 0) return false;
    if (fseek(s_pack, (long)s_pe[e].offset, SEEK_SET) != 0) return false;
    if (fread(dst, sizeof(Color), COVER_SIZE * COVER_SIZE, s_pack) != COVER_SIZE * COVER_SIZE) return false;
    return true;
}

static int index_find(const char *name) {
    if (!s_scan_done) return -1;
    uint32_t h = hash_name(name);
    int lo = 0, hi = s_index_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s_index[mid].hash == h) {
            // several directories may contain the same name: return the best (lowest) dir
            int best = s_index[mid].dir, k = mid;
            while (k > 0 && s_index[k - 1].hash == h) k--;
            for (; k < s_index_count && s_index[k].hash == h; k++) if (s_index[k].dir < best) best = s_index[k].dir;
            return best;
        }
        if (s_index[mid].hash < h) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

// Scale an RGB(A) source into the square slot, cropping to fill ("cover" fit)
typedef Color (*PixelFn)(const void *ctx, int x, int y);

static void fit_cover(Color *dst, int sw, int sh, PixelFn fn, const void *ctx) {
    // the whole picture, aspect kept ("contain"), centered; the border stays transparent (0)
    int dw = COVER_SIZE, dh = COVER_SIZE;
    if (sw >= sh) dh = (sh * COVER_SIZE + sw / 2) / sw; else dw = (sw * COVER_SIZE + sh / 2) / sh;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int px0 = (COVER_SIZE - dw) / 2, py0 = (COVER_SIZE - dh) / 2;
    for (int i = 0; i < COVER_SIZE * COVER_SIZE; i++) dst[i] = 0;
    for (int y = 0; y < dh; y++) {
        int sy0 = (y * sh) / dh, sy1 = ((y + 1) * sh) / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; x++) {
            int sx0 = (x * sw) / dw, sx1 = ((x + 1) * sw) / dw;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            u32 r = 0, g = 0, b = 0, n = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                for (int xx = sx0; xx < sx1; xx++) {
                    Color c = fn(ctx, xx, yy);
                    r += c & 0x1F; g += (c >> 5) & 0x1F; b += (c >> 10) & 0x1F; n++;
                }
            }
            dst[(py0 + y) * COVER_SIZE + px0 + x] = (Color)((r / n) | ((g / n) << 5) | ((b / n) << 10) | 0x8000);
        }
    }
}

typedef struct {
    const unsigned char *buf;
    unsigned w;
    int bpp; // bytes per pixel
    int fmt; // 0 rgb, 1 rgba, 2 grey, 3 grey+alpha
} PngCtx;

static Color png_px(const void *vctx, int x, int y) {
    const PngCtx *c = (const PngCtx *)vctx;
    const unsigned char *p = c->buf + ((size_t)y * c->w + (size_t)x) * c->bpp;
    unsigned r, g, b, a = 255;
    if (c->fmt <= 1) { r = p[0]; g = p[1]; b = p[2]; if (c->fmt == 1) a = p[3]; }
    else { r = g = b = p[0]; if (c->fmt == 3) a = p[1]; }
    if (a < 255) { // composite on dark grey
        r = (r * a + 40 * (255 - a)) / 255; g = (g * a + 40 * (255 - a)) / 255; b = (b * a + 40 * (255 - a)) / 255;
    }
    return (Color)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000);
}

static bool decode_png(const char *path, Color *dst) {
    upng_t *u = upng_new_from_file(path);
    if (!u) return false;
    bool ok = false;
    if (upng_decode(u) == UPNG_EOK) {
        PngCtx c;
        c.buf = upng_get_buffer(u);
        c.w = upng_get_width(u);
        unsigned h = upng_get_height(u);
        switch (upng_get_format(u)) {
            case UPNG_RGB8: c.bpp = 3; c.fmt = 0; ok = true; break;
            case UPNG_RGBA8: c.bpp = 4; c.fmt = 1; ok = true; break;
            case UPNG_LUMINANCE8: c.bpp = 1; c.fmt = 2; ok = true; break;
            case UPNG_LUMINANCE_ALPHA8: c.bpp = 2; c.fmt = 3; ok = true; break;
            default: break;
        }
        if (ok && c.buf && c.w >= 8 && h >= 8 && c.w <= 1024 && h <= 1024) fit_cover(dst, (int)c.w, (int)h, png_px, &c);
        else ok = false;
    }
    upng_free(u);
    return ok;
}

typedef struct {
    const uint8_t *data;
    int stride, h, bpp;
    bool flip;
} BmpCtx;

static Color bmp_px(const void *vctx, int x, int y) {
    const BmpCtx *c = (const BmpCtx *)vctx;
    int row = c->flip ? (c->h - 1 - y) : y;
    const uint8_t *p = c->data + (size_t)row * c->stride + (size_t)x * (c->bpp / 8);
    if (c->bpp == 16) {
        uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
        return (Color)((((v >> 10) & 0x1F)) | (((v >> 5) & 0x1F) << 5) | ((v & 0x1F) << 10) | 0x8000);
    }
    return (Color)((p[2] >> 3) | ((p[1] >> 3) << 5) | ((p[0] >> 3) << 10) | 0x8000);
}

static bool decode_bmp(const char *path, Color *dst) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[54];
    bool ok = false;
    if (fread(h, 1, 54, f) == 54 && h[0] == 'B' && h[1] == 'M') {
        int32_t w = (int32_t)(h[18] | (h[19] << 8) | (h[20] << 16) | ((uint32_t)h[21] << 24));
        int32_t hh = (int32_t)(h[22] | (h[23] << 8) | (h[24] << 16) | ((uint32_t)h[25] << 24));
        int bpp = h[28] | (h[29] << 8);
        uint32_t off = h[10] | (h[11] << 8) | (h[12] << 16) | ((uint32_t)h[13] << 24);
        uint32_t comp = h[30] | (h[31] << 8) | (h[32] << 16) | ((uint32_t)h[33] << 24);
        int ah = hh < 0 ? -hh : hh;
        if (w >= 8 && w <= 1024 && ah >= 8 && ah <= 1024 && (bpp == 16 || bpp == 24 || bpp == 32) && (comp == 0 || comp == 3)) {
            int stride = ((w * (bpp / 8)) + 3) & ~3;
            uint8_t *data = malloc((size_t)stride * ah);
            if (data && fseek(f, (long)off, SEEK_SET) == 0 && fread(data, 1, (size_t)stride * ah, f) == (size_t)stride * ah) {
                BmpCtx c = { data, stride, ah, bpp, hh > 0 };
                fit_cover(dst, w, ah, bmp_px, &c);
                ok = true;
            }
            free(data);
        }
    }
    fclose(f);
    return ok;
}

static bool try_load(const char *name, Color *dst) {
    int d = index_find(name);
    if (d < 0) return false;
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s%s/%s", s_drv, s_dirs[d], name);
    const char *ext = strrchr(name, '.');
    if (ext && strcasecmp(ext, ".bmp") == 0) return decode_bmp(path, dst);
    return decode_png(path, dst);
}

// Game card box art: <drv>/_hyper/covers/<CODE>.png (works even when box art is turned off)
bool covers_load_code(const char *drv, const char *code, Color *dst) {
    if (!code || !code[0] || !dst) return false;
    char name[16];
    snprintf(name, sizeof(name), "%.4s.png", code);
    if (s_pack && !strcmp(drv ? drv : "", s_drv) && pack_read(pack_find(name), dst)) return true;
    for (unsigned d = 0; d < sizeof(s_dirs) / sizeof(s_dirs[0]); d++) {
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s%s/%s", drv ? drv : "", s_dirs[d], name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fclose(f);
        if (decode_png(path, dst)) return true;
    }
    return false;
}

static Slot *cache_hit(int lib_idx) {
    for (int i = 0; i < s_slot_n; i++) if (s_slots[i].lib_idx == lib_idx) return &s_slots[i];
    return NULL;
}

// visible = false: prefetch (no redraw is asked for, the picture is only kept for later)
static const Color *load(int lib_idx, const GameItem *item, bool visible) {
    if (!item || !s_slots || lib_idx < 0 || lib_idx >= MAX_LIBRARY_ITEMS) return NULL;
    Slot *hit = cache_hit(lib_idx);
    if (hit) {
        hit->stamp = s_stamp++;
        return hit->px;
    }
    if (s_state[lib_idx] || !s_enabled) return NULL;

    // candidate names, best first
    char stem[MAX_TITLE_LEN];
    snprintf(stem, sizeof(stem), "%s", item->filename);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = '\0';
    char names[6][MAX_TITLE_LEN + 8];
    int n = 0;
    // "<file name>.png" first: a cover made on the console from a photo (Photos > Set as cover)
    snprintf(names[n++], sizeof(names[0]), "%s.png", item->filename);
    if (item->game_code[0] && item->type == TYPE_NDS && strcmp(item->game_code, "####") != 0) {
        snprintf(names[n++], sizeof(names[0]), "%.4s.png", item->game_code);
        snprintf(names[n++], sizeof(names[0]), "%.4s.bmp", item->game_code);
    }
    snprintf(names[n++], sizeof(names[0]), "%s.png", stem);
    snprintf(names[n++], sizeof(names[0]), "%s.bmp", item->filename);
    snprintf(names[n++], sizeof(names[0]), "%s.bmp", stem);

    int pe = -1;
    bool own = index_find(names[0]) == 0;      // a cover made on the console wins over the pack
    for (int i = 0; i < n && pe < 0 && !own; i++) pe = pack_find(names[i]);
    bool in_dirs = false;
    if (pe < 0) {
        if (!s_scan_done) return NULL;        // not known yet: asked again when the scan ends
        for (int i = 0; i < n; i++) if (index_find(names[i]) >= 0) { in_dirs = true; break; }
        if (!in_dirs) { s_state[lib_idx] = 1; return NULL; }
    }
    int cost = pe >= 0 ? COST_PACK : COST_DECODE;
    if (s_budget < cost) return NULL;     // later: a few reads or one decode per frame keeps scrolling smooth
    s_budget -= cost;

    int victim = 0;   // LRU slot
    for (int i = 1; i < s_slot_n; i++) if (s_slots[i].stamp < s_slots[victim].stamp) victim = i;
    Slot *sl = &s_slots[victim];
    bool ok = pe >= 0 && pack_read(pe, sl->px);
    for (int i = 0; !ok && i < n; i++) ok = try_load(names[i], sl->px);
    if (ok) {
        sl->lib_idx = lib_idx;
        sl->stamp = visible ? s_stamp++ : (s_stamp > 4 ? s_stamp - 4 : 0);   // prefetched: first to go
        if (visible) s_generation++;
        return sl->px;
    }
    s_state[lib_idx] = 2;
    sl->lib_idx = -1;
    if (visible) s_generation++;
    return NULL;
}

const Color *covers_get(int lib_idx, const GameItem *item) { return load(lib_idx, item, true); }

bool covers_prefetch(int lib_idx, const GameItem *item) {
    if (s_budget <= 0 || !item || lib_idx < 0 || lib_idx >= MAX_LIBRARY_ITEMS) return false;
    if (s_state[lib_idx] || (s_slots && cache_hit(lib_idx))) return false;
    return load(lib_idx, item, false) != NULL;
}

bool covers_budget_left(void) { return s_budget > 0; }

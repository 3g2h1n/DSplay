// Photo viewer (see include/photos.h). JPEG decoding: stb_image (public domain).
#include "photos.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 4096
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#include "../third_party/stb/stb_image.h"
#include "../third_party/tjpgd/tjpgd.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

typedef struct { char *path; uint32_t t; } Photo;
static Photo *s_ph;
static int    s_n;

static bool is_pic(const char *name) {
    const char *d = strrchr(name, '.');
    return d && (!strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg") || !strcasecmp(d, ".png"));
}

static void add_dir(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && s_n < MAX_PHOTOS) {
        if (e->d_name[0] == '.') continue;
        char p[MAX_PATH_LEN];
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 3) add_dir(p, depth + 1);
        } else if (is_pic(e->d_name) && st.st_size > 0 && st.st_size < 8 * 1024 * 1024) {
            s_ph[s_n].path = strdup(p);
            s_ph[s_n].t = (uint32_t)st.st_mtime;
            if (s_ph[s_n].path) s_n++;
        }
    }
    closedir(d);
}

static int newest_first(const void *a, const void *b) {
    const Photo *x = a, *y = b;
    if (x->t != y->t) return x->t < y->t ? 1 : -1;
    return strcmp(y->path, x->path);            // DSi names count up: HNI_0012 after HNI_0011
}

bool photos_exist(const char *drive) {
    char p[96];
    struct stat st;
    snprintf(p, sizeof(p), "%s/DCIM", drive);
    if (stat(p, &st) == 0) return true;
    snprintf(p, sizeof(p), "%s/photos", drive);
    return stat(p, &st) == 0;
}

int photos_scan(const char *drive) {
    for (int i = 0; i < s_n; i++) free(s_ph[i].path);
    s_n = 0;
    if (!s_ph) s_ph = calloc(MAX_PHOTOS, sizeof(Photo));
    if (!s_ph) return 0;
    char p[96];
    snprintf(p, sizeof(p), "%s/DCIM", drive);
    add_dir(p, 0);
    snprintf(p, sizeof(p), "%s/photos", drive);
    add_dir(p, 0);
    qsort(s_ph, (size_t)s_n, sizeof(Photo), newest_first);
    return s_n;
}

int photos_count(void) { return s_n; }
const char *photos_path(int i) { return (i >= 0 && i < s_n) ? s_ph[i].path : ""; }
uint32_t photos_time(int i) { return (i >= 0 && i < s_n) ? s_ph[i].t : 0; }

// fit src (sw x sh, RGB555) inside the 256x192 screen, averaging the pixels under each one
static void fit(const Color *src, int sw, int sh, Color *dst) {
    int dw = SCREEN_WIDTH, dh = SCREEN_WIDTH * sh / sw;
    if (dh > SCREEN_HEIGHT) { dh = SCREEN_HEIGHT; dw = SCREEN_HEIGHT * sw / sh; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ox = (SCREEN_WIDTH - dw) / 2, oy = (SCREEN_HEIGHT - dh) / 2;
    for (int y = 0; y < dh; y++) {
        int sy0 = y * sh / dh, sy1 = (y + 1) * sh / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; x++) {
            int sx0 = x * sw / dw, sx1 = (x + 1) * sw / dw;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned r = 0, g = 0, b = 0, cnt = 0;
            for (int yy = sy0; yy < sy1; yy++)
                for (int xx = sx0; xx < sx1; xx++) {
                    Color c = src[yy * sw + xx];
                    r += c & 31; g += (c >> 5) & 31; b += (c >> 10) & 31; cnt++;
                }
            dst[(oy + y) * SCREEN_WIDTH + ox + x] = (Color)(0x8000 | (r / cnt) | ((g / cnt) << 5) | ((b / cnt) << 10));
        }
    }
}

// --- JPEG: TJpgDec decodes block by block, already shrunk 2x/4x/8x (little memory: the
// DSi Camera's 640x480 pictures need about 160 KB instead of 1 MB)
typedef struct { FILE *f; Color *out; int w, h; } JpgCtx;

static size_t jpg_in(JDEC *jd, uint8_t *buf, size_t n) {
    JpgCtx *c = jd->device;
    if (buf) return fread(buf, 1, n, c->f);
    return fseek(c->f, (long)n, SEEK_CUR) == 0 ? n : 0;
}

static int jpg_out(JDEC *jd, void *bitmap, JRECT *r) {
    JpgCtx *c = jd->device;
    const uint8_t *p = bitmap;
    for (int y = r->top; y <= r->bottom; y++)
        for (int x = r->left; x <= r->right; x++, p += 3)
            if (x < c->w && y < c->h) c->out[y * c->w + x] = C8(p[0], p[1], p[2]);
    return 1;
}

static bool load_jpeg(FILE *f, Color *dst) {
    static uint8_t pool[TJPGD_WORKSPACE_SIZE + 4096] __attribute__((aligned(4)));
    JDEC jd;
    JpgCtx ctx = { f, NULL, 0, 0 };
    if (jd_prepare(&jd, jpg_in, pool, sizeof(pool), &ctx) != JDR_OK) return false;
    int scale = 0;                              // largest shrink that still covers the screen
    while (scale < 3 && (jd.width >> (scale + 1)) >= SCREEN_WIDTH && (jd.height >> (scale + 1)) >= SCREEN_HEIGHT) scale++;
    for (;;) {                                  // little memory left (DS mode): a smaller decode
        ctx.w = (jd.width + (1 << scale) - 1) >> scale;
        ctx.h = (jd.height + (1 << scale) - 1) >> scale;
        if (ctx.w * ctx.h <= 1024 * 768) ctx.out = calloc((size_t)ctx.w * (size_t)ctx.h, sizeof(Color));
        if (ctx.out || scale >= 3) break;
        scale++;
    }
    if (!ctx.out) return false;
    bool ok = jd_decomp(&jd, jpg_out, (uint8_t)scale) == JDR_OK;
    if (ok) fit(ctx.out, ctx.w, ctx.h, dst);
    free(ctx.out);
    return ok;
}

bool photos_load(int i, Color *dst) {
    for (int k = 0; k < SCREEN_WIDTH * SCREEN_HEIGHT; k++) dst[k] = 0x8000;
    if (i < 0 || i >= s_n) return false;
    FILE *f = fopen(s_ph[i].path, "rb");
    if (!f) return false;
    const char *dot = strrchr(s_ph[i].path, '.');
    if (dot && strcasecmp(dot, ".png") != 0) {
        bool ok = load_jpeg(f, dst);
        fclose(f);
        return ok;
    }
    // PNG (rare: pictures copied by hand) - stb_image
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (n > 0 && n < 4 * 1024 * 1024) ? malloc((size_t)n) : NULL;
    bool ok = buf && fread(buf, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok) { free(buf); return false; }
    int w = 0, h = 0, comp = 0;
    unsigned char *px = stbi_load_from_memory(buf, (int)n, &w, &h, &comp, 3);
    free(buf);
    if (!px) return false;
    Color *tmp = malloc((size_t)w * (size_t)h * sizeof(Color));
    if (tmp) {
        for (long k = 0; k < (long)w * h; k++) tmp[k] = C8(px[k * 3], px[k * 3 + 1], px[k * 3 + 2]);
        fit(tmp, w, h, dst);
        free(tmp);
    }
    stbi_image_free(px);
    return tmp != NULL;
}

// Photos beyond the viewer (see include/photofx.h)
#include "photofx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Crop
// ---------------------------------------------------------------------------
static void crop_fix(PhotoCrop *c) {
    int maxh = SCREEN_HEIGHT;
    if (maxh * c->aw / c->ah > SCREEN_WIDTH) maxh = SCREEN_WIDTH * c->ah / c->aw;
    int minh = 24;
    if (c->h > maxh) c->h = maxh;
    if (c->h < minh) c->h = minh;
    int w = c->h * c->aw / c->ah;
    if (c->cx < w / 2) c->cx = w / 2;
    if (c->cx > SCREEN_WIDTH - (w - w / 2)) c->cx = SCREEN_WIDTH - (w - w / 2);
    if (c->cy < c->h / 2) c->cy = c->h / 2;
    if (c->cy > SCREEN_HEIGHT - (c->h - c->h / 2)) c->cy = SCREEN_HEIGHT - (c->h - c->h / 2);
}

void photo_crop_init(PhotoCrop *c, int aw, int ah) {
    c->aw = aw > 0 ? aw : 1;
    c->ah = ah > 0 ? ah : 1;
    c->h = SCREEN_HEIGHT;
    c->cx = SCREEN_WIDTH / 2;
    c->cy = SCREEN_HEIGHT / 2;
    crop_fix(c);
}

void photo_crop_move(PhotoCrop *c, int dx, int dy) { c->cx += dx; c->cy += dy; crop_fix(c); }
void photo_crop_zoom(PhotoCrop *c, int dh) { c->h += dh; crop_fix(c); }

void photo_crop_rect(const PhotoCrop *c, int *x, int *y, int *w, int *h) {
    int ww = c->h * c->aw / c->ah;
    if (x) *x = c->cx - ww / 2;
    if (y) *y = c->cy - c->h / 2;
    if (w) *w = ww;
    if (h) *h = c->h;
}

// ---------------------------------------------------------------------------
// Scaling (bilinear, 8-bit fractions; shrinking averages a few pixels)
// ---------------------------------------------------------------------------
static inline Color mix4(Color a, Color b, Color c, Color d, int fx, int fy) {
    int r0 = (a & 31) * (256 - fx) + (b & 31) * fx, r1 = (c & 31) * (256 - fx) + (d & 31) * fx;
    int g0 = ((a >> 5) & 31) * (256 - fx) + ((b >> 5) & 31) * fx, g1 = ((c >> 5) & 31) * (256 - fx) + ((d >> 5) & 31) * fx;
    int b0 = ((a >> 10) & 31) * (256 - fx) + ((b >> 10) & 31) * fx, b1 = ((c >> 10) & 31) * (256 - fx) + ((d >> 10) & 31) * fx;
    int r = (r0 * (256 - fy) + r1 * fy) >> 16, g = (g0 * (256 - fy) + g1 * fy) >> 16, bb = (b0 * (256 - fy) + b1 * fy) >> 16;
    return (Color)(0x8000 | r | (g << 5) | (bb << 10));
}

void photo_sample(const Color *src, int sw, int sh, int rx, int ry, int rw, int rh, Color *dst, int dw, int dh) {
    if (!src || !dst || dw <= 0 || dh <= 0 || rw <= 0 || rh <= 0) return;
    for (int y = 0; y < dh; y++) {
        // centre of the destination pixel in source coordinates (8.8 fixed point)
        int sy = ry * 256 + (int)(((long long)(2 * y + 1) * rh * 256) / (2 * dh)) - 128;
        if (sy < 0) sy = 0;
        int y0 = sy >> 8, fy = sy & 255;
        if (y0 >= sh - 1) { y0 = sh - 1; fy = 0; }
        int y1 = y0 + 1 < sh ? y0 + 1 : y0;
        for (int x = 0; x < dw; x++) {
            int sx = rx * 256 + (int)(((long long)(2 * x + 1) * rw * 256) / (2 * dw)) - 128;
            if (sx < 0) sx = 0;
            int x0 = sx >> 8, fx = sx & 255;
            if (x0 >= sw - 1) { x0 = sw - 1; fx = 0; }
            int x1 = x0 + 1 < sw ? x0 + 1 : x0;
            Color c;
            if (rw > dw * 2 || rh > dh * 2) {         // shrinking a lot: average a 2x2 grid of samples
                int stx = rw * 128 / dw, sty = rh * 128 / dh;   // a quarter pixel step (8.8)
                unsigned r = 0, g = 0, b = 0;
                for (int k = 0; k < 4; k++) {
                    int px = (sx + ((k & 1) ? stx : -stx)) >> 8, py = (sy + ((k & 2) ? sty : -sty)) >> 8;
                    if (px < 0) px = 0;
                    if (px >= sw) px = sw - 1;
                    if (py < 0) py = 0;
                    if (py >= sh) py = sh - 1;
                    Color s = src[py * sw + px];
                    r += s & 31; g += (s >> 5) & 31; b += (s >> 10) & 31;
                }
                c = (Color)(0x8000 | (r >> 2) | ((g >> 2) << 5) | ((b >> 2) << 10));
            } else {
                c = mix4(src[y0 * sw + x0], src[y0 * sw + x1], src[y1 * sw + x0], src[y1 * sw + x1], fx, fy);
            }
            dst[y * dw + x] = c;
        }
    }
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
static bool finish_tmp(const char *tmp, const char *path, bool ok) {
    if (!ok) { remove(tmp); return false; }
    remove(path);
    if (rename(tmp, path) != 0) { remove(tmp); return false; }
    return true;
}

bool photo_write_raw(const char *path, const char magic[4], const Color *px, int w, int h) {
    char tmp[MAX_PATH_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    uint8_t hdr[8] = { (uint8_t)magic[0], (uint8_t)magic[1], (uint8_t)magic[2], (uint8_t)magic[3],
                       (uint8_t)(w & 255), (uint8_t)(w >> 8), (uint8_t)(h & 255), (uint8_t)(h >> 8) };
    bool ok = fwrite(hdr, 1, 8, f) == 8 && fwrite(px, sizeof(Color), (size_t)(w * h), f) == (size_t)(w * h);
    if (fclose(f) != 0) ok = false;
    return finish_tmp(tmp, path, ok);
}

bool photo_read_raw(const char *path, const char magic[4], Color *px, int w, int h) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[8];
    bool ok = fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, magic, 4) && (hdr[4] | (hdr[5] << 8)) == w &&
              (hdr[6] | (hdr[7] << 8)) == h && fread(px, sizeof(Color), (size_t)(w * h), f) == (size_t)(w * h);
    fclose(f);
    return ok;
}

static uint32_t s_crc_tab[256];
static uint32_t crc32_upd(uint32_t crc, const uint8_t *p, size_t n) {
    if (!s_crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            s_crc_tab[i] = c;
        }
    crc = ~crc;
    while (n--) crc = s_crc_tab[(crc ^ *p++) & 255] ^ (crc >> 8);
    return ~crc;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static bool png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t h[8];
    be32(h, len);
    memcpy(h + 4, type, 4);
    uint32_t crc = crc32_upd(0, (const uint8_t *)type, 4);
    if (len) crc = crc32_upd(crc, data, len);
    uint8_t c[4];
    be32(c, crc);
    return fwrite(h, 1, 8, f) == 8 && (!len || fwrite(data, 1, len, f) == len) && fwrite(c, 1, 4, f) == 4;
}

bool photo_write_png(const char *path, const Color *px, int w, int h) {
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
    size_t row = (size_t)w * 3 + 1, raw_len = row * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len);
    uint8_t *raw = malloc(raw_len);
    if (!z || !raw) { free(z); free(raw); return false; }
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * row;
        *r++ = 0;                                 // filter: none
        for (int x = 0; x < w; x++) {
            Color c = px[y * w + x];
            int cr = c & 31, cg = (c >> 5) & 31, cb = (c >> 10) & 31;
            *r++ = (uint8_t)((cr << 3) | (cr >> 2));
            *r++ = (uint8_t)((cg << 3) | (cg >> 2));
            *r++ = (uint8_t)((cb << 3) | (cb >> 2));
        }
    }
    // zlib stream with stored blocks
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t done = 0; done < raw_len;) {
        size_t n = raw_len - done > 65535 ? 65535 : raw_len - done;
        z[o++] = (uint8_t)(done + n == raw_len ? 1 : 0);
        z[o++] = (uint8_t)(n & 255); z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)(~n & 255); z[o++] = (uint8_t)((~n >> 8) & 255);
        memcpy(z + o, raw + done, n);
        for (size_t i = 0; i < n; i++) { a = (a + raw[done + i]) % 65521; b = (b + a) % 65521; }
        o += n;
        done += n;
    }
    be32(z + o, (b << 16) | a);
    o += 4;
    free(raw);
    char tmp[MAX_PATH_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(z); return false; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    bool ok = fwrite(sig, 1, 8, f) == 8 && png_chunk(f, "IHDR", ihdr, 13) && png_chunk(f, "IDAT", z, (uint32_t)o) &&
              png_chunk(f, "IEND", NULL, 0);
    if (fclose(f) != 0) ok = false;
    free(z);
    return finish_tmp(tmp, path, ok);
}

// ---------------------------------------------------------------------------
// Favorites
// ---------------------------------------------------------------------------
#define FAV_MAX 400
static char  s_fav_file[MAX_PATH_LEN];
static char *s_fav[FAV_MAX];
static int   s_fav_n;

void photo_favs_load(const char *file) {
    for (int i = 0; i < s_fav_n; i++) free(s_fav[i]);
    s_fav_n = 0;
    snprintf(s_fav_file, sizeof(s_fav_file), "%s", file ? file : "");
    FILE *f = file ? fopen(file, "r") : NULL;
    if (!f) return;
    char line[MAX_PATH_LEN];
    while (s_fav_n < FAV_MAX && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] && (s_fav[s_fav_n] = strdup(line)) != NULL) s_fav_n++;
    }
    fclose(f);
}

bool photo_favs_save(void) {
    if (!s_fav_file[0]) return false;
    char tmp[MAX_PATH_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", s_fav_file);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    bool ok = true;
    for (int i = 0; i < s_fav_n; i++) if (fprintf(f, "%s\n", s_fav[i]) < 0) ok = false;
    if (fclose(f) != 0) ok = false;
    return finish_tmp(tmp, s_fav_file, ok);
}

// the drive prefix is left out, so the list works on "sd:" and "fat:" alike
static const char *no_drive(const char *p) {
    const char *c = strchr(p, ':');
    return (c && c - p <= 4) ? c + 1 : p;
}

bool photo_is_fav(const char *path) {
    if (!path) return false;
    const char *p = no_drive(path);
    for (int i = 0; i < s_fav_n; i++) if (!strcmp(s_fav[i], p)) return true;
    return false;
}

void photo_set_fav(const char *path, bool on) {
    if (!path) return;
    const char *p = no_drive(path);
    for (int i = 0; i < s_fav_n; i++) {
        if (strcmp(s_fav[i], p) != 0) continue;
        if (on) return;
        free(s_fav[i]);
        memmove(&s_fav[i], &s_fav[i + 1], sizeof(char *) * (size_t)(s_fav_n - i - 1));
        s_fav_n--;
        return;
    }
    if (on && s_fav_n < FAV_MAX && (s_fav[s_fav_n] = strdup(p)) != NULL) s_fav_n++;
}

int photo_favs_count(void) { return s_fav_n; }

// ---------------------------------------------------------------------------
// Screensaver
// ---------------------------------------------------------------------------
void photo_kenburns(Canvas *c, const Color *src, int t, int variant) {
    if (!src) return;
    if (t < 0) t = 0;
    if (t > 4096) t = 4096;
    // zoom between 100 % and 115 % (in or out), drifting toward one of four corners
    int zin = (variant & 1) == 0;
    int z = 4096 + (zin ? t : 4096 - t) * 15 / 100;                 // 4096 = 1.0
    int vw = SCREEN_WIDTH * 4096 / z, vh = SCREEN_HEIGHT * 4096 / z;  // visible part of the picture
    int slack_x = SCREEN_WIDTH - vw, slack_y = SCREEN_HEIGHT - vh;
    int dirx = (variant & 2) ? 1 : -1, diry = (variant & 4) ? 1 : -1;
    int ox = slack_x / 2 + dirx * slack_x / 2 * t / 4096 / 2;
    int oy = slack_y / 2 + diry * slack_y / 2 * t / 4096 / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox > slack_x) ox = slack_x;
    if (oy > slack_y) oy = slack_y;
    // 16.16 steps through the source
    int step = (int)(((long long)vw << 16) / SCREEN_WIDTH);
    int stepy = (int)(((long long)vh << 16) / SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int sy = (oy << 16) + y * stepy;
        int y0 = sy >> 16, fy = (sy >> 8) & 255;
        int y1 = y0 + 1 < SCREEN_HEIGHT ? y0 + 1 : y0;
        const Color *r0 = src + y0 * SCREEN_WIDTH, *r1 = src + y1 * SCREEN_WIDTH;
        Color *out = c->px + y * c->w;
        int sx = ox << 16;
        for (int x = 0; x < SCREEN_WIDTH; x++, sx += step) {
            int x0 = sx >> 16, fx = (sx >> 8) & 255;
            int x1 = x0 + 1 < SCREEN_WIDTH ? x0 + 1 : x0;
            out[x] = mix4(r0[x0], r0[x1], r1[x0], r1[x1], fx, fy);
        }
    }
}

void photo_blend(Color *dst, const Color *a, const Color *b, int k) {
    if (k <= 0) { memcpy(dst, a, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color)); return; }
    if (k >= 32) { memcpy(dst, b, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color)); return; }
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        Color x = a[i], y = b[i];
        int r = ((x & 31) * (32 - k) + (y & 31) * k) >> 5;
        int g = (((x >> 5) & 31) * (32 - k) + ((y >> 5) & 31) * k) >> 5;
        int bb = (((x >> 10) & 31) * (32 - k) + ((y >> 10) & 31) * k) >> 5;
        dst[i] = (Color)(0x8000 | r | (g << 5) | (bb << 10));
    }
}

int photo_month(uint32_t t) {
    if (!t) return 0;
    time_t tt = (time_t)t;
    struct tm *m = localtime(&tt);
    return m ? (m->tm_year + 1900) * 100 + m->tm_mon + 1 : 0;
}

int photo_of_day(int count, uint32_t now) {
    if (count <= 0) return -1;
    uint32_t day = now / 86400u;
    return (int)((day * 2654435761u >> 8) % (uint32_t)count);
}

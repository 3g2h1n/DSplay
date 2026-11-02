#include "gfx.h"
#include <string.h>

#ifdef ARM9
#include <nds.h>
#endif


static Color s_top_px[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(32)));
static Color s_bot_px[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(32)));

Canvas g_top = { s_top_px, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
Canvas g_bot = { s_bot_px, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };

void gfx_canvas_init(Canvas *c, Color *px, int w, int h) {
    c->px = px; c->w = w; c->h = h;
    c->cx0 = 0; c->cy0 = 0; c->cx1 = w; c->cy1 = h;
}

static int s_dim = 0;

#ifdef ARM9
static u16 *s_vram_top = NULL;
static u16 *s_vram_bot = NULL;
#endif

// --------------------------------------------------------------------------
// Color math (RGB555, channels spread into a 32-bit word: R bits 0-9,
// B bits 10-19, G bits 21-30 -> two products never overflow into each other)
// --------------------------------------------------------------------------
#define SPREAD_MASK 0x03E07C1Fu

static inline u32 spread(Color c) {
    u32 v = c & 0x7FFF;
    return (v | (v << 16)) & SPREAD_MASK;
}

static inline Color unspread(u32 v) {
    v &= SPREAD_MASK;
    return (Color)((v | (v >> 16)) & 0x7FFF) | 0x8000;
}

Color gfx_blend(Color dst, Color src, int a) {
    if (a <= 0) return dst | 0x8000;
    if (a >= 32) return src | 0x8000;
    u32 d = spread(dst), s = spread(src);
    return unspread((s * (u32)a + d * (u32)(32 - a)) >> 5);
}

Color gfx_mix(Color a, Color b, int t) {
    if (t <= 0) return a | 0x8000;
    if (t >= 256) return b | 0x8000;
    return gfx_blend(a, b, (t + 4) >> 3);
}

Color gfx_lighten(Color c, int amount) { return gfx_blend(c, 0xFFFF, amount); }
Color gfx_darken(Color c, int amount)  { return gfx_blend(c, 0x8000, amount); }

// --------------------------------------------------------------------------
// Setup & presentation
// --------------------------------------------------------------------------
void gfx_init(void) {
#ifdef ARM9
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    int bg_top = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bg_bot = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    s_vram_top = bgGetGfxPtr(bg_top);
    s_vram_bot = bgGetGfxPtr(bg_bot);
    dmaFillHalfWords(0x8000, s_vram_top, 256 * 256 * 2);
    dmaFillHalfWords(0x8000, s_vram_bot, 256 * 256 * 2);
#endif
    gfx_clear(&g_top, 0x8000);
    gfx_clear(&g_bot, 0x8000);
}

void gfx_set_dim(int level) {
    if (level < 0) level = 0;
    if (level > 31) level = 31;
    s_dim = level;
}

static void apply_dim(Color *px) {
    int keep = 32 - s_dim;
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        px[i] = unspread((spread(px[i]) * (u32)keep) >> 5);
    }
}

void gfx_present(bool top, bool bottom) {
    if (s_dim) {
        if (top) apply_dim(s_top_px);
        if (bottom) apply_dim(s_bot_px);
    }
#ifdef ARM9
    // Called at the start of vblank. Both screens are scanned out at the same time and one
    // full-screen copy takes a bit more than half of vblank, so when both changed the bottom
    // screen is copied in two halves around the top one: every row is written before the
    // display reaches it again (no tearing / "double image" while scrolling).
    if (top && s_vram_top) DC_FlushRange(s_top_px, sizeof(s_top_px));
    if (bottom && s_vram_bot) DC_FlushRange(s_bot_px, sizeof(s_bot_px));
    const size_t half = sizeof(s_bot_px) / 2;
    if (top && bottom && s_vram_top && s_vram_bot) {
        dmaCopyWords(3, s_bot_px, s_vram_bot, half);
        dmaCopyWords(3, s_top_px, s_vram_top, sizeof(s_top_px));
        dmaCopyWords(3, (const u8 *)s_bot_px + half, (u8 *)s_vram_bot + half, half);
        return;
    }
    if (top && s_vram_top) dmaCopyWords(3, s_top_px, s_vram_top, sizeof(s_top_px));
    if (bottom && s_vram_bot) dmaCopyWords(3, s_bot_px, s_vram_bot, sizeof(s_bot_px));
#else
    (void)top; (void)bottom;
#endif
}

void gfx_present_rows(bool top, int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y1 > SCREEN_HEIGHT) y1 = SCREEN_HEIGHT;
    if (y1 <= y0) return;
#ifdef ARM9
    Color *src = top ? s_top_px : s_bot_px;
    u16 *dst = top ? s_vram_top : s_vram_bot;
    if (!dst) return;
    size_t off = (size_t)y0 * SCREEN_WIDTH, bytes = (size_t)(y1 - y0) * SCREEN_WIDTH * 2;
    DC_FlushRange(src + off, bytes);
    dmaCopyWords(3, src + off, dst + off, bytes);
#else
    (void)top;
#endif
}

void gfx_copy_rect(Canvas *dst, const Canvas *src, int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > dst->w) w = dst->w - x;
    if (y + h > dst->h) h = dst->h - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++) memcpy(&dst->px[yy * dst->w + x], &src->px[yy * src->w + x], (size_t)w * 2);
}

void gfx_set_clip(Canvas *c, int x, int y, int w, int h) {
    c->cx0 = x < 0 ? 0 : x;
    c->cy0 = y < 0 ? 0 : y;
    c->cx1 = (x + w) > c->w ? c->w : (x + w);
    c->cy1 = (y + h) > c->h ? c->h : (y + h);
    if (c->cx1 < c->cx0) c->cx1 = c->cx0;
    if (c->cy1 < c->cy0) c->cy1 = c->cy0;
}

void gfx_reset_clip(Canvas *c) {
    c->cx0 = 0; c->cy0 = 0; c->cx1 = c->w; c->cy1 = c->h;
}

// --------------------------------------------------------------------------
// Basic primitives
// --------------------------------------------------------------------------
void gfx_clear(Canvas *c, Color col) {
    u32 v = (u32)(col | 0x8000) | ((u32)(col | 0x8000) << 16);
#ifdef ARM9
    // full 32-byte aligned screens: DMA fill, then drop the stale cache lines
    size_t bytes = (size_t)c->w * c->h * sizeof(Color);
    if (((uintptr_t)c->px & 31) == 0 && (bytes & 31) == 0) {
        DC_FlushRange(c->px, bytes);
        dmaFillWords(v, c->px, bytes);
        DC_InvalidateRange(c->px, bytes);
        return;
    }
#endif
    u32 *p = (u32 *)c->px;
    int n = (c->w * c->h) / 2;
    for (int i = 0; i < n; i++) p[i] = v;
    if ((c->w * c->h) & 1) c->px[c->w * c->h - 1] = col | 0x8000;
}

void gfx_pixel(Canvas *c, int x, int y, Color col) {
    if (x < c->cx0 || x >= c->cx1 || y < c->cy0 || y >= c->cy1) return;
    c->px[y * c->w + x] = col | 0x8000;
}

void gfx_pixel_a(Canvas *c, int x, int y, Color col, int a) {
    if (a <= 0 || x < c->cx0 || x >= c->cx1 || y < c->cy0 || y >= c->cy1) return;
    Color *p = &c->px[y * c->w + x];
    *p = gfx_blend(*p, col, a);
}

static bool clip_rect(const Canvas *c, int *x, int *y, int *w, int *h) {
    int x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h;
    if (x0 < c->cx0) x0 = c->cx0;
    if (y0 < c->cy0) y0 = c->cy0;
    if (x1 > c->cx1) x1 = c->cx1;
    if (y1 > c->cy1) y1 = c->cy1;
    if (x1 <= x0 || y1 <= y0) return false;
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    return true;
}

void gfx_rect(Canvas *c, int x, int y, int w, int h, Color col) {
    if (!clip_rect(c, &x, &y, &w, &h)) return;
    col |= 0x8000;
    u32 v2 = (u32)col | ((u32)col << 16);
    for (int yy = y; yy < y + h; yy++) {
        Color *row = &c->px[yy * c->w + x];
        int n = w;
        if (((uintptr_t)row & 2) && n) { *row++ = col; n--; }
        u32 *r32 = (u32 *)row;
        int pairs = n >> 1;
        for (int i = 0; i < pairs; i++) r32[i] = v2;
        if (n & 1) row[n - 1] = col;
    }
}

void gfx_rect_a(Canvas *c, int x, int y, int w, int h, Color col, int a) {
    if (a >= 32) { gfx_rect(c, x, y, w, h, col); return; }
    if (a <= 0 || !clip_rect(c, &x, &y, &w, &h)) return;
    u32 s = spread(col) * (u32)a;
    u32 ia = (u32)(32 - a);
    for (int yy = y; yy < y + h; yy++) {
        Color *row = &c->px[yy * c->w + x];
        for (int i = 0; i < w; i++) row[i] = unspread((s + spread(row[i]) * ia) >> 5);
    }
}

void gfx_hline(Canvas *c, int x, int y, int w, Color col, int a) {
    gfx_rect_a(c, x, y, w, 1, col, a);
}

void gfx_vgradient(Canvas *c, int x, int y, int w, int h, Color top, Color bottom) {
    if (h <= 0) return;
    for (int i = 0; i < h; i++) {
        Color col = gfx_mix(top, bottom, (i * 256) / (h > 1 ? h - 1 : 1));
        gfx_rect(c, x, y + i, w, 1, col);
    }
}

// --------------------------------------------------------------------------
// Anti-aliased rounded shapes. Coverage for a quarter circle of radius r is
// computed with 4x4 supersampling and cached per radius.
// --------------------------------------------------------------------------
#define MAX_RADIUS 48
static uint8_t s_cov[MAX_RADIUS + 1][MAX_RADIUS * MAX_RADIUS]; // [r][y*r+x], 0..32
static bool    s_cov_ready[MAX_RADIUS + 1];

static const uint8_t *corner_cov(int r) {
    if (r > MAX_RADIUS) r = MAX_RADIUS;
    if (!s_cov_ready[r]) {
        // pixel (x,y) of the top-left corner square, circle center at (r, r)
        int rr16 = (r * 8) * (r * 8);
        for (int y = 0; y < r; y++) {
            for (int x = 0; x < r; x++) {
                int inside = 0;
                for (int sy = 0; sy < 4; sy++) {
                    for (int sx = 0; sx < 4; sx++) {
                        int px8 = x * 8 + sx * 2 + 1; // in 1/8 pixel units
                        int py8 = y * 8 + sy * 2 + 1;
                        int dx = r * 8 - px8, dy = r * 8 - py8;
                        if (dx * dx + dy * dy <= rr16) inside++;
                    }
                }
                s_cov[r][y * r + x] = (uint8_t)(inside * 2); // 16 samples -> 0..32
            }
        }
        s_cov_ready[r] = true;
    }
    return s_cov[r];
}

void gfx_round_rect(Canvas *c, int x, int y, int w, int h, int r, Color col, int a) {
    if (w <= 0 || h <= 0 || a <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r > MAX_RADIUS) r = MAX_RADIUS;
    if (r <= 0) { gfx_rect_a(c, x, y, w, h, col, a); return; }
    const uint8_t *cov = corner_cov(r);
    // middle band
    gfx_rect_a(c, x, y + r, w, h - 2 * r, col, a);
    // top & bottom bands (between corners)
    gfx_rect_a(c, x + r, y, w - 2 * r, r, col, a);
    gfx_rect_a(c, x + r, y + h - r, w - 2 * r, r, col, a);
    // corners
    for (int yy = 0; yy < r; yy++) {
        for (int xx = 0; xx < r; xx++) {
            int cv = cov[yy * r + xx];
            if (!cv) continue;
            int aa = (cv * a) >> 5;
            gfx_pixel_a(c, x + xx, y + yy, col, aa);
            gfx_pixel_a(c, x + w - 1 - xx, y + yy, col, aa);
            gfx_pixel_a(c, x + xx, y + h - 1 - yy, col, aa);
            gfx_pixel_a(c, x + w - 1 - xx, y + h - 1 - yy, col, aa);
        }
    }
}

// Rounded rectangle with a vertical gradient, drawn row by row in one pass
void gfx_round_rect_vgrad(Canvas *c, int x, int y, int w, int h, int r, Color top, Color bottom) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r > MAX_RADIUS) r = MAX_RADIUS;
    const uint8_t *cov = r > 0 ? corner_cov(r) : NULL;
    for (int yy = 0; yy < h; yy++) {
        int py = y + yy;
        if (py < c->cy0 || py >= c->cy1) continue;
        Color col = gfx_mix(top, bottom, h > 1 ? (yy * 256) / (h - 1) : 0);
        int cy = (yy < r) ? yy : (yy >= h - r ? h - 1 - yy : -1);
        if (cy < 0) {
            gfx_rect(c, x, py, w, 1, col);
            continue;
        }
        // corner row: fully covered middle + anti-aliased ends
        const uint8_t *row = cov + cy * r;
        int full = 0;
        while (full < r && row[full] < 32) full++;
        gfx_rect(c, x + full, py, w - 2 * full, 1, col);
        for (int xx = 0; xx < full; xx++) {
            int a = row[xx];
            if (!a) continue;
            gfx_pixel_a(c, x + xx, py, col, a);
            gfx_pixel_a(c, x + w - 1 - xx, py, col, a);
        }
    }
}

void gfx_blit_round(Canvas *c, int x, int y, const Color *src, int sw, int sh, int r) {
    if (r * 2 > sw) r = sw / 2;
    if (r * 2 > sh) r = sh / 2;
    if (r > MAX_RADIUS) r = MAX_RADIUS;
    const uint8_t *cov = r > 0 ? corner_cov(r) : NULL;
    for (int yy = 0; yy < sh; yy++) {
        int py = y + yy;
        if (py < c->cy0 || py >= c->cy1) continue;
        int cy = (yy < r) ? yy : (yy >= sh - r ? sh - 1 - yy : -1);
        int x0 = 0, x1 = sw;
        const Color *srow = src + yy * sw;
        Color *drow = c->px + py * c->w;
        if (cy >= 0) {
            const uint8_t *row = cov + cy * r;
            int full = 0;
            while (full < r && row[full] < 32) full++;
            for (int xx = 0; xx < full; xx++) {
                int a = row[xx];
                if (!a) continue;
                int px = x + xx;
                if (px >= c->cx0 && px < c->cx1) drow[px] = gfx_blend(drow[px], srow[xx], a);
                px = x + sw - 1 - xx;
                if (px >= c->cx0 && px < c->cx1) drow[px] = gfx_blend(drow[px], srow[sw - 1 - xx], a);
            }
            x0 = full;
            x1 = sw - full;
        }
        int dx0 = x + x0, dx1 = x + x1;
        if (dx0 < c->cx0) dx0 = c->cx0;
        if (dx1 > c->cx1) dx1 = c->cx1;
        if (dx1 > dx0) memcpy(drow + dx0, srow + (dx0 - x), (size_t)(dx1 - dx0) * 2);
    }
}

// Coverage of a ring (outer radius R, inner radius R-t) for corner pixels
void gfx_round_ring(Canvas *c, int x, int y, int w, int h, int r, int t, Color col, int a) {
    if (w <= 0 || h <= 0 || t <= 0 || a <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r > MAX_RADIUS) r = MAX_RADIUS;
    // straight edges
    gfx_rect_a(c, x + r, y, w - 2 * r, t, col, a);
    gfx_rect_a(c, x + r, y + h - t, w - 2 * r, t, col, a);
    gfx_rect_a(c, x, y + r, t, h - 2 * r, col, a);
    gfx_rect_a(c, x + w - t, y + r, t, h - 2 * r, col, a);
    if (r <= 0) return;
    const uint8_t *outer = corner_cov(r);
    int ri = r - t;
    const uint8_t *inner = ri > 0 ? corner_cov(ri) : NULL;
    for (int yy = 0; yy < r; yy++) {
        for (int xx = 0; xx < r; xx++) {
            int cv = outer[yy * r + xx];
            if (inner) {
                int ix = xx - t, iy = yy - t;
                if (ix >= 0 && iy >= 0 && ix < ri && iy < ri) cv -= inner[iy * ri + ix];
                else if (ix >= ri || iy >= ri) cv = 0; // fully inside inner square (beyond corner)
            }
            if (cv <= 0) continue;
            int aa = (cv * a) >> 5;
            gfx_pixel_a(c, x + xx, y + yy, col, aa);
            gfx_pixel_a(c, x + w - 1 - xx, y + yy, col, aa);
            gfx_pixel_a(c, x + xx, y + h - 1 - yy, col, aa);
            gfx_pixel_a(c, x + w - 1 - xx, y + h - 1 - yy, col, aa);
        }
    }
}

void gfx_circle(Canvas *c, int cx, int cy, int r, Color col, int a) {
    gfx_round_rect(c, cx - r, cy - r, r * 2, r * 2, r, col, a);
}

void gfx_ring(Canvas *c, int cx, int cy, int r, int t, Color col, int a) {
    gfx_round_ring(c, cx - r, cy - r, r * 2, r * 2, r, t, col, a);
}

// Soft drop shadow: a few expanding translucent rounded rects
// Soft drop shadow drawn as a thin halo around the shape (only the visible border
// band is touched, so it stays cheap even for large cards)
void gfx_shadow(Canvas *c, int x, int y, int w, int h, int r, int spread_px, int a) {
    for (int i = spread_px; i >= 1; i--) {
        int aa = (a * (spread_px - i + 1)) / (spread_px * 2);
        if (aa < 1) aa = 1;
        gfx_round_ring(c, x - i, y - i + 2, w + 2 * i, h + 2 * i, r + i, 1, 0x8000, aa);
    }
}

// --------------------------------------------------------------------------
// Text
// --------------------------------------------------------------------------
static const uint8_t s_a4to32[16] = { 0, 2, 4, 6, 9, 11, 13, 15, 17, 19, 21, 23, 26, 28, 30, 32 };

static uint32_t utf8_next(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    uint32_t cp;
    if (s[0] < 0x80) { cp = s[0]; *ps += 1; }
    else if ((s[0] & 0xE0) == 0xC0 && s[1]) { cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); *ps += 2; }
    else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) { cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); *ps += 3; }
    else if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) { cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); *ps += 4; }
    else { cp = '?'; *ps += 1; }
    return cp;
}

static const FontGlyph *find_glyph(const FontData *fd, uint32_t cp) {
    int lo = 0, hi = fd->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t c = fd->glyphs[mid].code;
        if (c == cp) return &fd->glyphs[mid];
        if (c < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

static const FontGlyph *glyph_for(const FontData *fd, uint32_t cp) {
    const FontGlyph *g = find_glyph(fd, cp);
    if (!g) g = find_glyph(fd, '?');
    return g;
}

bool gfx_text_renderable(const char *s) {
    if (!s) return true;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        if (cp != ' ' && !find_glyph(&g_fonts[FONT_BODY], cp)) return false;
    }
    return true;
}

int gfx_font_height(FontId f) {
    return g_fonts[f].line_h;
}

int gfx_text_width(FontId f, const char *s) {
    if (!s) return 0;
    const FontData *fd = &g_fonts[f];
    int w = 0;
    while (*s) {
        const FontGlyph *g = glyph_for(fd, utf8_next(&s));
        if (g) w += g->adv;
    }
    return w;
}

static void draw_glyph(Canvas *c, const FontData *fd, const FontGlyph *g, int x, int y, Color col) {
    const uint8_t *bm = fd->bitmap + g->offset;
    int stride = (g->w + 1) >> 1;
    int gx = x + g->xoff, gy = y + g->yoff;
    for (int yy = 0; yy < g->h; yy++) {
        int py = gy + yy;
        if (py < c->cy0 || py >= c->cy1) continue;
        const uint8_t *row = bm + yy * stride;
        Color *dst = &c->px[py * c->w];
        for (int xx = 0; xx < g->w; xx++) {
            int a4 = (row[xx >> 1] >> ((xx & 1) * 4)) & 0xF;
            if (!a4) continue;
            int px = gx + xx;
            if (px < c->cx0 || px >= c->cx1) continue;
            dst[px] = gfx_blend(dst[px], col, s_a4to32[a4]);
        }
    }
}

int gfx_text(Canvas *c, FontId f, int x, int y, const char *s, Color col, int max_w) {
    if (!s) return 0;
    const FontData *fd = &g_fonts[f];
    int total = gfx_text_width(f, s);
    bool ellipsis = (max_w > 0 && total > max_w);
    const FontGlyph *dot = glyph_for(fd, 0x2026); // …
    int limit = ellipsis ? (max_w - (dot ? dot->adv : 0)) : 0x7FFF;
    int cx = x;
    while (*s) {
        const FontGlyph *g = glyph_for(fd, utf8_next(&s));
        if (!g) continue;
        if (ellipsis && (cx - x) + g->adv > limit) break;
        draw_glyph(c, fd, g, cx, y, col);
        cx += g->adv;
    }
    if (ellipsis && dot) {
        draw_glyph(c, fd, dot, cx, y, col);
        cx += dot->adv;
    }
    return cx - x;
}

int gfx_text_align(Canvas *c, FontId f, int x, int y, int w, const char *s, Color col, TextAlign align) {
    int tw = gfx_text_width(f, s);
    if (tw > w) tw = w;
    int sx = x;
    if (align == ALIGN_CENTER) sx = x + (w - tw) / 2;
    else if (align == ALIGN_RIGHT) sx = x + w - tw;
    return gfx_text(c, f, sx, y, s, col, w);
}

// Finds the end of the next wrapped line starting at s (returns pointer past the line)
static const char *wrap_line(const FontData *fd, const char *s, int w, bool last_line) {
    const char *p = s, *last_break = NULL;
    int width = 0;
    while (*p && *p != '\n') {
        const char *q = p;
        const FontGlyph *g = glyph_for(fd, utf8_next(&q));
        int adv = g ? g->adv : 0;
        if (width + adv > w) break;
        width += adv;
        if (*p == ' ' || *p == '-') last_break = q;
        p = q;
    }
    if (!*p || *p == '\n') return p;
    if (last_line) return s + strlen(s);           // gfx_text adds the ellipsis
    if (last_break) return last_break;
    if (p == s) { const char *q = p; utf8_next(&q); return q; } // single wide glyph
    return p;
}

int gfx_text_lines(FontId f, int w, int max_lines, const char *s) {
    if (!s || !*s) return 0;
    const FontData *fd = &g_fonts[f];
    int lines = 0;
    while (*s && lines < max_lines) {
        while (*s == ' ') s++;
        if (!*s) break;
        s = wrap_line(fd, s, w, lines == max_lines - 1);
        if (*s == '\n') s++;
        lines++;
    }
    return lines;
}

int gfx_text_wrap(Canvas *c, FontId f, int x, int y, int w, int max_lines, int line_h, const char *s, Color col) {
    if (!s) return 0;
    const FontData *fd = &g_fonts[f];
    if (line_h <= 0) line_h = fd->line_h;
    int lines = 0;
    char buf[200];
    while (*s && lines < max_lines) {
        while (*s == ' ') s++;
        if (!*s) break;
        const char *end = wrap_line(fd, s, w, lines == max_lines - 1);
        int len = (int)(end - s);
        if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, s, (size_t)len);
        buf[len] = '\0';
        while (len > 0 && buf[len - 1] == ' ') buf[--len] = '\0';
        gfx_text(c, f, x, y + lines * line_h, buf, col, w);
        lines++;
        s = end;
        if (*s == '\n') s++;
    }
    return lines;
}

// --------------------------------------------------------------------------
// Icons
// --------------------------------------------------------------------------
int gfx_icon_size(IconId id) {
    return (id >= 0 && id < ICON_COUNT) ? g_icons[id].size : 0;
}

void gfx_icon(Canvas *c, IconId id, int x, int y, Color col) {
    if (id < 0 || id >= ICON_COUNT) return;
    int n = g_icons[id].size;
    int stride = (n + 1) >> 1;
    const uint8_t *bm = g_icon_bitmap + g_icons[id].offset;
    for (int yy = 0; yy < n; yy++) {
        int py = y + yy;
        if (py < c->cy0 || py >= c->cy1) continue;
        for (int xx = 0; xx < n; xx++) {
            int a4 = (bm[yy * stride + (xx >> 1)] >> ((xx & 1) * 4)) & 0xF;
            if (!a4) continue;
            int px = x + xx;
            if (px < c->cx0 || px >= c->cx1) continue;
            Color *d = &c->px[py * c->w + px];
            *d = gfx_blend(*d, col, s_a4to32[a4]);
        }
    }
}

void gfx_icon_centered(Canvas *c, IconId id, int cx, int cy, Color col) {
    int n = gfx_icon_size(id);
    gfx_icon(c, id, cx - n / 2, cy - n / 2, col);
}

// --------------------------------------------------------------------------
// Images
// --------------------------------------------------------------------------
void gfx_blit(Canvas *c, int x, int y, const Color *src, int sw, int sh) {
    for (int yy = 0; yy < sh; yy++) {
        int py = y + yy;
        if (py < c->cy0 || py >= c->cy1) continue;
        for (int xx = 0; xx < sw; xx++) {
            int px = x + xx;
            if (px < c->cx0 || px >= c->cx1) continue;
            Color s = src[yy * sw + xx];
            if (s & 0x8000) c->px[py * c->w + px] = s;
        }
    }
}

static inline Color sample_box(const Color *src, int sw, int sh, int x0, int y0, int x1, int y1) {
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    u32 r = 0, g = 0, b = 0, n = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            Color p = src[y * sw + x];
            r += p & 0x1F; g += (p >> 5) & 0x1F; b += (p >> 10) & 0x1F; n++;
        }
    }
    if (!n) return 0x8000;
    return (Color)((r / n) | ((g / n) << 5) | ((b / n) << 10) | 0x8000);
}

static void blit_scaled_impl(Canvas *c, int dx, int dy, int dw, int dh, const Color *src, int sw, int sh, int radius) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    const uint8_t *cov = (radius > 0) ? corner_cov(radius > MAX_RADIUS ? MAX_RADIUS : radius) : NULL;
    if (radius > MAX_RADIUS) radius = MAX_RADIUS;
    bool shrink = (sw > dw) || (sh > dh);
    for (int yy = 0; yy < dh; yy++) {
        int py = dy + yy;
        if (py < c->cy0 || py >= c->cy1) continue;
        int sy0 = (yy * sh) / dh, sy1 = ((yy + 1) * sh) / dh;
        for (int xx = 0; xx < dw; xx++) {
            int px = dx + xx;
            if (px < c->cx0 || px >= c->cx1) continue;
            Color s;
            if (shrink) {
                int sx0 = (xx * sw) / dw, sx1 = ((xx + 1) * sw) / dw;
                s = sample_box(src, sw, sh, sx0, sy0, sx1, sy1);
            } else {
                s = src[((yy * sh) / dh) * sw + (xx * sw) / dw] | 0x8000;
            }
            int a = 32;
            if (cov) {
                int cx = -1, cy = -1;
                if (xx < radius) cx = xx; else if (xx >= dw - radius) cx = dw - 1 - xx;
                if (yy < radius) cy = yy; else if (yy >= dh - radius) cy = dh - 1 - yy;
                if (cx >= 0 && cy >= 0) a = cov[cy * radius + cx];
            }
            Color *d = &c->px[py * c->w + px];
            *d = (a >= 32) ? s : gfx_blend(*d, s, a);
        }
    }
}

void gfx_blit_scaled(Canvas *c, int dx, int dy, int dw, int dh, const Color *src, int sw, int sh) {
    blit_scaled_impl(c, dx, dy, dw, dh, src, sw, sh, 0);
}

void gfx_blit_scaled_round(Canvas *c, int dx, int dy, int dw, int dh, const Color *src, int sw, int sh, int r) {
    blit_scaled_impl(c, dx, dy, dw, dh, src, sw, sh, r);
}

void gfx_nds_icon(Canvas *c, int x, int y, const uint8_t *tiles, const uint16_t *pal, int scale) {
    if (!tiles || !pal || scale <= 0) return;
    for (int ty = 0; ty < 4; ty++) {
        for (int tx = 0; tx < 4; tx++) {
            const uint8_t *tile = &tiles[(ty * 4 + tx) * 32];
            for (int py = 0; py < 8; py++) {
                for (int px = 0; px < 8; px++) {
                    uint8_t v = tile[py * 4 + (px >> 1)];
                    int idx = (px & 1) ? (v >> 4) : (v & 0xF);
                    if (!idx) continue;
                    Color col = pal[idx] | 0x8000;
                    int ox = x + (tx * 8 + px) * scale, oy = y + (ty * 8 + py) * scale;
                    if (scale == 1) gfx_pixel(c, ox, oy, col);
                    else gfx_rect(c, ox, oy, scale, scale, col);
                }
            }
        }
    }
}

Color gfx_nds_icon_tint(const uint8_t *tiles, const uint16_t *pal) {
    if (!tiles || !pal) return C8(60, 60, 60);
    u32 r = 0, g = 0, b = 0, n = 0;
    for (int i = 0; i < 512; i++) {
        for (int k = 0; k < 2; k++) {
            int idx = k ? (tiles[i] >> 4) : (tiles[i] & 0xF);
            if (!idx) continue;
            Color p = pal[idx];
            int pr = p & 0x1F, pg = (p >> 5) & 0x1F, pb = (p >> 10) & 0x1F;
            // favour saturated pixels so the tint reflects the artwork, not outlines
            int mx = pr > pg ? (pr > pb ? pr : pb) : (pg > pb ? pg : pb);
            int mn = pr < pg ? (pr < pb ? pr : pb) : (pg < pb ? pg : pb);
            int w = 1 + (mx - mn);
            r += pr * w; g += pg * w; b += pb * w; n += w;
        }
    }
    if (!n) return C8(60, 60, 60);
    return (Color)((r / n) | ((g / n) << 5) | ((b / n) << 10) | 0x8000);
}


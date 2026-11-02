// Boot animation "Snap". Integer-only maths (the ARM9 has
// no FPU); time runs in milliseconds derived from the 60 fps frame counter.
#include "splash.h"
#include "gfx.h"
#include "ui.h"
#include "logo_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 256
#define SH 192
#define BLACK ((Color)0x8000)
#define WHITE C8(255, 255, 255)

// ---------------------------------------------------------------------------
// easing helpers, all in Q8 (0..256)
// ---------------------------------------------------------------------------
static int seg(int t, int s, int e) {
    if (t <= s) return 0;
    if (t >= e) return 256;
    return ((t - s) * 256) / (e - s);
}
static int e_out(int x) { int u = 256 - x; return 256 - (u * u / 256) * u / 256; }
static int e_in(int x) { return x * x / 256; }
static int e_back(int x) {           // overshoots ~10 % before settling
    int d = x - 256;                 // -256..0
    int d2 = d * d;                  // <= 65536
    return 256 + (29 * (d2 / 256) * d) / 2560 + (19 * d2) / 2560;
}
static int imin(int a, int b) { return a < b ? a : b; }
static int lerp(int a, int b, int q) { return a + ((b - a) * q) / 256; }

static void rrect(Canvas *c, int x, int y, int w, int h, int r, Color col, int a32) {
    if (w <= 0 || h <= 0 || a32 <= 0) return;
    int m = imin(w, h) / 2;
    if (r > m) r = m;
    if (r < 0) r = 0;
    gfx_round_rect(c, x, y, w, h, r, col, a32 > 32 ? 32 : a32);
}
// ---------------------------------------------------------------------------
// custom boot screen
// ---------------------------------------------------------------------------
static bool s_custom;
static int  s_custom_ms = 2500, s_custom_style;     // 0 fade, 1 flash

static bool read_pic(const char *path, Canvas *c) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[8];
    bool ok = fread(hdr, 1, 8, f) == 8 && memcmp(hdr, "HWP1", 4) == 0 &&
              (hdr[4] | (hdr[5] << 8)) == SW && (hdr[6] | (hdr[7] << 8)) == SH &&
              fread(c->px, sizeof(Color), SW * SH, f) == SW * SH;
    fclose(f);
    return ok;
}

bool splash_custom_available(const char *drive) {
    char p[96];
    snprintf(p, sizeof(p), "%s/_hyper/boot/top.bin", drive);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool splash_custom_load(const char *drive) {
    char p[96];
    s_custom = false;
    snprintf(p, sizeof(p), "%s/_hyper/boot/top.bin", drive);
    if (!read_pic(p, &g_top)) return false;
    snprintf(p, sizeof(p), "%s/_hyper/boot/bottom.bin", drive);
    if (!read_pic(p, &g_bot)) gfx_clear(&g_bot, BLACK);
    s_custom_ms = 2500;
    s_custom_style = 0;
    snprintf(p, sizeof(p), "%s/_hyper/boot/boot.cfg", drive);
    FILE *f = fopen(p, "r");
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "ms=", 3)) s_custom_ms = atoi(line + 3);
            else if (!strncmp(line, "style=flash", 11)) s_custom_style = 1;
        }
        fclose(f);
    }
    if (s_custom_ms < 800) s_custom_ms = 800;
    if (s_custom_ms > 5000) s_custom_ms = 5000;
    s_custom = true;
    return true;
}

void splash_custom_off(void) { s_custom = false; }
bool splash_is_custom(void) { return s_custom; }

int splash_fade_in(int f) {
    if (!s_custom || s_custom_style != 0) return 0;
    const int n = 18;                                  // 0.3 s
    return f >= n ? 0 : 16 - 16 * f / n;
}

int splash_frames(void) { return s_custom ? s_custom_ms * 60 / 1000 : 60 * 1900 / 1000; }
int splash_fade_frames(void) { return s_custom ? 16 : 10; }

#define LOGO_CYAN C8(0, 195, 227)

static void blit_logo(Canvas *c, const LogoBitmap *l, int x, int y, int a32) {
    for (int j = 0; j < l->size; j++)
        for (int i = 0; i < l->size; i++) {
            int a = l->alpha[j * l->size + i] * a32 / 32;
            if (a) gfx_pixel_a(c, x + i, y + j, (Color)l->px[j * l->size + i], a);
        }
}

// "DSplay": "DS" in the logo's cyan, "play" in the text colour of the theme
static void draw_wordmark(Canvas *c, int cx, int y, int a32) {
    const LogoWordmark *w = &g_wordmark;
    int x0 = cx - w->w / 2;
    for (int j = 0; j < w->h; j++)
        for (int i = 0; i < w->w; i++) {
            int a = w->alpha[j * w->w + i] * a32 / 32;
            if (a) gfx_pixel_a(c, x0 + i, y + j, i < w->split ? LOGO_CYAN : T.text, a);
        }
}

static void draw_snap(int t) {
    Canvas *a = &g_top, *b = &g_bot;
    Color bg = gfx_mix(BLACK, T.bg, seg(t, 0, 200));
    gfx_clear(a, bg);
    gfx_clear(b, bg);
    const Color coral = C8(255, 96, 88), dark = C8(20, 24, 28);
    const int pw = 112, ph = 88;                     // piece size (part of it hides past the hinge)
    int m = e_in(seg(t, 120, 540));
    bool hit = t >= 540;
    int shake = 0;
    if (hit && t < 660) {
        int k = 660 - t;                             // decays over 120 ms
        shake = ((t / 16) & 1 ? 2 : -2) * k / 120;
    }
    int glow = e_out(seg(t, 700, 1050));
    Color screen = gfx_mix(dark, T.accent_hi, glow);
    int px = (SW - pw) / 2;

    // upper half falls from above and lands on the hinge
    int ty = lerp(-ph, SH - ph + 14, m) + shake;
    gfx_shadow(a, px, ty, pw, ph, 12, 3, 14);
    rrect(a, px, ty, pw, ph, 12, T.accent, 32);
    rrect(a, px + 10, ty + 8, pw - 20, ph - 30, 5, screen, 32);
    // lower half rises from below
    int by = lerp(SH, -14, m) - shake;
    gfx_shadow(b, px, by, pw, ph, 12, 3, 14);
    rrect(b, px, by, pw, ph, 12, coral, 32);
    rrect(b, px + 10, by + 22, pw - 20, ph - 30, 5, screen, 32);

    if (glow > 0) {
        // upper screen: a tile with the DSplay logo pops up; lower screen: three loading dots
        int s = 40 * e_back(seg(t, 850, 1150)) / 256;
        int cy = ty + 8 + (ph - 30) / 2;
        if (s > 2) rrect(a, SW / 2 - s / 2, cy - s / 2, s, s, 9, WHITE, glow * 32 / 256);
        if (s >= 34) {
            int la = 32 * seg(t, 1000, 1150) / 256;
            blit_logo(a, &g_logo30, SW / 2 - 15, cy - 15, la * glow / 256);
        }
        // the name appears above the console
        int na = 32 * e_out(seg(t, 1000, 1300)) / 256;
        if (na > 0) draw_wordmark(a, SW / 2, 44 + (32 - na) / 4, na);
        int dy = by + 22 + (ph - 30) / 2;
        for (int i = 0; i < 3; i++) {
            int ph2 = ui_pulse(t * 3 / 50 - i * 6);
            gfx_circle(b, SW / 2 - 12 + i * 12, dy, 2 + ph2 / 200, gfx_mix(T.accent_hi, WHITE, ph2), glow * 32 / 256);
        }
    }
    if (hit) {
        // rings spread over both screens from the hinge, plus a short flash
        int k = seg(t, 540, 1150);
        int r = 170 * e_out(k) / 256, al = (256 - k) * 26 / 256;
        if (r > 6 && al > 0) {
            gfx_ring(a, SW / 2, SH + 8, r, 2, WHITE, al);
            gfx_ring(b, SW / 2, -8, r, 2, WHITE, al);
            if (r * 7 / 10 > 4) {
                gfx_ring(a, SW / 2, SH + 8, r * 7 / 10, 2, T.accent, al);
                gfx_ring(b, SW / 2, -8, r * 7 / 10, 2, T.accent, al);
            }
        }
    }
}

int splash_flash(int f) {
    if (s_custom) {
        if (s_custom_style != 1) return 0;
        return f >= 24 ? 0 : 16 - 16 * f / 24;        // comes out of white
    }
    int t = f * 50 / 3;
    if (t < 540 || t >= 660) return 0;
    return 10 - (t - 540) * 10 / 120;        // white flash done by the display hardware (free)
}

void splash_draw(int f) {
    if (s_custom) return;                              // the pictures are already in the buffers
    draw_snap(f * 50 / 3);
}

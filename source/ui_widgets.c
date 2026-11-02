#include "ui.h"
#include "library.h"
#include "cart_icons.h"
#include "romdb.h"
#include "i18n.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#ifdef ARM9
#include <nds.h>
#include <calico/nds/pm.h>
#endif


Theme T;

void theme_apply(int id) {
    if (id == 1) { // Basic White
        T.light = true;
        T.bg = C8(235, 235, 235);
        T.bg_top = C8(245, 245, 245);
        T.panel = C8(255, 255, 255);
        T.panel_hi = C8(246, 246, 246);
        T.tile = C8(255, 255, 255);
        T.tile_hi = C8(250, 250, 250);
        T.text = C8(45, 45, 45);
        T.text2 = C8(110, 110, 110);
        T.text3 = C8(160, 160, 160);
        T.divider = C8(205, 205, 205);
        T.accent = C8(0, 195, 227);
        T.accent_hi = C8(120, 236, 255);
        T.sel_text = C8(0, 140, 210);
        T.btn_bg = C8(255, 255, 255);
        T.btn_icon = C8(45, 45, 45);
        T.danger = C8(230, 50, 50);
        T.ok = C8(30, 170, 90);
        T.scrim = C8(30, 30, 30);
    } else { // Basic Black
        T.light = false;
        T.bg = C8(45, 45, 45);
        T.bg_top = C8(38, 38, 38);
        T.panel = C8(58, 58, 58);
        T.panel_hi = C8(70, 70, 70);
        T.tile = C8(70, 70, 70);
        T.tile_hi = C8(84, 84, 84);
        T.text = C8(255, 255, 255);
        T.text2 = C8(178, 178, 178);
        T.text3 = C8(125, 125, 125);
        T.divider = C8(82, 82, 82);
        T.accent = C8(0, 195, 227);
        T.accent_hi = C8(140, 245, 255);
        T.sel_text = C8(25, 210, 245);
        T.btn_bg = C8(72, 72, 72);
        T.btn_icon = C8(255, 255, 255);
        T.danger = C8(255, 80, 80);
        T.ok = C8(60, 210, 120);
        T.scrim = C8(0, 0, 0);
    }
}

// 64-step sine table (0..256)
static const uint16_t k_sine[64] = {
    128, 141, 153, 165, 177, 188, 199, 209, 218, 226, 234, 240, 245, 250, 253, 255,
    256, 255, 253, 250, 245, 240, 234, 226, 218, 209, 199, 188, 177, 165, 153, 141,
    128, 115, 103, 91, 79, 68, 57, 47, 38, 30, 22, 16, 11, 6, 3, 1,
    0, 1, 3, 6, 11, 16, 22, 30, 38, 47, 57, 68, 79, 91, 103, 115
};

int ui_pulse(int frame) { return k_sine[(frame / 2) & 63]; }

int ui_ease(int cur, int target, int div) {
    int d = target - cur;
    if (d >= -3 && d <= 3) return target;          // no long 1-pixel tail: every step is a full redraw
    int step = d / div;
    if (step > -3 && step < 3) step = (d > 0) ? 3 : -3;
    return cur + step;
}

void ui_scrim(Canvas *c, int a) {
    gfx_rect_a(c, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, T.scrim, a);
}

// --------------------------------------------------------------------------
void ui_format_clock(char *out, size_t len) {
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (tmv) snprintf(out, len, "%d:%02d", tmv->tm_hour, tmv->tm_min);
    else snprintf(out, len, "--:--");
}

void ui_format_date(uint32_t ts, char *out, size_t len) {
    time_t t = (time_t)ts;
    struct tm *tmv = localtime(&t);
    if (tmv) snprintf(out, len, "%02d.%02d.%04d", tmv->tm_mday, tmv->tm_mon + 1, tmv->tm_year + 1900);
    else snprintf(out, len, "-");
}

unsigned ui_battery_state(void) {
#ifdef ARM9
    return pmGetBatteryState();
#else
    return 15;
#endif
}

void ui_battery(Canvas *c, int x, int y) {
    int level = 15;
    bool charging = false;
#ifdef ARM9
    unsigned st = pmGetBatteryState();
    level = (int)PM_BATT_LEVEL(st);
    charging = (st & PM_BATT_CHARGING) != 0;
#endif
    int fill = (level * 14 + 14) / 15; // 0..14 px
    if (fill > 14) fill = 14;
    Color body = T.text;
    Color fc = (level <= 3 && !charging) ? T.danger : (charging ? T.ok : T.text);
    gfx_round_ring(c, x, y, 20, 10, 2, 1, body, 32);
    gfx_rect(c, x + 20, y + 3, 2, 4, body);
    if (fill > 0) gfx_round_rect(c, x + 2, y + 2, (fill * 16) / 14, 6, 1, fc, 32);
    if (charging) gfx_icon(c, ICON_BOLT_S, x - 10, y, T.ok);
}

// Profile pictures: <profile>/avatar.bin = "HAV1", u16 width, u16 height (64x64), RGB555 pixels.
// One picture per profile slot; the "current" slot is used by ui_avatar() (status bar).
#define AVATAR_SIZE 64
#define AV_SLOTS    4
#define AV_CACHE_N  6
#define AV_MAX_R    32
static Color *s_avatar_px[AV_SLOTS];
static int    s_av_cur = 0;
static Color  s_av_color;          // fallback circle color of the current profile
static bool   s_av_color_set = false;
// circle-masked, box-filtered copies (slot, radius) -> colour + coverage (0..32)
typedef struct { int slot, r; uint32_t use; Color px[AV_MAX_R * 2 * AV_MAX_R * 2]; uint8_t a[AV_MAX_R * 2 * AV_MAX_R * 2]; } AvCache;
static AvCache *s_avc;             // allocated on first use
static uint32_t s_av_tick;

static void av_cache_drop(int slot) {
    if (!s_avc) return;
    for (int i = 0; i < AV_CACHE_N; i++) if (s_avc[i].slot == slot) s_avc[i].slot = -1;
}

bool ui_avatar_load_slot(int slot, const char *path) {
    if (slot < 0 || slot >= AV_SLOTS) return false;
    free(s_avatar_px[slot]);
    s_avatar_px[slot] = NULL;
    av_cache_drop(slot);
    if (!path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[8];
    bool ok = fread(hdr, 1, 8, f) == 8 && memcmp(hdr, "HAV1", 4) == 0 &&
              (hdr[4] | (hdr[5] << 8)) == AVATAR_SIZE && (hdr[6] | (hdr[7] << 8)) == AVATAR_SIZE;
    Color *px = NULL;
    if (ok) {
        px = malloc(AVATAR_SIZE * AVATAR_SIZE * sizeof(Color));
        ok = px && fread(px, sizeof(Color), AVATAR_SIZE * AVATAR_SIZE, f) == AVATAR_SIZE * AVATAR_SIZE;
    }
    fclose(f);
    if (!ok) { free(px); return false; }
    s_avatar_px[slot] = px;
    return true;
}

bool ui_avatar_load(const char *path) { return ui_avatar_load_slot(s_av_cur, path); }
void ui_avatar_select(int slot, Color fallback) {
    if (slot >= 0 && slot < AV_SLOTS) s_av_cur = slot;
    s_av_color = fallback;
    s_av_color_set = true;
}
bool ui_avatar_present(void) { return s_avatar_px[s_av_cur] != NULL; }

static AvCache *avatar_build(int slot, int r) {
    if (!s_avc) {
        s_avc = malloc(sizeof(AvCache) * AV_CACHE_N);
        if (!s_avc) return NULL;
        for (int i = 0; i < AV_CACHE_N; i++) { s_avc[i].slot = -1; s_avc[i].use = 0; }
    }
    s_av_tick++;
    int lru = 0;
    for (int i = 0; i < AV_CACHE_N; i++) {
        if (s_avc[i].slot == slot && s_avc[i].r == r) { s_avc[i].use = s_av_tick; return &s_avc[i]; }
        if (s_avc[i].use < s_avc[lru].use) lru = i;
    }
    AvCache *e = &s_avc[lru];
    const Color *src = s_avatar_px[slot];
    int d = r * 2;
    for (int y = 0; y < d; y++) {
        for (int x = 0; x < d; x++) {
            // box-filter the 64x64 picture down to d x d (nearest when enlarging)
            int sx0 = x * AVATAR_SIZE / d, sx1 = (x + 1) * AVATAR_SIZE / d;
            int sy0 = y * AVATAR_SIZE / d, sy1 = (y + 1) * AVATAR_SIZE / d;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            u32 rr = 0, gg = 0, bb = 0, n = 0;
            for (int yy = sy0; yy < sy1; yy++)
                for (int xx = sx0; xx < sx1; xx++) {
                    Color p = src[yy * AVATAR_SIZE + xx];
                    rr += p & 31; gg += (p >> 5) & 31; bb += (p >> 10) & 31; n++;
                }
            e->px[y * d + x] = (Color)(0x8000 | (rr / n) | ((gg / n) << 5) | ((bb / n) << 10));
            // coverage of a circle of radius r, 4x4 supersampled
            int cov = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    int px = x * 8 + sx * 2 + 1 - r * 8, py = y * 8 + sy * 2 + 1 - r * 8;
                    if (px * px + py * py <= r * r * 64) cov++;
                }
            e->a[y * d + x] = (uint8_t)(cov * 2);
        }
    }
    e->slot = slot; e->r = r; e->use = s_av_tick;
    return e;
}

static Color name_color(const char *name) {
    uint32_t h = 5381;
    for (const char *p = name; p && *p; p++) h = h * 33 + (uint8_t)*p;
    static const Color pal[] = { C8(230, 60, 60), C8(0, 160, 230), C8(80, 190, 90), C8(240, 150, 30),
                                 C8(150, 90, 220), C8(240, 90, 160), C8(0, 175, 160) };
    return pal[h % (sizeof(pal) / sizeof(pal[0]))];
}

void ui_avatar_slot(Canvas *c, int slot, int cx, int cy, int r, const char *name, Color fallback) {
    if (slot >= 0 && slot < AV_SLOTS && s_avatar_px[slot] && r > 0 && r <= AV_MAX_R) {
        AvCache *e = avatar_build(slot, r);
        if (e) {
            int d = r * 2;
            for (int y = 0; y < d; y++)
                for (int x = 0; x < d; x++) {
                    int a = e->a[y * d + x];
                    if (a) gfx_pixel_a(c, cx - r + x, cy - r + y, e->px[y * d + x], a > 32 ? 32 : a);
                }
            gfx_ring(c, cx, cy, r, 1, C8(255, 255, 255), 10);
            return;
        }
    }
    gfx_circle(c, cx, cy, r, fallback, 32);
    gfx_ring(c, cx, cy, r, 1, C8(255, 255, 255), 10);
    char initial[5] = { 0 };
    if (name && name[0]) {
        int n = 1;
        unsigned char b = (unsigned char)name[0];
        if (b >= 0xF0) n = 4; else if (b >= 0xE0) n = 3; else if (b >= 0xC0) n = 2;
        memcpy(initial, name, (size_t)n);
        if (n == 1 && initial[0] >= 'a' && initial[0] <= 'z') initial[0] = (char)(initial[0] - 32);
    } else {
        initial[0] = '?';
    }
    FontId f = r >= 26 ? FONT_HUGE : r >= 16 ? FONT_TITLE : r >= 12 ? FONT_BOLD : FONT_SMALL;
    int fh = g_fonts[f].ascent;
    gfx_text_align(c, f, cx - r, cy - fh / 2 - 1, r * 2, initial, C8(255, 255, 255), ALIGN_CENTER);
}

void ui_avatar(Canvas *c, int cx, int cy, int r, const char *name) {
    ui_avatar_slot(c, s_av_cur, cx, cy, r, name, s_av_color_set ? s_av_color : name_color(name));
}

// Wallpaper (top screen): <profile>/wallpaper.bin = "HWP1", u16 256, u16 192, RGB555 pixels.
// A softened copy (blended towards the theme background) keeps text readable.
static Color *s_wp_raw, *s_wp;

void ui_wallpaper_retint(void) {
    if (!s_wp_raw) return;
    if (!s_wp) s_wp = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
    if (!s_wp) return;
    Color base = T.bg_top;
    int amt = T.light ? 15 : 13;   // 0..32 towards the background
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) s_wp[i] = gfx_blend(s_wp_raw[i] | 0x8000, base, amt);
}

bool ui_wallpaper_load(const char *path) {
    free(s_wp_raw); s_wp_raw = NULL;
    free(s_wp); s_wp = NULL;
    if (!path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[8];
    bool ok = fread(hdr, 1, 8, f) == 8 && memcmp(hdr, "HWP1", 4) == 0 &&
              (hdr[4] | (hdr[5] << 8)) == SCREEN_WIDTH && (hdr[6] | (hdr[7] << 8)) == SCREEN_HEIGHT;
    if (ok) {
        s_wp_raw = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
        ok = s_wp_raw && fread(s_wp_raw, sizeof(Color), SCREEN_WIDTH * SCREEN_HEIGHT, f) == SCREEN_WIDTH * SCREEN_HEIGHT;
    }
    fclose(f);
    if (!ok) { free(s_wp_raw); s_wp_raw = NULL; return false; }
    ui_wallpaper_retint();
    return true;
}

bool ui_wallpaper_present(void) { return s_wp != NULL; }

void ui_wallpaper_draw(Canvas *c) {
    if (s_wp) memcpy(c->px, s_wp, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
}

// Profile accent color on top of the theme
void theme_set_accent(Color accent, Color hi, Color sel_light, Color sel_dark) {
    T.accent = accent;
    T.accent_hi = hi;
    T.sel_text = T.light ? sel_light : sel_dark;
}

static int s_lv_kind, s_lv_val, s_lv_max = 1, s_lv_a;
void ui_set_level(int kind, int value, int max, int alpha) {
    s_lv_kind = kind; s_lv_val = value; s_lv_max = max > 0 ? max : 1; s_lv_a = alpha;
}

static void level_pill(Canvas *c) {
    const int w = 74, h = 16, x = 128 - w / 2 + 6, y = 3;
    int a = s_lv_a > 32 ? 32 : s_lv_a;
    gfx_round_rect(c, x, y, w, h, h / 2, T.panel_hi, a);
    if (a > 12) gfx_icon(c, s_lv_kind ? ICON_SUN : ICON_SPEAKER, x + 3, y, s_lv_val ? T.text : T.text3);
    int bx = x + 22, bw = w - 30, by = y + h / 2 - 2;
    gfx_round_rect(c, bx, by, bw, 4, 2, T.divider, a);
    int fw = bw * s_lv_val / s_lv_max;
    if (fw > 0) gfx_round_rect(c, bx, by, fw < 4 ? 4 : fw, 4, 2, T.accent, a);
}

void ui_status_bar(Canvas *c, const SystemSettings *cfg) {
    const char *name = (cfg && cfg->profile_name[0]) ? cfg->profile_name : "DSplay";
    ui_avatar(c, 16, 12, 9, name);
    gfx_text(c, FONT_BODY, 30, 5, name, T.text, s_lv_a > 0 ? 65 : 110);
    if (s_lv_a > 0) level_pill(c);
    char clock[16];
    ui_format_clock(clock, sizeof(clock));
    int tw = gfx_text_width(FONT_BODY, clock);
    gfx_text(c, FONT_BODY, 220 - tw - 8, 5, clock, T.text, 0);
    if (library_scanning() && s_lv_a <= 0) gfx_icon(c, ICON_RESTORE, 220 - tw - 26, 6, T.text2); // library is being updated
    ui_battery(c, 228, 7);
}

// The DS has START and SELECT (no + / -): drawn as a small pill with the key's name
static const char *pill_label(IconId icon) {
    return icon == BTN_START ? "START" : icon == BTN_SELECT ? "SELECT" : NULL;
}

int ui_button_width(IconId icon) {
    const char *pl = pill_label(icon);
    if (pl) return gfx_text_width(FONT_SMALL, pl) + 6;
    return icon < ICON_COUNT ? gfx_icon_size(icon) : 0;
}

void ui_button_glyph(Canvas *c, IconId icon, int x, int y, Color col) {
    const char *pl = pill_label(icon);
    if (!pl) { gfx_icon(c, icon, x, y, col); return; }
    int w = ui_button_width(icon);
    gfx_round_rect(c, x, y, w, 11, 5, col, 32);
    gfx_text(c, FONT_SMALL, x + 3, y, pl, T.bg, 0);
}

int ui_hint_bar(Canvas *c, const Hint *hints, int n, HitRect *rects) {
    gfx_rect(c, 0, HINT_BAR_Y, SCREEN_WIDTH, HINT_BAR_H, T.bg);
    gfx_rect(c, 8, HINT_BAR_Y, SCREEN_WIDTH - 16, 1, T.divider);
    int x = SCREEN_WIDTH - 10;
    int count = 0;
    for (int i = n - 1; i >= 0; i--) {
        int tw = gfx_text_width(FONT_SMALL, hints[i].text);
        int iw = (hints[i].icon < ICON_COUNT) ? ui_button_width(hints[i].icon) + 3 : 0;
        int w = iw + tw;
        x -= w;
        int y = HINT_BAR_Y + 5;
        if (iw) ui_button_glyph(c, hints[i].icon, x, y, T.text);
        gfx_text(c, FONT_SMALL, x + iw, y, hints[i].text, T.text, 0);
        if (rects) {
            rects[count].x = x - 4;
            rects[count].y = HINT_BAR_Y;
            rects[count].w = w + 8;
            rects[count].h = HINT_BAR_H;
            rects[count].key = hints[i].key;
            count++;
        }
        x -= 12;
    }
    return count;
}

// Selection frames are recorded while a screen is rendered and drawn separately,
// so the pulse animation only touches a few rows each frame.
UiRing ui_rings_top[UI_MAX_RINGS], ui_rings_bot[UI_MAX_RINGS];
int    ui_rings_top_n, ui_rings_bot_n;

void ui_rings_reset(Canvas *c) {
    if (c == &g_top) ui_rings_top_n = 0;
    else ui_rings_bot_n = 0;
}

void ui_ring_draw(const UiRing *rg, int frame) {
    int p = ui_pulse(frame);
    Color col = gfx_mix(T.accent, T.accent_hi, p);
    Canvas *c = rg->c;
    int ox0 = c->cx0, oy0 = c->cy0, ox1 = c->cx1, oy1 = c->cy1;
    c->cx0 = rg->cx0; c->cy0 = rg->cy0; c->cx1 = rg->cx1; c->cy1 = rg->cy1; // e.g. the scrolling grid
    gfx_round_ring(c, rg->x, rg->y, rg->w, rg->h, rg->r, 3, col, 32);
    c->cx0 = ox0; c->cy0 = oy0; c->cx1 = ox1; c->cy1 = oy1;
}

void ui_sel_frame(Canvas *c, int x, int y, int w, int h, int r, int frame) {
    // Animated two-tone cyan frame, 3 px wide with a 1 px gap around the content
    UiRing rg = { c, x - 4, y - 4, w + 8, h + 8, r + 4, c->cx0, c->cy0, c->cx1, c->cy1 };
    UiRing *list = (c == &g_top) ? ui_rings_top : ui_rings_bot;
    int *n = (c == &g_top) ? &ui_rings_top_n : &ui_rings_bot_n;
    if (*n < UI_MAX_RINGS) list[(*n)++] = rg;
    (void)frame;
}

void ui_spinner(Canvas *c, int cx, int cy, int r, int frame, Color col) {
    static const int8_t dx[8] = { 0, 7, 10, 7, 0, -7, -10, -7 };
    static const int8_t dy[8] = { -10, -7, 0, 7, 10, 7, 0, -7 };
    int head = (frame / 4) & 7;
    for (int i = 0; i < 8; i++) {
        int age = (head - i) & 7;
        int a = 32 - age * 4;
        if (a < 4) a = 4;
        gfx_circle(c, cx + (dx[i] * r) / 10, cy + (dy[i] * r) / 10, r >= 10 ? 2 : 1, col, a);
    }
}

int ui_pill(Canvas *c, int x, int y, FontId f, const char *text, Color bg, Color fg) {
    int tw = gfx_text_width(f, text);
    int h = g_fonts[f].line_h + 2;
    int w = tw + h;
    gfx_round_rect(c, x, y, w, h, h / 2, bg, 32);
    gfx_text(c, f, x + h / 2, y + 1, text, fg, 0);
    return w;
}

void ui_round_button(Canvas *c, int cx, int cy, int r, IconId icon, Color icon_col, bool focused, int frame) {
    if (T.light) gfx_circle(c, cx, cy + 1, r + 1, C8(0, 0, 0), 4);
    gfx_circle(c, cx, cy, r, T.btn_bg, 32);
    gfx_icon_centered(c, icon, cx, cy, icon_col);
    if (focused) ui_sel_frame(c, cx - r, cy - r, 2 * r, 2 * r, r, frame);
}

// --------------------------------------------------------------------------
// Game artwork
// --------------------------------------------------------------------------
Color ui_system_color(ItemType t, bool end) {
    switch (t) {
        case TYPE_GBA:     return end ? C8(46, 28, 120) : C8(98, 66, 204);
        case TYPE_GB:      return end ? C8(62, 82, 32) : C8(140, 162, 64);
        case TYPE_GBC:     return end ? C8(0, 92, 118) : C8(0, 170, 176);
        case TYPE_NES:     return end ? C8(110, 18, 28) : C8(214, 44, 44);
        case TYPE_SNES:    return end ? C8(70, 66, 96) : C8(150, 142, 182);
        case TYPE_GENESIS: return end ? C8(12, 18, 56) : C8(38, 64, 160);
        case TYPE_SMS:     return end ? C8(8, 36, 104) : C8(30, 96, 210);
        case TYPE_GG:      return end ? C8(16, 16, 16) : C8(64, 64, 72);
        case TYPE_SLOT1:   return end ? C8(40, 40, 46) : C8(96, 96, 104);
        default:           return end ? C8(44, 44, 52) : C8(96, 96, 108);
    }
}

// ---------------------------------------------------------------------------
// Tile art is rendered once into a cache (square, no corners) and blitted with
// anti-aliased rounded corners afterwards.
// ---------------------------------------------------------------------------
#define TC_SLOTS_MAX 48      // DSi: enough for HOME + a full All Software page + the hero
static int s_tc_slots = 24;
#define TC_SLOTS s_tc_slots
#define TC_MAX   96

typedef struct {
    int      idx, size, theme, cov;
    uint32_t stamp;
    Color    px[TC_MAX * TC_MAX];
} TileSlot;

static TileSlot *s_tc = NULL;
static uint32_t  s_tc_stamp = 1;

void ui_tile_cache_reset(void) {
    if (!s_tc) {
#ifdef ARM9
        s_tc_slots = isDSiMode() ? TC_SLOTS_MAX : 24;
#else
        s_tc_slots = TC_SLOTS_MAX;
#endif
        s_tc = (TileSlot *)calloc((size_t)s_tc_slots, sizeof(TileSlot));
        if (!s_tc) { s_tc_slots = 24; s_tc = (TileSlot *)calloc(24, sizeof(TileSlot)); }
    }
    if (!s_tc) return;
    for (int i = 0; i < TC_SLOTS; i++) { s_tc[i].idx = -999; s_tc[i].stamp = 0; }
}

static void retro_art(Canvas *c, const GameItem *it, int size) {
    ItemType t = (ItemType)it->type;
    gfx_vgradient(c, 0, 0, size, size, ui_system_color(t, false), ui_system_color(t, true));
    gfx_rect_a(c, size / 2, 2, size / 2 - 2, 1, C8(255, 255, 255), 6);
    const char *sys = romdb_system_short(t);
    FontId tf = size >= 80 ? FONT_TITLE : (size >= 60 ? FONT_BOLD : FONT_SMALL);
    int pad = size >= 60 ? 7 : 5;
    gfx_text(c, FONT_SMALL, pad, pad - 1, sys, C8(255, 255, 255), size - pad * 2);
    gfx_rect_a(c, pad, pad + 11, 12, 2, C8(255, 255, 255), 20);
    int lh = g_fonts[tf].line_h;
    int lines_max = size >= 60 ? 3 : 2;
    int avail = size - pad * 2;
    int lines = gfx_text_lines(tf, avail, lines_max, it->title);
    if (lines < 1) lines = 1;
    int ty = size - pad - lines * lh + 2;
    gfx_text_wrap(c, tf, pad, ty, avail, lines_max, lh, it->title, C8(255, 255, 255));
}

static void icon_to_buffer(const GameItem *it, Color *buf) {
    for (int ty = 0; ty < 4; ty++)
        for (int tx = 0; tx < 4; tx++)
            for (int py = 0; py < 8; py++)
                for (int px = 0; px < 8; px++) {
                    uint8_t v = it->icon_data[(ty * 4 + tx) * 32 + py * 4 + (px >> 1)];
                    int idx = (px & 1) ? (v >> 4) : (v & 0xF);
                    buf[(ty * 8 + py) * 32 + tx * 8 + px] = idx ? (it->icon_palette[idx] | 0x8000) : 0;
                }
}

// Draws a 32x32 icon (bit 15 = opaque) at d x d pixels: exact 1x/2x/3x pixel scaling,
// and a smooth 1.5x (each 2 source pixels -> 3, the middle one blended) for 48 px
static void blit_icon(Canvas *c, const Color *src, int ox, int oy, int d) {
    if (d == 48) {
        static const uint8_t map0[3] = { 0, 0, 1 }, map1[3] = { 0, 1, 1 };
        for (int yy = 0; yy < 48; yy++) {
            int by = (yy / 3) * 2, ry = yy % 3;
            int sy0 = by + map0[ry], sy1 = by + map1[ry];
            Color *drow = c->px + (oy + yy) * c->w + ox;
            for (int xx = 0; xx < 48; xx++) {
                int bx = (xx / 3) * 2, rx = xx % 3;
                int sx0 = bx + map0[rx], sx1 = bx + map1[rx];
                Color s4[4] = { src[sy0 * 32 + sx0], src[sy0 * 32 + sx1], src[sy1 * 32 + sx0], src[sy1 * 32 + sx1] };
                int r = 0, g = 0, b = 0, n = 0;
                for (int k = 0; k < 4; k++) {
                    if (!(s4[k] & 0x8000)) continue;
                    r += s4[k] & 31; g += (s4[k] >> 5) & 31; b += (s4[k] >> 10) & 31; n++;
                }
                if (n >= 2 || (n == 1 && rx != 1 && ry != 1)) drow[xx] = (Color)(0x8000 | (r / n) | ((g / n) << 5) | ((b / n) << 10));
            }
        }
        return;
    }
    int f = d / 32;
    if (f < 1) f = 1;
    for (int yy = 0; yy < 32 * f; yy++) {
        const Color *srow = src + (yy / f) * 32;
        Color *drow = c->px + (oy + yy) * c->w + ox;
        for (int xx = 0; xx < 32 * f; xx++) {
            Color p = srow[xx / f];
            if (p & 0x8000) drow[xx] = p;
        }
    }
}

// icon size for a tile: 3x for the 96 px hero, 2x for 64+ px tiles, 1.5x for grid tiles
static int icon_size_for(int size) {
    return size >= 96 ? 96 : (size >= 64 ? 64 : (size >= 48 ? 48 : 32));
}

static void nds_icon_art(Canvas *c, const GameItem *it, int size) {
    Color tint = gfx_nds_icon_tint(it->icon_data, it->icon_palette);
    Color top = T.light ? gfx_mix(C8(255, 255, 255), tint, 70) : gfx_mix(C8(70, 70, 70), tint, 110);
    Color bot = T.light ? gfx_mix(C8(225, 225, 225), tint, 110) : gfx_mix(C8(30, 30, 30), tint, 80);
    gfx_vgradient(c, 0, 0, size, size, top, bot);
    int d = icon_size_for(size);
    static Color buf[32 * 32];
    icon_to_buffer(it, buf);
    blit_icon(c, buf, (size - d) / 2, (size - d) / 2, d);
}

static void cart_art(Canvas *c, ItemType t, const Color *icon, int size) {
    gfx_vgradient(c, 0, 0, size, size, ui_system_color(t, false), ui_system_color(t, true));
    gfx_rect_a(c, size / 2, 2, size / 2 - 2, 1, C8(255, 255, 255), 6);
    int d = icon_size_for(size);
    int o = (size - d) / 2;
    blit_icon(c, icon, o, o, d);
    // the system name on the label, drawn with the UI font so it stays sharp
    int x0, y0, x1, y1;
    const char *name;
    if (d < 48 || !cart_label(t, &x0, &y0, &x1, &y1, &name)) return;
    int lx = o + x0 * d / 32, ly = o + y0 * d / 32;
    int lw = (x1 - x0 + 1) * d / 32, lh = (y1 - y0 + 1) * d / 32;
    bool ds = (t == TYPE_NDS || t == TYPE_SLOT1);
    Color stripe = ds ? C8(220, 40, 40) : ui_system_color(t, false);
    Color ink = ds ? C8(52, 54, 60) : ui_system_color(t, true);
    int sh = d >= 96 ? 5 : d >= 64 ? 4 : 3;
    gfx_rect(c, lx + 1, ly + 1, lw - 2, sh, stripe);
    FontId f = FONT_SMALL;
    if (lh - sh >= 22 && gfx_text_width(FONT_TITLE, name) <= lw - 4) f = FONT_TITLE;
    else if (lh - sh >= 14 && gfx_text_width(FONT_BOLD, name) <= lw - 2) f = FONT_BOLD;
    int fh = g_fonts[f].ascent;
    int ty = ly + sh + (lh - sh - fh) / 2;
    gfx_text_align(c, f, lx, ty, lw, name, ink, ALIGN_CENTER);
}

static bool has_icon(const GameItem *it) {
    for (int i = 0; i < ICON_TILE_BYTES; i++) if (it->icon_data[i]) return true;
    return false;
}

static void render_art(Canvas *c, const GameItem *it, const Color *cover, int size) {
    if (cover) {
        gfx_blit_scaled(c, 0, 0, size, size, cover, COVER_SIZE, COVER_SIZE);
    } else if ((it->type == TYPE_NDS || it->type == TYPE_SLOT1 || it->type == TYPE_APP) && has_icon(it)) {
        nds_icon_art(c, it, size);
    } else if (cart_icon((ItemType)it->type)) {
        cart_art(c, (ItemType)it->type, cart_icon((ItemType)it->type), size);
    } else {
        retro_art(c, it, size);
    }
}

// Box art of the inserted game card (loaded by the app from _hyper/covers/<CODE>.png)
static const Color *s_card_cover;
void ui_set_card_cover(const Color *px) { s_card_cover = px; }

const Color *ui_card_cover(void) { return s_card_cover; }

// Box art on the upper screen: the whole picture, fitted into size x size and centered
// (covers.c keeps the aspect ratio; the unused border is transparent)
bool ui_cover(Canvas *c, const Color *cover, int x, int y, int size, int radius) {
    if (!cover) return false;
    int x0 = COVER_SIZE, y0 = COVER_SIZE, x1 = -1, y1 = -1;
    for (int yy = 0; yy < COVER_SIZE; yy++)
        for (int xx = 0; xx < COVER_SIZE; xx++)
            if (cover[yy * COVER_SIZE + xx] & 0x8000) {
                if (xx < x0) x0 = xx;
                if (xx > x1) x1 = xx;
                if (yy < y0) y0 = yy;
                if (yy > y1) y1 = yy;
            }
    if (x1 < x0 || y1 < y0) return false;
    int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    static Color tmp[COVER_SIZE * COVER_SIZE];
    for (int yy = 0; yy < ch; yy++) memcpy(&tmp[yy * cw], &cover[(y0 + yy) * COVER_SIZE + x0], sizeof(Color) * (size_t)cw);
    int dw = cw * size / COVER_SIZE, dh = ch * size / COVER_SIZE;
    int dx = x + (size - dw) / 2, dy = y + (size - dh) / 2;
    gfx_shadow(c, dx, dy, dw, dh, radius, 4, 24);
    if (dw == cw && dh == ch) gfx_blit_round(c, dx, dy, tmp, cw, ch, radius);
    else gfx_blit_scaled_round(c, dx, dy, dw, dh, tmp, cw, ch, radius);
    return true;
}

// One frame of an animated icon: drawn every time, never cached
void ui_tile_uncached(Canvas *c, const GameItem *it, int x, int y, int size, int radius) {
    if (!it || size > TC_MAX) return;
    if (T.light) gfx_shadow(c, x, y, size, size, radius, 2, 14);
    static Color tmp[TC_MAX * TC_MAX];
    Canvas tc;
    gfx_canvas_init(&tc, tmp, size, size);
    render_art(&tc, it, NULL, size);
    gfx_blit_round(c, x, y, tmp, size, size, radius);
}

void ui_tile(Canvas *c, int lib_idx, const GameItem *it, int x, int y, int size, int radius) {
    if (!it) return;
    if (T.light) gfx_shadow(c, x, y, size, size, radius, 2, 14);
    if (!s_tc) ui_tile_cache_reset();
    const Color *cover = NULL;             // tiles show the game's own icon; box art is for the upper screen
    int cov = 0;
    int theme = T.light ? 1 : 0;
    if (size > TC_MAX || !s_tc) {
        static Color tmp[TC_MAX * TC_MAX];
        Canvas tc;
        gfx_canvas_init(&tc, tmp, size > TC_MAX ? TC_MAX : size, size > TC_MAX ? TC_MAX : size);
        render_art(&tc, it, cover, tc.w);
        gfx_blit_round(c, x, y, tmp, tc.w, tc.h, radius);
        return;
    }
    TileSlot *hit = NULL;
    for (int i = 0; i < TC_SLOTS; i++) {
        TileSlot *t = &s_tc[i];
        if (t->idx == lib_idx && t->size == size && t->theme == theme && t->cov == cov) { hit = t; break; }
    }
    if (!hit) {
        hit = &s_tc[0];
        for (int i = 1; i < TC_SLOTS; i++) if (s_tc[i].stamp < hit->stamp) hit = &s_tc[i];
        Canvas tc;
        gfx_canvas_init(&tc, hit->px, size, size);
        render_art(&tc, it, cover, size);
        hit->idx = (lib_idx >= 0) ? lib_idx : -999; // the game card is never cached
        hit->size = size;
        hit->theme = theme;
        hit->cov = cov;
    }
    hit->stamp = s_tc_stamp++;
    gfx_blit_round(c, x, y, hit->px, size, size, radius);
}

void ui_tile_allsoft(Canvas *c, int x, int y, int size, int radius, const char *label) {
    if (T.light) gfx_shadow(c, x, y, size, size, radius, 3, 14);
    gfx_round_rect(c, x, y, size, size, radius, T.tile, 32);
    gfx_icon_centered(c, size >= 60 ? ICON_GRID_L : ICON_GRID, x + size / 2, y + size / 2 - 7, T.text);
    gfx_text_align(c, FONT_SMALL, x + 2, y + size - 18, size - 4, label, T.text2, ALIGN_CENTER);
}

void ui_thumb(Canvas *c, int lib_idx, const GameItem *it, int x, int y, int size) {
    // Small list thumbnail: the game's icon / system badge (box art is for the upper screen)
    (void)lib_idx;
    if ((it->type == TYPE_NDS || it->type == TYPE_SLOT1 || it->type == TYPE_APP) && has_icon(it)) {
        gfx_round_rect(c, x, y, size, size, 3, T.tile, 32);
        static Color buf[32 * 32];
        for (int ty = 0; ty < 4; ty++)
            for (int tx = 0; tx < 4; tx++)
                for (int py = 0; py < 8; py++)
                    for (int px = 0; px < 8; px++) {
                        uint8_t v = it->icon_data[(ty * 4 + tx) * 32 + py * 4 + (px >> 1)];
                        int idx = (px & 1) ? (v >> 4) : (v & 0xF);
                        buf[(ty * 8 + py) * 32 + tx * 8 + px] = idx ? (it->icon_palette[idx] | 0x8000) : (T.tile | 0x8000);
                    }
        gfx_blit_scaled(c, x + 1, y + 1, size - 2, size - 2, buf, 32, 32);
        return;
    }
    ItemType t = (ItemType)it->type;
    const Color *ci = cart_icon(t);
    if (ci) {
        static Color cbuf[32 * 32];
        Color bg = ui_system_color(t, false);
        for (int i = 0; i < 32 * 32; i++) cbuf[i] = (ci[i] & 0x8000) ? ci[i] : (Color)(bg | 0x8000);
        gfx_round_rect(c, x, y, size, size, 3, bg, 32);
        gfx_blit_scaled(c, x + 1, y + 1, size - 2, size - 2, cbuf, 32, 32);
        return;
    }
    gfx_round_rect(c, x, y, size, size, 3, ui_system_color(t, false), 32);
    // first letter of the title as a monogram
    char mono[5] = { 0 };
    const unsigned char *p = (const unsigned char *)it->title;
    int n = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
    if (*p) memcpy(mono, p, (size_t)n);
    FontId f = size >= 20 ? FONT_BOLD : FONT_SMALL;
    gfx_text_align(c, f, x, y + (size - g_fonts[f].ascent) / 2 - 1, size, mono, C8(255, 255, 255), ALIGN_CENTER);
}


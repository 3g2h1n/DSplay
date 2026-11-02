#ifndef UI_H
#define UI_H

// DSplay visual language: Switch-inspired palette, tiles, round buttons,
// hint bar and dialogs drawn with the gfx renderer.

#include "gfx.h"
#include "covers.h"
#include "stats.h"

typedef struct {
    Color bg, bg_top, panel, panel_hi, tile, tile_hi;
    Color text, text2, text3, divider;
    Color accent, accent_hi, sel_text;
    Color btn_bg, btn_icon, danger, ok, scrim;
    bool  light;
} Theme;

extern Theme T;

void theme_apply(int theme_id);

// Animation helpers
int  ui_pulse(int frame);                 // 0..256 triangle-ish pulse
int  ui_ease(int cur, int target, int div); // exponential approach

// Common layout
#define HINT_BAR_Y   172
#define HINT_BAR_H   20

typedef struct {
    IconId icon;   // BTN_A ... or ICON_COUNT for none
    const char *text;
    int key;       // KEY_* emitted when tapped
} Hint;

typedef struct {
    int x, y, w, h;
    int key;
} HitRect;

// Draws the hint bar; returns number of hit rects written (max n)
void ui_tile_uncached(Canvas *c, const GameItem *it, int x, int y, int size, int radius);
// Upper screen box art, whole picture centered in size x size (false: no cover)
bool ui_cover(Canvas *c, const Color *cover, int x, int y, int size, int radius);
const Color *ui_card_cover(void);
int  ui_button_width(IconId icon);                         // hint-bar key glyph (START / SELECT as pills)
void ui_button_glyph(Canvas *c, IconId icon, int x, int y, Color col);
int  ui_hint_bar(Canvas *c, const Hint *hints, int n, HitRect *rects);
void ui_status_bar(Canvas *c, const SystemSettings *cfg);
// Volume / brightness indicator in the status bar, between the name and the clock
// (kind 0 = volume, 1 = brightness; alpha 0 = hidden, 32 = full)
void ui_set_level(int kind, int value, int max, int alpha);
void ui_battery(Canvas *c, int x, int y);
unsigned ui_battery_state(void);
void ui_avatar(Canvas *c, int cx, int cy, int r, const char *name);
bool ui_avatar_load(const char *path);   // picture of the current profile (avatar.bin from DSplay Studio)
bool ui_avatar_load_slot(int slot, const char *path); // NULL path forgets the picture
void ui_avatar_select(int slot, Color fallback);       // profile shown by ui_avatar(); circle color without a picture
bool ui_avatar_present(void);
void ui_avatar_slot(Canvas *c, int slot, int cx, int cy, int r, const char *name, Color fallback);
bool ui_wallpaper_load(const char *path);   // NULL clears it
bool ui_wallpaper_present(void);
void ui_wallpaper_draw(Canvas *c);          // full top screen
void ui_wallpaper_retint(void);             // after a theme change
void theme_set_accent(Color accent, Color hi, Color sel_light, Color sel_dark);
void ui_sel_frame(Canvas *c, int x, int y, int w, int h, int r, int frame);

// Deferred selection frames (see ui_widgets.c)
#define UI_MAX_RINGS 4
typedef struct {
    Canvas *c;
    int x, y, w, h, r;
    int cx0, cy0, cx1, cy1;   // clip rectangle active when the frame was placed
} UiRing;
extern UiRing ui_rings_top[UI_MAX_RINGS], ui_rings_bot[UI_MAX_RINGS];
extern int    ui_rings_top_n, ui_rings_bot_n;
void ui_rings_reset(Canvas *c);
void ui_ring_draw(const UiRing *rg, int frame);
void ui_tile_cache_reset(void);
void ui_spinner(Canvas *c, int cx, int cy, int r, int frame, Color col);
int  ui_pill(Canvas *c, int x, int y, FontId f, const char *text, Color bg, Color fg);
void ui_round_button(Canvas *c, int cx, int cy, int r, IconId icon, Color icon_col, bool focused, int frame);

// Game artwork
Color ui_system_color(ItemType t, bool dark_end);
void  ui_tile(Canvas *c, int lib_idx, const GameItem *it, int x, int y, int size, int radius);
void  ui_set_card_cover(const Color *px);   // COVER_SIZE^2 picture of the game card, or NULL
void  ui_tile_allsoft(Canvas *c, int x, int y, int size, int radius, const char *label);
void  ui_thumb(Canvas *c, int lib_idx, const GameItem *it, int x, int y, int size);

// Dimming overlay for modals
void  ui_scrim(Canvas *c, int a32);

// Time helpers
void  ui_format_clock(char *out, size_t len);
void  ui_format_date(uint32_t ts, char *out, size_t len);

#endif // UI_H

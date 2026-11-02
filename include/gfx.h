#ifndef GFX_H
#define GFX_H

// DSplay software renderer: two 256x192 RGB555 back buffers, anti-aliased
// primitives (rounded rectangles, circles, rings), alpha blending, gradients,
// proportional anti-aliased text (Inter) and image blitting.

#include "common.h"
#include "font_data.h"
#include "icon_data.h"

typedef u16 Color;

// 8-bit-per-channel helper -> RGB555 with the opaque bit set
#define C8(r, g, b) ((Color)((((r) >> 3) & 0x1F) | ((((g) >> 3) & 0x1F) << 5) | ((((b) >> 3) & 0x1F) << 10) | 0x8000))

typedef struct {
    Color *px;
    int w, h;               // size (w is also the row stride)
    int cx0, cy0, cx1, cy1; // clip rectangle (x1/y1 exclusive)
} Canvas;

extern Canvas g_top;
extern Canvas g_bot;

typedef enum {
    ALIGN_LEFT = 0,
    ALIGN_CENTER = 1,
    ALIGN_RIGHT = 2
} TextAlign;

void  gfx_init(void);
void  gfx_canvas_init(Canvas *c, Color *px, int w, int h);
void  gfx_present(bool top, bool bottom);
// Copy only rows [y0, y1) of a screen to VRAM (used for small animated updates)
void  gfx_present_rows(bool top, int y0, int y1);
void  gfx_copy_rect(Canvas *dst, const Canvas *src, int x, int y, int w, int h);
void  gfx_set_dim(int level); // 0 = normal, 1..31 darken amount applied at present time

void  gfx_set_clip(Canvas *c, int x, int y, int w, int h);
void  gfx_reset_clip(Canvas *c);

Color gfx_blend(Color dst, Color src, int a32);          // a32: 0..32
Color gfx_mix(Color a, Color b, int t256);              // t256: 0 (a) .. 256 (b)
Color gfx_lighten(Color c, int amount32);
Color gfx_darken(Color c, int amount32);

void  gfx_clear(Canvas *c, Color col);
void  gfx_pixel(Canvas *c, int x, int y, Color col);
void  gfx_pixel_a(Canvas *c, int x, int y, Color col, int a32);
void  gfx_rect(Canvas *c, int x, int y, int w, int h, Color col);
void  gfx_rect_a(Canvas *c, int x, int y, int w, int h, Color col, int a32);
void  gfx_vgradient(Canvas *c, int x, int y, int w, int h, Color top, Color bottom);
void  gfx_round_rect(Canvas *c, int x, int y, int w, int h, int r, Color col, int a32);
void  gfx_round_rect_vgrad(Canvas *c, int x, int y, int w, int h, int r, Color top, Color bottom);
void  gfx_round_ring(Canvas *c, int x, int y, int w, int h, int r, int thick, Color col, int a32);
void  gfx_circle(Canvas *c, int cx, int cy, int r, Color col, int a32);
void  gfx_ring(Canvas *c, int cx, int cy, int r, int thick, Color col, int a32);
void  gfx_shadow(Canvas *c, int x, int y, int w, int h, int r, int spread, int a32);
void  gfx_hline(Canvas *c, int x, int y, int w, Color col, int a32);

// Text (UTF-8). y is the top of the line box. Returns the drawn width.
int   gfx_text_width(FontId f, const char *s);
int   gfx_text(Canvas *c, FontId f, int x, int y, const char *s, Color col, int max_w);
int   gfx_text_align(Canvas *c, FontId f, int x, int y, int w, const char *s, Color col, TextAlign align);
// Word-wrapped paragraph; returns number of lines drawn
int   gfx_text_wrap(Canvas *c, FontId f, int x, int y, int w, int max_lines, int line_h, const char *s, Color col);
int   gfx_font_height(FontId f);
// true when every character has a glyph in the font (no "?" in its place)
bool  gfx_text_renderable(const char *s);
// Number of lines a paragraph needs when wrapped to width w (at most max_lines)
int   gfx_text_lines(FontId f, int w, int max_lines, const char *s);

// Monochrome alpha icons (see icon_data.h)
void  gfx_icon(Canvas *c, IconId id, int x, int y, Color col);
void  gfx_icon_centered(Canvas *c, IconId id, int cx, int cy, Color col);
int   gfx_icon_size(IconId id);

// Images
void  gfx_blit(Canvas *c, int x, int y, const Color *src, int sw, int sh);
// Scaled copy (nearest when upscaling by an integer factor, box filter when shrinking)
void  gfx_blit_scaled(Canvas *c, int dx, int dy, int dw, int dh, const Color *src, int sw, int sh);
// Rounded-corner mask applied to a scaled image (corners blended into the existing background)
void  gfx_blit_scaled_round(Canvas *c, int dx, int dy, int dw, int dh, const Color *src, int sw, int sh, int r);
// 1:1 copy of an opaque image with anti-aliased rounded corners (fast path for cached tiles)
void  gfx_blit_round(Canvas *c, int x, int y, const Color *src, int sw, int sh, int r);
// 32x32 4bpp NDS banner icon (tile data + 16 color palette), scaled by an integer factor
void  gfx_nds_icon(Canvas *c, int x, int y, const uint8_t *tiles, const uint16_t *pal, int scale);
// Average (dominant) color of an NDS icon, ignoring transparent pixels
Color gfx_nds_icon_tint(const uint8_t *tiles, const uint16_t *pal);

#endif // GFX_H

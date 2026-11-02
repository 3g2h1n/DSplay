#ifndef PHOTOFX_H
#define PHOTOFX_H
// What DSplay does with photos besides showing them: cropping into wallpapers / profile
// pictures / boot screens / covers, favorites, albums by month, the screensaver's slow
// pan-and-zoom, and the photo of the day.
#include "common.h"
#include "gfx.h"

// Crop region (in the 256x192 decoded picture) with a fixed aspect ratio aw:ah
typedef struct { int cx, cy, h, aw, ah; } PhotoCrop;
void photo_crop_init(PhotoCrop *c, int aw, int ah);            // largest region, centered
void photo_crop_move(PhotoCrop *c, int dx, int dy);            // keeps it inside the picture
void photo_crop_zoom(PhotoCrop *c, int dh);                    // smaller / larger region
void photo_crop_rect(const PhotoCrop *c, int *x, int *y, int *w, int *h);

// Region (rx, ry, rw, rh) of a sw x sh picture scaled (bilinear) into dw x dh
void photo_sample(const Color *src, int sw, int sh, int rx, int ry, int rw, int rh, Color *dst, int dw, int dh);

// The picture formats DSplay reads: "HWP1" (wallpaper, boot screen: 256x192), "HAV1" (profile
// picture: 64x64), "HFC1" (folder picture) - magic, u16 width, u16 height, RGB555 pixels.
// Written through <path>.tmp, so a power cut never leaves half a file.
bool photo_write_raw(const char *path, const char magic[4], const Color *px, int w, int h);
bool photo_read_raw(const char *path, const char magic[4], Color *px, int w, int h);
// RGB PNG (stored deflate blocks: no compressor needed) - covers are PNG files
bool photo_write_png(const char *path, const Color *px, int w, int h);

// Favorite photos (one path per line in <file>)
void photo_favs_load(const char *file);
bool photo_favs_save(void);
bool photo_is_fav(const char *path);
void photo_set_fav(const char *path, bool on);
int  photo_favs_count(void);

// Screensaver: one step of a slow pan-and-zoom of a 256x192 picture (t: 0..4096 over its time)
void photo_kenburns(Canvas *c, const Color *src, int t, int variant);
// Crossfade: dst = a * (32 - k) / 32 + b * k / 32
void photo_blend(Color *dst, const Color *a, const Color *b, int k);

// Month of a file time as YYYYMM (local time), 0 when unknown
int  photo_month(uint32_t t);
// Picture of the day: an index from the date (same all day)
int  photo_of_day(int count, uint32_t now);

#endif // PHOTOFX_H

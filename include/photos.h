#ifndef PHOTOS_H
#define PHOTOS_H
// Photo viewer: pictures taken with the DSi Camera (sd:/DCIM/...) and JPEG/PNG files put in
// sd:/photos/. Decoded one at a time and fitted to the 256x192 upper screen.
#include "common.h"
#include "gfx.h"

#define MAX_PHOTOS 600

bool        photos_exist(const char *drive);          // cheap: is there a DCIM or photos folder?
int         photos_scan(const char *drive);           // -> number of pictures (newest first)
int         photos_count(void);
const char *photos_path(int i);
uint32_t    photos_time(int i);                       // file time (0 = unknown)
bool        photos_load(int i, Color *dst);           // dst: 256x192; black bars keep the shape

#endif // PHOTOS_H

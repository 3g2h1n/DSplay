#ifndef COVERS_H
#define COVERS_H

// Cover art (box art) loader with a small LRU cache of decoded 96x96 images.
// Looks in sd:/_hyper/covers/ (written by DSplay Studio) and in
// sd:/_nds/TWiLightMenu/boxart/ (TWiLight Menu++ box art packs).
// Supported formats: PNG (RGB/RGBA/grey, non-interlaced) and 24/32-bit BMP.
//
// Speed: DSplay Studio also writes <drv>/_hyper/covers/_covers.pak, every cover already scaled
// to 96x96 RGB555, so the console reads 18 KB instead of decoding a PNG:
//   u32 magic "HCVP", u32 version (1), u32 count, u32 entry size (20)
//   count x { u32 FNV-1a hash of the lower-case file name, u8 folder (0 _hyper/covers,
//             1 TWiLight boxart, 2 _hyper/boxart), 3 x u8 0, u32 source size, u32 source mtime,
//             u32 offset of the 96x96 u16 picture }, sorted by hash
// The folders are scanned in the background (a few dozen entries per frame), so a large
// box art pack never slows the start; covers that are not in the pack are decoded as before.

#include "common.h"
#include "gfx.h"

#define COVER_SIZE 96
#define COVERS_PACK_NAME    "_covers.pak"
#define COVERS_PACK_MAGIC   0x50564348u   // "HCVP"
#define COVERS_PACK_VERSION 2   // 2: whole picture, transparent border

void         covers_init(const char *drv);
void         covers_disable(void);       // box art off: never touch the SD card for pictures
void         covers_reset(void);          // library changed
void         covers_add_file(const char *name);   // a cover was written to _hyper/covers
void         covers_frame_begin(void);    // allows one decode per frame
// Returns a COVER_SIZE x COVER_SIZE image or NULL (not available / not decoded yet). The picture
// keeps its aspect ratio, centered; pixels without bit 15 are the transparent border.
const Color *covers_get(int lib_idx, const GameItem *item);
bool         covers_known_missing(int lib_idx);
// Loads a cover into the cache without asking for a redraw (idle time, neighbours of the view);
// true when one was loaded
bool         covers_prefetch(int lib_idx, const GameItem *item);
bool         covers_budget_left(void);
bool         covers_scan_done(void);
int          covers_indexed_count(void);
uint32_t     covers_generation(void);
// Box art by game code (the inserted game card); dst = COVER_SIZE x COVER_SIZE
bool         covers_load_code(const char *drv, const char *code, Color *dst);   // changes whenever a cover finished loading

#endif // COVERS_H

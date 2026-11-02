#ifndef ROMDB_H
#define ROMDB_H

#include "common.h"

// File extension -> ItemType (TYPE_UNKNOWN when not a supported title)
ItemType romdb_type_from_name(const char *name);

// Fill a GameItem for the file at `path` (path, filename, title, icon, flags, ...)
bool romdb_fill_item(GameItem *item, const char *path, const char *filename, uint32_t size, uint32_t mtime);

// Parse an NDS header + banner from a 0x200 header buffer and a banner read callback
bool romdb_parse_nds_buffers(GameItem *item, const uint8_t *header, const uint8_t *banner, size_t banner_len);

// Clean a ROM file name into a display title ("Super_Game (USA) [!].sfc" -> "Super Game")
void romdb_title_from_filename(const char *filename, char *dst, size_t max_len);

// Convert one banner title (UTF-16LE, up to 128 chars) into UTF-8 title + subtitle (publisher line)
void romdb_banner_to_utf8(const uint16_t *src, char *title, size_t title_len, char *subtitle, size_t sub_len);

// Game name for a 4-letter code from <drive>/_hyper/titles.txt (DSplay Studio); false when unknown
bool romdb_lookup_title(const char *code, char *out, size_t len);

// Short / long system names for a type
const char *romdb_system_short(ItemType t);
const char *romdb_system_name(ItemType t);

// True when the .nds file is homebrew (no nds-bootstrap needed)
bool romdb_nds_is_homebrew(const uint8_t *header);

// Animated icon of DSi titles (banner version 0x0103): 8 bitmaps, 8 palettes and a sequence of
// up to 64 steps (u16: bits 0-7 frames at 60 Hz, 8-10 bitmap, 11-13 palette, 14 flip H, 15 flip V)
typedef struct {
    uint8_t  bmp[8][512];
    uint16_t pal[8][16];
    uint16_t seq[64];
    int      steps;
    uint32_t total;          // frames for one loop
} NdsAnim;
bool romdb_load_anim(const char *path, NdsAnim *a);
int  romdb_anim_step(const NdsAnim *a, uint32_t frame);     // sequence step shown at that frame
// 4bpp tiles of one step (flips applied) into a GameItem-style icon + palette
void romdb_anim_icon(const NdsAnim *a, int step, uint8_t *icon_out, uint16_t *pal_out);

#endif // ROMDB_H

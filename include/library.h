#ifndef LIBRARY_H
#define LIBRARY_H

// Game library: every supported title found under /roms (recursively), /games
// and the SD card root. Indexed once and cached in sd:/_hyper/library.bin so the
// HOME Menu appears instantly; only new / removed files are processed on boot.

#include "common.h"
#include "stats.h"

typedef void (*LibraryProgressFn)(int found, const char *current_name);

typedef enum {
    LIBRARY_SCAN_BUSY = 0,      // call library_scan_step again next frame
    LIBRARY_SCAN_UNCHANGED,     // finished, nothing changed
    LIBRARY_SCAN_CHANGED,       // finished, titles were added or removed (indices changed)
} LibraryScanResult;

bool      library_init(void);
// Load the cache and reconcile it with the SD card contents. `full` forces a rescan.
int       library_sync(const char *drv, bool full, LibraryProgressFn progress);
int       library_count(void);
int       library_capacity(void);        // how many titles fit (4096 on DSi, 1024 in DS mode)
void      library_merge_apps(void);      // after the list of DSi applications changed (apps.h)
bool      library_truncated(void);       // the last scan found more titles than fit

// Background scan: show the cached library at once, then walk the card a little per frame
void      library_load_cache(const char *drv);
bool      library_scan_begin(const char *drv, bool full, LibraryProgressFn progress);
LibraryScanResult library_scan_step(int walk_budget, int add_budget);
bool      library_scanning(void);
GameItem *library_get(int idx);
int       library_find_path(const char *path);
void      library_remove(int idx);       // after the file was deleted
void      library_refresh_flags(void);   // re-apply favorite / hidden sets
uint32_t  library_generation(void);      // changes whenever titles or flags change

// Build a filtered, sorted list of library indices.
// filter: TYPE_UNKNOWN = everything, TYPE_FOLDER = favorites only, otherwise the system type
int       library_view(int *out, int max, ItemType filter, SortMode sort, bool include_hidden, const PlaytimeDB *pt);
// Systems that currently have at least one visible title (in display order)
int       library_systems(ItemType *out, int max, bool include_hidden);
// Case/diacritic-insensitive substring search on titles and file names
int       library_search(const char *query, int *out, int max, bool include_hidden);

// Accent-folding lower-case ASCII key used for sorting/searching (exposed for tests)
void      library_fold(const char *utf8, char *out, size_t out_len);

#endif // LIBRARY_H

#ifndef STATS_H
#define STATS_H

// Persistent user data stored in sd:/_hyper/ : settings, play statistics,
// favorites and hidden titles.

#include "common.h"

#define PLAYTIME_MAGIC 0x34594C50 // "PLY4" (format unchanged since v1)
#define MAX_PLAYTIME_ENTRIES 256

typedef struct {
    char     filename[MAX_TITLE_LEN];
    uint32_t launch_count;
    uint32_t total_minutes;
    uint32_t last_played_ts;
} PlaytimeEntry;

typedef struct {
    uint32_t      magic;
    int32_t       count;
    PlaytimeEntry entries[MAX_PLAYTIME_ENTRIES];
} PlaytimeDB;

// Paths of the data files, built from the drive prefix ("sd:" / "fat:" / "")
void stats_set_drive(const char *drv);
const char *stats_drive(void);
void stats_path(char *out, size_t len, const char *name); // -> "<drv>/_hyper/profiles/pN/<name>"
void stats_set_profile(int slot);                          // -1 = "<drv>/_hyper/<name>"

// Settings
void settings_defaults(SystemSettings *cfg);
bool settings_load(SystemSettings *cfg);
bool settings_save(const SystemSettings *cfg);

// Play time
void playtime_load(PlaytimeDB *db);
bool playtime_save(const PlaytimeDB *db);
void playtime_begin_session(PlaytimeDB *db, const GameItem *item); // on launch
void playtime_end_session(PlaytimeDB *db);                         // on next boot
const PlaytimeEntry *playtime_get(const PlaytimeDB *db, const char *filename);
void daylog_add(uint32_t ts, uint32_t minutes);
void daylog_last_days(uint32_t now, uint32_t out[7]);              // out[6] = today

// Favorites / hidden sets (paths stored without the drive prefix)
void  pathset_load(void);
bool  pathset_is_favorite(const char *path);
bool  pathset_is_hidden(const char *path);
void  pathset_set_favorite(const char *path, bool on);
void  pathset_set_hidden(const char *path, bool on);
void  pathset_forget(const char *path); // after deletion

// Small helpers
bool  file_exists(const char *path);
bool  copy_file(const char *src, const char *dst);
void  make_dirs(const char *path); // mkdir -p for a directory path
const char *path_without_drive(const char *path);

#endif // STATS_H

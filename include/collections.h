#ifndef COLLECTIONS_H
#define COLLECTIONS_H
// Game folders ("collections") made on the console: a name, a color and a list of games.
// Stored per profile in folders.txt:
//     #<color>|<name>
//     /roms/NDS/Game.nds        (paths without the drive prefix)
// A game can be in several folders.
#include "common.h"

#define MAX_COLLECTIONS 12
#define COLL_NAME_LEN   24

void        coll_load(void);                 // the current profile's folders
bool        coll_save(void);
int         coll_count(void);
const char *coll_name(int i);
int         coll_color(int i);               // profile color index (0..7)
int         coll_create(const char *name, int color);   // -> index, -1 when full
void        coll_delete(int i);
void        coll_rename(int i, const char *name);
void        coll_set_color(int i, int color);
bool        coll_has(int i, const char *path);
void        coll_set(int i, const char *path, bool on);
uint32_t    coll_mask(const char *path);     // bit i = in folder i
int         coll_size(int i);                // number of games (as stored)
void        coll_forget(const char *path);   // the game was deleted
uint32_t    coll_version(void);              // changes with every edit (redraws)

#endif // COLLECTIONS_H

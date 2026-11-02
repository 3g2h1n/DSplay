#ifndef APPS_H
#define APPS_H
// DSi applications (Flipnote Studio, DSi Camera, DSi Sound, System Settings, DSiWare...) that
// the user added to DSplay. They are found in the system memory (read only, see
// launcher_nand_root) and kept in <drive>/_hyper/apps.bin, so the NAND is read only when the
// list is edited. The library shows them under "Applications" (TYPE_APP).
#include "common.h"

#define APPS_MAX 40

// Reads every application with a banner from the NAND (slow: a few seconds on the console).
// Returns how many were found, -1 when the NAND cannot be read.
int  apps_scan(GameItem *out, int max, uint8_t lang);

void apps_load(void);
bool apps_save(void);
int  apps_count(void);
const GameItem *apps_get(int i);
bool apps_has(const char *path);
bool apps_add(const GameItem *it);
bool apps_remove(const char *path);
bool apps_available(void);          // the console can run DSi applications (DSi mode)

#endif // APPS_H

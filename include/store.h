#ifndef STORE_H
#define STORE_H
// Homebrew store on the console. DSplay Studio writes the catalog (studio/store.py,
// _hyper/store/catalog.bin) and installs programs into roms/NDS/Homebrew/ over Wi-Fi;
// the console only reads the catalog and asks Studio (pad.h: pad_store_request).
#include "common.h"

typedef struct {
    char     slug[40];
    char     title[48];
    char     author[32];
    char     version[16];
    char     desc[200];
    char     file[48];
    uint8_t  category;       // 1 game, 2 utility, 3 emulator, 4 app / media
    uint8_t  flags;          // bit 0: has an icon
    uint16_t stars;
    uint32_t size;
    uint32_t updated;        // unix time
    uint16_t icon[32 * 32];  // RGB555
} StoreEntry;                // 2444 bytes, same layout as studio/store.py ENTRY + icon

enum { STORE_CAT_ALL = 0, STORE_CAT_GAME = 1, STORE_CAT_UTIL = 2, STORE_CAT_EMU = 3, STORE_CAT_APP = 4 };
enum { STORE_RES_OK = 0, STORE_RES_FAIL = 1, STORE_RES_OFFLINE = 2, STORE_RES_SPACE = 3, STORE_RES_UNKNOWN = 4 };

bool store_load(const char *drive);      // catalog + installed list; false when there is no catalog yet
void store_free(void);
int  store_count(void);
const StoreEntry *store_get(int i);
bool store_installed(const char *slug);
int  store_view(int category, int *out, int max);   // indices, catalog order (most stars first)
bool store_parse(const uint8_t *data, int len);     // (host tests)
void store_parse_installed(const char *text);       // (host tests)

#endif // STORE_H

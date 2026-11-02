#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define HYPEROS_VERSION "3.2.0"

#ifdef ARM9
#include <nds.h>
#include <fat.h>
#else
// Host (PC) build: minimal stand-ins so the UI/library code can be unit tested
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

#define SCREEN_WIDTH  256
#define SCREEN_HEIGHT 192

#define KEY_A      (1 << 0)
#define KEY_B      (1 << 1)
#define KEY_SELECT (1 << 2)
#define KEY_START  (1 << 3)
#define KEY_RIGHT  (1 << 4)
#define KEY_LEFT   (1 << 5)
#define KEY_UP     (1 << 6)
#define KEY_DOWN   (1 << 7)
#define KEY_R      (1 << 8)
#define KEY_L      (1 << 9)
#define KEY_X      (1 << 10)
#define KEY_Y      (1 << 11)
#define KEY_TOUCH  (1 << 12)
#define KEY_LID    (1 << 13)

typedef struct {
    u16 rawx, rawy, px, py, z1, z2;
} touchPosition;
#endif

#define MAX_PATH_LEN        256
#define MAX_TITLE_LEN       64
#define MAX_DISPLAY_TITLE   96
#define MAX_LIBRARY_ITEMS   4096   // DSi mode; 1024 in DS mode (see library_capacity)
#define MAX_RECENT_ITEMS    12

#define ICON_TILE_BYTES     512   // 32x32 @ 4bpp
#define ICON_PALETTE_COLORS 16

// Item / system types. Values are persisted (recents.bin, library.bin) - append only.
typedef enum {
    TYPE_FOLDER  = 0,
    TYPE_NDS     = 1,
    TYPE_GBA     = 2,
    TYPE_GB      = 3,
    TYPE_GBC     = 4,
    TYPE_NES     = 5,
    TYPE_SNES    = 6,
    TYPE_GENESIS = 7,
    TYPE_SLOT1   = 8,
    TYPE_HOME    = 9,
    TYPE_SMS     = 10,
    TYPE_GG      = 11,
    TYPE_ALLSOFT = 12, // pseudo tile at the end of the HOME row
    TYPE_UNKNOWN = 13,
    TYPE_APP     = 14  // DSi application in the system memory (Flipnote, DSi Camera...)
} ItemType;

#define ITEM_FLAG_FAVORITE  (1u << 0)
#define ITEM_FLAG_HIDDEN    (1u << 1)
#define ITEM_FLAG_HOMEBREW  (1u << 2)
#define ITEM_FLAG_DSI       (1u << 3)  // DSi-enhanced / DSi-only NDS title

// One launchable title. Fixed layout (persisted to library.bin / recents.bin).
typedef struct {
    char     path[MAX_PATH_LEN];          // full path including the drive prefix ("sd:/roms/...")
    char     filename[MAX_TITLE_LEN];     // file name (key for play time statistics)
    char     title[MAX_DISPLAY_TITLE];    // UTF-8 display title
    char     subtitle[MAX_TITLE_LEN];     // publisher / second banner line (UTF-8)
    char     game_code[8];                // 4-char NDS game code or ""
    uint32_t type;                        // ItemType
    uint32_t file_size;
    uint32_t mtime;
    uint32_t flags;                       // ITEM_FLAG_*
    uint8_t  icon_data[ICON_TILE_BYTES];
    uint16_t icon_palette[ICON_PALETTE_COLORS];
} GameItem;

// Sort modes for the HOME Menu / All Software
typedef enum {
    SORT_RECENT   = 0,
    SORT_ALPHA    = 1,
    SORT_PLAYTIME = 2,
    SORT_COUNT    = 3
} SortMode;

// UI sound packs
#define SFX_PACK_HYPER 0
#define SFX_PACK_DSI   1   // (old settings only: the classic sounds are a DSplay Studio download now)
#define SFX_PACK_MUTE  2

// Persistent user settings (sd:/_hyper/settings.bin)
#define SETTINGS_MAGIC 0x35475453 // "STG5"
typedef struct {
    uint32_t magic;
    uint8_t  theme;            // 0 = Basic Black, 1 = Basic White
    uint8_t  language;         // AppLanguage
    uint8_t  sfx_pack;         // SFX_PACK_*
    uint8_t  volume;           // 0..5
    uint8_t  brightness;       // 1..5
    uint8_t  sort_mode;        // SortMode
    uint8_t  save_slot;        // 0..2 default save slot
    uint8_t  anim_off;         // 1 = no animated DSi icons (this byte was "cheats" before 3.2)
    uint8_t  show_hidden;      // 1 = list hidden titles
    uint8_t  covers;           // 1 = show box art from the SD card (slower), 0 = system icons
    uint8_t  splash;           // SPLASH_* boot animation (include/splash.h)
    uint8_t  lid_sound_off;    // 1 = no sound when the lid is opened / closed
    uint8_t  lid_login_off;    // 1 = closing the lid does not go back to the login screen
    uint8_t  home_hide;        // HOMEHIDE_* bits: parts of the home screen switched off
    uint8_t  home_show;        // HOMESHOW_* bits: optional parts switched on (off by default)
    uint8_t  home_games;       // game tiles on the home screen: 0 = default, HOME_GAMES_NONE = none
    char     profile_name[20]; // UTF-8
    // ---- added in 3.2 (older settings.bin files end above: read with the defaults below) ----
    uint8_t  home_btns;        // HOMEBTN_* round buttons on the home screen
    uint8_t  beta;             // 1 = "Try beta features": Store and Nearby are shown
    uint8_t  photo_opts;       // PHOTOOPT_*: 0 = the defaults (screensaver after 3 min, frame, photo of the day)
    uint8_t  reserved[1];
} SystemSettings;
#define SETTINGS_SIZE_V1     40     // settings.bin before home_btns
#define HOMEBTN_POWER        0x01   // power button shown (always shown without DSplay's own ARM7)
#define HOMEBTN_PAD          0x02   // controller
#define HOMEBTN_NEARBY       0x04
#define HOMEBTN_PHOTOS       0x08
#define HOMEBTN_CARTBOX      0x10   // physical card: its box art on the lower screen tile
#define PHOTOOPT_SAVER_MASK  0x07   // screensaver: 0 = 3 min, 1 = off, 2 = 1 min, 3 = 5 min, 4 = 10 min
#define PHOTOOPT_FAVS_ONLY   0x08   // screensaver shows only favorite photos
#define PHOTOOPT_NO_FRAME    0x10   // no photo frame while charging
#define PHOTOOPT_NO_POD      0x20   // no photo of the day behind the big clock

#define HOMEHIDE_CARD_TILE   0x01
#define HOMEHIDE_ALL_TILE    0x02
#define HOMEHIDE_BTN_CARD    0x04
#define HOMEHIDE_BTN_ALL     0x08
#define HOMEHIDE_BTN_SEARCH  0x10
#define HOMEHIDE_BTN_FAVS    0x20
#define HOMEHIDE_BTN_FOLDERS 0x40
#define HOMESHOW_PAD         0x01
#define HOMEOPT_NO_PHOTOS    0x02   // Photos tile switched off (it is on by default when there are photos)
#define HOMEOPT_NO_CLOCK     0x04   // no big clock on the upper screen after 30 s without input
#define HOMEOPT_NO_STORE     0x08   // Store tile switched off
#define HOMEOPT_NO_NEARBY    0x10   // "Nearby" tile switched off
// upper screen, bottom line of a game: its region (default), its game code, or neither
#define GAMEINFO_MASK        0x60
#define GAMEINFO_REGION      0x00
#define GAMEINFO_CODE        0x20
#define GAMEINFO_NONE        0x40
#define HOME_GAMES_NONE      255

#endif // COMMON_H

#ifndef PROFILES_H
#define PROFILES_H
// User profiles (max 4). Each profile owns a folder with its own settings, favorites,
// hidden titles, play time, avatar, wallpaper and Nintendo DS saves:
//
//   <drv>/_hyper/profiles/profiles.bin      ProfilesGlobal (last profile, login screen on/off, resume flag)
//   <drv>/_hyper/profiles/pN/profile.bin    ProfileInfo (name, color, lock)
//   <drv>/_hyper/profiles/pN/settings.bin, favorites.txt, hidden.txt, playtime.bin, session.bin,
//                            daylog.bin, avatar.bin, wallpaper.bin, saves/, backups/
//
// The first profile (p0) keeps its DS saves next to the game files as before, so saves
// made before profiles existed stay where every other tool expects them.
#include "common.h"

#define MAX_PROFILES   4
#define PROFILE_MAGIC  0x31465250u   // "PRF1"
#define PROFILES_MAGIC 0x31535250u   // "PRS1"
#define PIN_MAX        8
#define PIN_LEN        4
#define PROFILE_COLORS 8

typedef struct {
    uint32_t magic;
    char     name[20];          // UTF-8
    uint8_t  color;             // 0..PROFILE_COLORS-1
    uint8_t  pin_len;           // 0 = no lock
    uint8_t  pin[PIN_MAX];      // PIN_KEY_* codes
    uint8_t  reserved[2];
    uint32_t created;
    char     last_title[48];    // last game played (shown on the login screen)
    uint32_t last_ts;
} ProfileInfo;                  // 92 bytes

typedef struct {
    uint32_t magic;
    uint8_t  last;              // profile used last
    uint8_t  login;             // 1 = show the login screen at every start
    uint8_t  resume;            // 1 = a game was started; come back without the login screen
    uint8_t  reserved[9];
} ProfilesGlobal;               // 16 bytes

// PIN keys (B is never part of a PIN: it cancels)
enum { PIN_KEY_NONE = 0, PIN_KEY_UP, PIN_KEY_DOWN, PIN_KEY_LEFT, PIN_KEY_RIGHT,
       PIN_KEY_A, PIN_KEY_X, PIN_KEY_Y, PIN_KEY_L, PIN_KEY_R };

// Loads the profile list, creating the first profile from the pre-profile files when needed
void  profiles_init(const char *drv);
int   profiles_count(void);
int   profiles_slot(int i);                 // i-th existing profile -> slot number (0..3)
bool  profiles_exists(int slot);
const ProfileInfo *profiles_get(int slot);
bool  profiles_update(int slot, const ProfileInfo *info);
int   profiles_create(const char *name);    // -> slot, or -1 when all 4 are used
bool  profiles_delete(int slot);            // the folder is kept as _hyper/profiles/deleted_*
void  profiles_activate(int slot);          // data paths now point at this profile
int   profiles_active(void);
void  profiles_dir(int slot, char *out, size_t len);
void  profiles_file(int slot, const char *name, char *out, size_t len);

ProfilesGlobal *profiles_global(void);
bool  profiles_global_save(void);

bool  profiles_has_pin(int slot);
bool  profiles_check_pin(int slot, const uint8_t *pin, int len);
void  profiles_set_pin(int slot, const uint8_t *pin, int len);   // len 0 removes the lock
void  profiles_note_launch(int slot, const char *title);

// Accent colors: base, highlight, text accent on light / dark backgrounds
void  profile_color(int idx, uint16_t *accent, uint16_t *hi, uint16_t *sel_light, uint16_t *sel_dark);
int   profiles_free_color(void);            // first color no profile uses yet

#endif // PROFILES_H

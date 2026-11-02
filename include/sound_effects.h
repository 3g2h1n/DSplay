#ifndef SOUND_EFFECTS_H
#define SOUND_EFFECTS_H

#include "common.h"

// Every UI event that can trigger a sound. A custom pack can replace any of them
// with sd:/_hyper/sfx/<name>.wav (PCM 8/16-bit, mono/stereo, 4-48 kHz, max 3 s).
typedef enum {
    SFX_TICK = 0,   // cursor moved                -> tick.wav
    SFX_DECIDE,     // confirm / open              -> decide.wav
    SFX_BACK,       // cancel / close              -> back.wav
    SFX_LAUNCH,     // software starts             -> launch.wav
    SFX_MODAL,      // popup / options sheet       -> popup.wav
    SFX_TOGGLE,     // setting changed             -> toggle.wav
    SFX_PAGE,       // L/R category change         -> page.wav
    SFX_ERROR,      // not possible                -> error.wav
    SFX_INSERT,     // game card inserted          -> insert.wav
    SFX_EJECT,      // game card removed           -> eject.wav
    SFX_FAVORITE,   // favorite toggled            -> favorite.wav
    SFX_STARTUP,    // DSplay boot                -> startup.wav
    SFX_KEY,        // keyboard key                -> key.wav
    SFX_BOOT,       // boot animation "Snap"       -> boot_snap.wav
    SFX_LID_CLOSE,  // lid closed (menu sleeps)    -> lid_close.wav
    SFX_LID_OPEN,   // lid opened                  -> lid_open.wav
    SFX_COUNT
} SfxEvent;

void        sound_init(void);
void        sound_set_volume(int level);       // 0..5
int         sound_load_pack(const char *dir);  // returns number of custom sounds loaded
bool        sound_load_boot(const char *path); // sound of the custom boot screen (max 5 s)
bool        sound_play_boot(void);             // plays it (any pack but mute); false when there is none
const char *sound_event_filename(SfxEvent ev);
void        sound_play(SfxEvent ev, int pack); // pack: SFX_PACK_*
void        sound_prepare(SfxEvent ev, int pack); // synthesize now (sounds are otherwise made on first use)

// Synthesized built-in sounds (exposed for the host test / WAV export)
int         sound_builtin(SfxEvent ev, int pack, const int16_t **data);
#define SFX_RATE 16000

#endif // SOUND_EFFECTS_H

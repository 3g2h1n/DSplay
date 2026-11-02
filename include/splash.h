#ifndef SPLASH_H
#define SPLASH_H
// Boot animation "Snap": the two halves of a folding console meet at the hinge with a
// click. Drawn with the gfx primitives only (no image files, nothing read from the SD).
#include "common.h"

// SystemSettings.splash: any value except SPLASH_OFF means "on" (older builds stored
// other animation numbers there; they all map to this animation now)
#define SPLASH_ON     1
#define SPLASH_CUSTOM 2      // the user's own boot screen, made in DSplay Studio
#define SPLASH_OFF    4
static inline bool splash_enabled(uint8_t v) { return v != SPLASH_OFF; }

// Custom boot screen: <drive>/_hyper/boot/top.bin and bottom.bin ("HWP1" pictures, 256x192
// RGB555, bottom optional), boot.cfg ("ms=2500", "style=fade|flash") and boot.wav (sound).
// The pictures are read straight into the screen buffers once; the animation itself is done
// by the display's brightness hardware, so it costs nothing while the menu loads.
bool splash_custom_available(const char *drive);   // top.bin is there
bool splash_custom_load(const char *drive);        // reads the pictures + settings, selects custom mode
void splash_custom_off(void);                      // back to the built-in animation
bool splash_is_custom(void);
int  splash_fade_in(int f);                        // custom: darkening level 0..16 for frame f (fade style)

int  splash_frames(void);        // length in frames (60 fps), including the fade-out
int  splash_fade_frames(void);   // frames at the end during which the screens fade to black
int  splash_flash(int f);        // hardware brighten level for frame f (the white flash)
void splash_draw(int f);         // renders frame f into g_top / g_bot with the current theme

#endif // SPLASH_H

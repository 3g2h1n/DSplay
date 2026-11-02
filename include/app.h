#ifndef APP_H
#define APP_H

#include "common.h"

// Input snapshot for one frame (filled by main.c on hardware, by tests on the host)
typedef struct {
    u32  down;      // newly pressed keys
    u32  repeat;    // pressed or auto-repeated keys
    u32  held;      // currently held keys
    u32  up;        // released keys
    bool touching;
    int  tx, ty;    // touch position when touching
    bool power;     // the POWER button was tapped (DSi, needs DSplay's ARM7)
    int  rawx, rawy; // raw 12-bit touch position (DSplay's own calibration, include/touchcal.h)
} AppInput;

// Shows the DSplay logo at once (before the SD card is mounted)
void app_boot_logo(void);
// One-time setup. `fat_ok` = storage mounted. Returns false on fatal error screen.
bool app_init(bool fat_ok, bool quick_resume);
// Process input and render one frame into the gfx back buffers. Returns false to exit.
bool app_frame(const AppInput *in);
// Hardware master brightness offset requested by the UI (0 = normal .. 16 = black)
int  app_fade_level(void);
// Copy the changed parts of the back buffers to VRAM (call during vblank)
void app_present(void);

#endif // APP_H

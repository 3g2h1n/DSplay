// DSplay for Nintendo DSi - entry point and main loop
#include "common.h"
#include "app.h"
#include "gfx.h"
#include "launcher.h"
#include <stdio.h>
#include <string.h>

#ifdef ARM9
#include <nds.h>
#include <calico/nds/pm.h>

// Input seen between two frames. The menu reads the keys once per frame; when a frame takes long
// (start-up, a cover being decoded) a quick tap or button press could fall in between and be
// lost. The vertical blank interrupt (60 times a second, whatever the menu is doing) notes every
// press, and the next frame gets the ones it would have missed.
static volatile u32  s_vbl;
static volatile u32  s_irq_prev, s_irq_down, s_irq_frame;
static volatile u16  s_irq_tx, s_irq_ty, s_irq_rx, s_irq_ry;

static void vblank_irq(void) {
    s_vbl++;
    TouchData t;
    bool touching = touchRead(&t);
    u32 cur = (keypadGetState() & 0x0FFF) | (touching ? KEY_TOUCH : 0);
    u32 rise = cur & ~s_irq_prev;
    s_irq_prev = cur;
    if (!rise) return;
    if ((rise & KEY_TOUCH) && !(s_irq_down & KEY_TOUCH)) {
        s_irq_tx = t.px; s_irq_ty = t.py; s_irq_rx = t.rawx; s_irq_ry = t.rawy;
    }
    s_irq_down |= rise;
    s_irq_frame = s_vbl;
}

// presses the irq saw that this frame's scanKeys() did not (pressed and released in between)
static u32 take_missed(u32 down, u32 held, int *tx, int *ty, int *rx, int *ry) {
    IrqState st = irqLock();
    u32 q = s_irq_down, age = s_vbl - s_irq_frame;
    *tx = s_irq_tx; *ty = s_irq_ty; *rx = s_irq_rx; *ry = s_irq_ry;
    s_irq_down = 0;
    irqUnlock(st);
    if (age > 30) return 0;                  // older than half a second: not meant any more
    return q & ~down & ~held;
}

static void set_master(int level) {
    if (level < 0) level = 0;
    if (level > 16) level = 16;
    u16 v = level ? (u16)((2 << 14) | level) : 0;
    REG_MASTER_BRIGHT = v;
    REG_MASTER_BRIGHT_SUB = v;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    // Black screens while everything is set up (no white flash)
    powerOn(POWER_ALL_2D);
    lcdMainOnTop();
    set_master(16);
    defaultExceptionHandler();
    power_button_init();            // asks the ARM7 early: POWER tap, levels, back from a game?
    setCpuClock(true); // 133 MHz while in the menu (DSi mode)
    keysSetRepeat(18, 5);

    app_boot_logo();               // something on screen while the SD card starts up
    bool fat_ok = fatInitDefault();

    scanKeys();
    bool quick_resume = (keysHeld() & KEY_START) != 0;

    app_init(fat_ok, quick_resume);
    pmSetSleepAllowed(false);       // closing the lid is handled by the menu (sound, login screen)
    irqSet(IRQ_VBLANK, vblank_irq);
    irqEnable(IRQ_VBLANK);

    u32 release_next = 0;           // a quick press found by the irq is released on the next frame
    while (pmMainLoop()) {
        scanKeys();
        AppInput in;
        memset(&in, 0, sizeof(in));
        in.down = keysDown();
        in.repeat = keysDownRepeat();
        in.held = keysHeld();
        in.up = keysUp() | release_next;
        release_next = 0;
        power_button_poll();
        in.power = power_button_tapped();
        if (in.held & KEY_TOUCH) {
            touchPosition t;
            touchRead(&t);
            in.touching = true;
            in.tx = t.px;
            in.ty = t.py;
            in.rawx = t.rawx;
            in.rawy = t.rawy;
        }
        int qx, qy, qrx, qry;
        u32 missed = take_missed(in.down, in.held, &qx, &qy, &qrx, &qry);
        if (missed) {
            in.down |= missed;
            in.repeat |= missed & ~KEY_TOUCH;
            if (missed & KEY_TOUCH) {          // a tap shorter than a frame: down now, up next frame
                in.held |= KEY_TOUCH;
                in.touching = true;
                in.tx = qx; in.ty = qy; in.rawx = qrx; in.rawy = qry;
            }
            release_next = missed;
        }
        app_frame(&in);          // input + render into the back buffers
        swiWaitForVBlank();      // copy during vblank: no tearing
        set_master(app_fade_level());
        app_present();
    }
    // Power button / reset combo: hand control back to the DSi Menu
    return_to_dsi_menu();
    return 0;
}

#else
// Host build: see source/sim_main.c
#endif

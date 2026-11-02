// DSplay ARM7: calico's default ARM7 program (ds7_maine) plus the power menu.
//
// PxiChannel_User0, ARM9 -> ARM7: 1 = power off now, 4 = hello, 6 = a game is being started
//                                 (remembered in the MCU's free register 0x71, kept over a reset),
//                                 8 = forget it (the game did not start)
//                   ARM7 -> ARM9: 2 = the POWER button was tapped, 5 = hello answer,
//                                 0x1000 | volume (0-31) | backlight (0-4) << 5 = levels changed (DSi),
//                                 0x2000 | 1 = this start comes back from a game (0x2000: power on),
//                                 7 = the game start is noted
// The stock ARM7 restarts the DSi when POWER is tapped; DSplay shows its power menu instead.
// Holding POWER still turns the console off at once (handled by calico, as before).
#include <calico.h>

#define DSPLAY_PXI_POWER PxiChannel_User0

static volatile bool s_power_off;
static volatile bool s_power_tap;
static volatile bool s_hello;
static volatile bool s_levels_on;          // set by the hello: only DSplay's ARM9 wants the levels
static unsigned s_levels, s_poll;
static volatile bool s_mark_game, s_unmark_game;
#define DSPLAY_GAME_MARK 0x5A              // MCU register 0x71: "DSplay started a game"
#define DSPLAY_MCU_MARK_REG 0x71

static void power_handler(void *user, u32 data) {
    (void)user;
    if (data == 1) s_power_off = true;      // handled on the main thread, not in the IRQ
    else if (data == 4) { s_hello = true; s_levels_on = true; s_levels = 0; }   // the ARM9 asks whether this ARM7 is DSplay's
    else if (data == 6) s_mark_game = true;
    else if (data == 8) s_unmark_game = true;
}

static void power_tap(unsigned irq_mask) {  // MCU thread: the button was released quickly
    (void)irq_mask;
    s_power_tap = true;
}

// Never runs (ARM code, like calico's .32 files). Its calls make the linker create the ARM->Thumb thunks that NTR code needs in
// ARM7 WRAM (see arm7/ds7-lld.ld); otherwise lld may share a thunk placed in the DSi-only
// .twl section, which does not exist in DS mode.
__attribute__((used, noinline, target("arm"), section(".text.dsplay_thunk_anchor")))
void dsplay_thunk_anchor(Mailbox *mb, u32 v) {
    volatile u32 r = 0;
    r += mailboxTrySend(mb, v);
    r += mailboxRecv(mb);
    (void)r;
}

int main(void) {
    envReadNvramSettings();
    keypadStartExtServer();
    lcdSetIrqMask(DISPSTAT_IE_ALL, DISPSTAT_IE_VBLANK);
    irqEnable(IRQ_VBLANK);
    rtcInit();
    rtcSyncTime();
    pmInit();
    blkInit();
    touchInit();
    touchStartServer(80, 28);
    soundStartServer(12);
    micStartServer(4);
    wlmgrStartServer(20);
    pxiSetHandler(DSPLAY_PXI_POWER, power_handler, NULL);
    if (systemIsTwlMode()) {
        // pmInit made "begin pressing" ask for a reset; DSplay only wants to hear about the tap
        mcuIrqSet(MCU_IRQ_PWRBTN_BEGIN, NULL);
        mcuIrqSet(MCU_IRQ_PWRBTN_RESET, power_tap);
    }

    // Back from a game started by DSplay? Read once at start (the register is cleared at power on,
    // and here, so only the very next start counts); every hello gets the same answer.
    unsigned back = 0;
    if (systemIsTwlMode()) {
        i2cLock();
        back = i2cReadRegister8(I2cDev_MCU, DSPLAY_MCU_MARK_REG) == DSPLAY_GAME_MARK;
        i2cWriteRegister8(I2cDev_MCU, DSPLAY_MCU_MARK_REG, 0);
        i2cUnlock();
    }

    while (pmMainLoop()) {
        threadWaitForVBlank();
        if (s_hello) {
            // (the ARM9 repeats its hello until it hears this: one sent before this ARM7 was
            // listening is lost)
            s_hello = false;
            pxiSend(DSPLAY_PXI_POWER, 5);
            pxiSend(DSPLAY_PXI_POWER, 0x2000 | back);
        }
        if (s_unmark_game) {
            s_unmark_game = false;
            if (systemIsTwlMode()) {
                i2cLock();
                i2cWriteRegister8(I2cDev_MCU, DSPLAY_MCU_MARK_REG, 0);
                i2cUnlock();
            }
        }
        if (s_mark_game) {
            s_mark_game = false;
            if (systemIsTwlMode()) {
                i2cLock();
                i2cWriteRegister8(I2cDev_MCU, DSPLAY_MCU_MARK_REG, DSPLAY_GAME_MARK);
                i2cUnlock();
            }
            pxiSend(DSPLAY_PXI_POWER, 7);
        }
        if (s_power_tap) {
            s_power_tap = false;
            pxiSend(DSPLAY_PXI_POWER, 2);
        }
        if (s_levels_on && systemIsTwlMode() && ++s_poll >= 6) {
            // volume / brightness (the MCU changes them itself): the ARM9 shows a small indicator
            s_poll = 0;
            i2cLock();
            unsigned vol = i2cReadRegister8(I2cDev_MCU, McuReg_VolumeLevel) & 0x1F;
            unsigned bl = i2cReadRegister8(I2cDev_MCU, McuReg_BacklightLevel) & 0x07;
            i2cUnlock();
            unsigned lv = 0x1000u | vol | bl << 5;
            if (lv != s_levels) {
                s_levels = lv;
                pxiSend(DSPLAY_PXI_POWER, lv);
            }
        }
        if (s_power_off) {
            if (systemIsTwlMode()) mcuIssueShutdown();   // DSi: MCU power off (same as holding POWER)
            else pmicIssueShutdown();                     // DS: PMIC power off
        }
    }
    return 0;
}

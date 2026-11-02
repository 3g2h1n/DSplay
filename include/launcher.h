#ifndef LAUNCHER_H
#define LAUNCHER_H

// Launch planning and execution. The recipes mirror TWiLight Menu++ 27.x
// (universal/source/launch/launchPlanner.cpp) so the bundled emulators and
// nds-bootstrap receive exactly the paths / ini keys they expect:
//   * retail DS games      -> nds-bootstrap-release.nds (+ nds-bootstrap.ini)
//   * DSi-ready homebrew   -> booted directly
//   * DS-only homebrew     -> nds-bootstrap-hb-release.nds (gives it SD access)
//   * GBA                  -> GBARunner2 through nds-bootstrap-hb (needs _gba/bios.bin)
//   * SNES                 -> SNEmulDS (legacy) through nds-bootstrap-hb
//   * Mega Drive           -> jEnesisDS through nds-bootstrap-hb, PicoDriveTWL for big ROMs
//   * GB/GBC, NES, SMS/GG  -> GameYob / nesDS / S8DS booted directly with an sd:/ path

#include "common.h"

typedef enum {
    LAUNCH_OK = 0,
    LAUNCH_ERR_NO_FILE,
    LAUNCH_ERR_MISSING_EMU,
    LAUNCH_ERR_NO_BOOTSTRAP,
    LAUNCH_ERR_GBA_BIOS,
    LAUNCH_ERR_LOADER,
    LAUNCH_ERR_UNSUPPORTED
} LaunchResult;

typedef enum {
    INI_NONE = 0,
    INI_RETAIL,
    INI_HOMEBREW,
    INI_DSIWARE                            // a DSiWare title booted by nds-bootstrap
} IniKind;

typedef struct {
    char        boot_path[MAX_PATH_LEN];   // file handed to the NDS loader
    char        args[3][MAX_PATH_LEN];     // argv[1..3]
    int         argc;                      // including argv[0] = boot_path
    IniKind     ini;
    char        ini_nds_path[MAX_PATH_LEN];
    char        ini_sav_path[MAX_PATH_LEN];
    char        ini_homebrew_arg[MAX_PATH_LEN];
    int         ini_dsi_mode;
    int         ini_boost_cpu;
    int         ini_boost_vram;
    bool        boost_cpu;                 // 133 MHz while loading / for direct boots
    char        mkdir_path[MAX_PATH_LEN];  // data folder some emulators need
    char        save_path[MAX_PATH_LEN];   // save file to create when missing (retail DS)
    // DSiWare (.nds of a downloadable DSi title) on the DSi SD card: booted natively by Unlaunch,
    // like TWiLight Menu++'s "DSiWare booter: Unlaunch". Its saves are FAT12 images next to it.
    bool        dsiware;
    char        pub_path[MAX_PATH_LEN];    // "<game>.pub" (public.sav), "" when the title has none
    char        prv_path[MAX_PATH_LEN];    // "<game>.prv" (private.sav)
    uint32_t    pub_size, prv_size;
    // pub_path / prv_path are where the game reads its saves while it runs. home_* is where the
    // save is kept: next to the game for the first profile, <profile>/saves/ for the others.
    // When they differ the save is swapped in before the launch and back at the next start.
    char        home_pub[MAX_PATH_LEN];
    char        home_prv[MAX_PATH_LEN];
    bool        dsw_bootstrap;             // Options > Game settings > Boot: nds-bootstrap
    char        app_path[MAX_PATH_LEN];    // short (8.3) path of the title for nds-bootstrap
    bool        app_copy;                  // the title is copied to app_path first (long path)
} LaunchPlan;

// DSi applications in the system memory (NAND, read only). The root is "nand:" on the console
// (mounted on first use, takes a moment); tests and the simulator point it at a folder.
bool         launcher_nand_root(char *out, size_t len);
void         launcher_set_nand_root(const char *dir);
// "<root>/title/00030004/4b475545" -> "<root>/title/00030004/4b475545/content/000000xx.app"
bool         launcher_title_app(const char *title_dir, char *out, size_t len);

// Returns "sd:" on DSi SD, "fat:" on a flashcard, "" on the PC simulator
const char  *get_drive_prefix(void);
void         launcher_set_drive(const char *drv); // tests / simulator

// Pure planning step (no side effects) - unit tested on the host
LaunchResult launcher_plan(const GameItem *item, const SystemSettings *cfg, LaunchPlan *plan, char *detail, size_t detail_len);
// Writes nds-bootstrap.ini, creates save files, backs up the save and boots
LaunchResult launcher_run(const GameItem *item, const SystemSettings *cfg, char *detail, size_t detail_len, int *loader_code);

// Save slot helpers (slot 0 = .sav, 1 = .sav1, 2 = .sav2)
void         save_path_for(const char *rom_path, int slot, char *out, size_t len);
void         game_save_path(const GameItem *item, int slot, char *out, size_t len); // profile aware
void         launcher_set_profile_dir(const char *dir);   // "" / NULL = first profile (legacy places)
bool         backup_game_save(const GameItem *item, int slot);
bool         restore_game_save_backup(const GameItem *item, int slot);
int          launcher_keep_saves(const GameItem *item);

// Per-game settings (Nintendo DS games, via nds-bootstrap): <drive>/_hyper/game_settings.txt,
// one line per game: "<file name>|cpu|mode|vram". 0 everywhere = automatic.
typedef struct {
    uint8_t cpu;    // 0 auto, 1 normal (67 MHz), 2 fast (133 MHz)
    uint8_t mode;   // 0 auto (DSi mode), 1 DS mode (compatibility), 2 DSi mode
    uint8_t vram;   // 0 auto (off), 1 on
    uint8_t dsw;    // DSiWare: 0 Unlaunch (default), 1 nds-bootstrap
} GameCfg;
void         game_cfg_get(const char *filename, GameCfg *out);
bool         game_cfg_set(const char *filename, const GameCfg *cfg);   // all 0 = line removed   // before deleting a game: saves -> _hyper/deleted_saves
bool         gba_bios_present(void);

// System software transitions (DSi): back to the DSi Menu / boot the Slot-1 card
void         return_to_dsi_menu(void);
// Power menu: turns the console off (needs DSplay's own ARM7, see arm7/main.c) / restarts it.
// Both unmount the SD card first so everything written is on the card.
void         power_off_console(void);
void         restart_console(void);
// The ARM7 reports a tap of the POWER button instead of restarting the console
void         power_button_init(void);
void         power_button_poll(void);    // every frame: asks again until DSplay's ARM7 answers
bool         power_button_tapped(void);   // true once per tap
bool         power_off_supported(void);   // false with the stock ARM7: no "Turn off" in the menu
// Volume (0-31) and backlight (0-4) of the DSi as DSplay's ARM7 reports them (-1 = unknown);
// the return value changes with every report
unsigned     console_levels(int *vol, int *bright);
void         console_levels_sim(int vol, int bright);   // simulator / tests only
// 1 = back from a game started by DSplay, 0 = switched on, -1 = cannot tell (stock ARM7)
int          launcher_boot_kind(void);
void         launcher_mark_game_start(void);    // just before a game is started
void         launcher_mark_game_start_cancel(void);   // the game did not start after all
void         launch_slot1_card(void);

// DSiWare: true when the .nds header is a downloadable DSi title (unit code DSi, title id
// 00030004 / 00030005, SD access); public / private save sizes from the header
bool         launcher_is_dsiware(const char *path, uint32_t *pub_size, uint32_t *prv_size);
// A blank FAT12 save image the way the DSi (and TWiLight Menu++) formats public.sav / private.sav
bool         launcher_make_dsiware_save(const char *path, uint32_t size);
// DSiWare saves swapped in for a launch go back to their profile: called at every start (and
// before a DSiWare launch). Returns false when something is still waiting (it is kept and retried).
bool         launcher_dsiware_resume(void);

// Writes the nds-bootstrap.ini text for a plan into buf (exposed for tests)
int          launcher_format_ini(const LaunchPlan *plan, const SystemSettings *cfg, char *buf, size_t len);

#endif // LAUNCHER_H

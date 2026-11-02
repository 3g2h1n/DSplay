// DSplay - application state machine, input handling and screen rendering.
#include "app.h"
#include "gfx.h"
#include "ui.h"
#include "i18n.h"
#include "library.h"
#include "stats.h"
#include "covers.h"
#include "touchcal.h"
#include "launcher.h"
#include "romdb.h"
#include "slot1.h"
#include "sound_effects.h"
#include "splash.h"
#include "profiles.h"
#include "collections.h"
#include "pad.h"
#include "photos.h"
#include "photofx.h"
#include "cheats.h"
#include "apps.h"
#include "store.h"
#include "nearby.h"
#include "xfer.h"
#include <dirent.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>

#ifdef ARM9
#include <nds.h>
#endif

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
typedef enum { SCR_HOME, SCR_ALL, SCR_KEYBOARD, SCR_SETTINGS, SCR_NOTES, SCR_FATAL, SCR_LOGIN, SCR_STATS, SCR_FOLDERS, SCR_PAD, SCR_HOMEEDIT, SCR_PHOTOS, SCR_CHEATS, SCR_APPS, SCR_STORE, SCR_NEARBY, SCR_TOUCHCAL, SCR_WIFI } Screen;
typedef enum { OVL_NONE, OVL_OPTIONS, OVL_DIALOG, OVL_PIN, OVL_MENU } Overlay;
typedef enum { KB_SEARCH, KB_PROFILE, KB_NOTE, KB_NEWPROFILE, KB_FOLDER, KB_FOLDER_RENAME, KB_GREETING, KB_WIFIPASS } KbMode;
typedef enum { DLG_INFO, DLG_DELETE, DLG_DELPROF, DLG_DELFOLDER, DLG_OLDMENU, DLG_WIFI, DLG_WPRESET } DialogKind;

#define HOME_MAX     14
#define HOME_CARD   (-2)
#define HOME_ALLSW  (-3)
#define HOME_PAD    (-4)
#define HOME_PHOTOS (-5)
#define HOME_STORE  (-6)
#define HOME_NEARBY (-7)
#define TILE        72
#define TILE_GAP    8
#define TILE_Y      40
#define TILE_X0     16
#define HB_Y        141
#define HB_R        15

enum { HB_CARD, HB_ALL, HB_SEARCH, HB_STORE, HB_PAD, HB_NEARBY, HB_PHOTOS, HB_SETTINGS, HB_POWER, HB_COUNT };

#define GRID_COLS   4
#define GRID_TILE   56
#define GRID_STRIDE 61
#define GRID_X0     8
#define GRID_Y0     48
#define GRID_BOTTOM 170

static SystemSettings s_cfg;
static PlaytimeDB     s_pt;
static int            s_frame;
static Screen         s_scr = SCR_HOME;
static Overlay        s_ovl = OVL_NONE;
static int            s_fade = 0;
static bool           s_fade_slow = false;   // boot without animation: ~0.5 s fade-in
static char           s_drv[128];

// HOME
static int  s_home[HOME_MAX + 5];
static int   s_phm_rows[8], s_phm_n;   // Photos > "Use as…" menu rows
static int  *s_pick_items;    // games for "Set as game cover"
static int   s_pick_n;
static bool s_ss_on, s_ss_preview;   // screensaver running (preview: started from Photos)
// Nearby photos: the one I share, the selected friend's
static Color  *s_nb_my_ph;      // (heap, while Nearby is open)
static Color  *s_nb_ph;
static uint32_t s_nb_ph_id;      // whose photo s_nb_ph holds (0 = to be read)
static bool    s_nb_ph_ok;
static bool  s_ph_scanned;    // photos_scan() ran since the start
static bool s_back_from_game;       // this start came back from a game (no boot animation)
static bool s_photos_exist;          // a DCIM or photos folder is on the card (checked at start)
static int  s_home_n, s_home_sel, s_home_focus, s_home_btn;
static int  s_home_scroll, s_home_scroll_target; // pixels
static bool s_home_dragging;
static int  s_drag_origin_scroll;

// ALL SOFTWARE
static int      *s_view;            // library indices
static int       s_view_n, s_grid_sel, s_grid_scroll, s_grid_scroll_target;
static ItemType  s_tabs[32];
#define COLL_TAB(i) ((ItemType)(64 + (i)))   // tab of folder i
static int       s_tab_n, s_tab;
static bool      s_grid_dragging;
static int       s_tab_scroll;

// SEARCH / KEYBOARD
static KbMode s_kb_mode;
static Screen s_kb_return;
static char   s_kb[72];          // (a Wi-Fi password has up to 63 characters)
static bool   s_kb_caps;         // Wi-Fi password: capital letters
static char   s_wf_ssid[33];     // Settings > Wi-Fi: the network being joined
static int    s_results[48];
static int    s_res_n, s_res_sel;
static bool   s_kb_sym;            // keyboard shows the symbol page

// SETTINGS
enum { SET_PROFILE, SET_COLOR, SET_LOCK, SET_STATS, SET_SWITCH, SET_HOME, SET_PAD, SET_THEME, SET_SPLASH, SET_LOGIN, SET_LIDLOGIN, SET_LIDSOUND, SET_LANG, SET_SOUNDS,
       SET_VOLUME, SET_BRIGHT, SET_COVERS, SET_ANIM, SET_GAMEINFO, SET_TOUCH, SET_SORT, SET_SLOT, SET_HIDDEN, SET_DELPROF, SET_RESCAN, SET_OLDMENU, SET_ABOUT,
       SET_BETA, SET_WIFI, SET_SAVER, SET_SAVER_SRC, SET_FRAME, SET_POD, SET_WPRESET, SET_COUNT };
static int s_set_sel, s_set_scroll;        // s_set_sel: the SET_* row; s_set_scroll: rows of the tab
// Settings in five tabs (L / R or a tap on the tab), like the Switch's System Settings
#define SET_TABS 5
#define SET_TAB_MAX 12
static const int8_t k_set_tabs[SET_TABS][SET_TAB_MAX] = {
    { SET_PROFILE, SET_COLOR, SET_LOCK, SET_STATS, SET_SWITCH, SET_DELPROF, -1 },
    { SET_THEME, SET_WPRESET, SET_SPLASH, SET_HOME, SET_COVERS, SET_ANIM, SET_GAMEINFO, SET_SAVER, SET_SAVER_SRC, SET_FRAME, SET_POD, -1 },
    { SET_SOUNDS, SET_VOLUME, SET_BRIGHT, SET_TOUCH, -1 },
    { SET_SORT, SET_SLOT, SET_HIDDEN, SET_RESCAN, -1 },
    { SET_LANG, SET_WIFI, SET_LOGIN, SET_LIDLOGIN, SET_PAD, SET_BETA, SET_OLDMENU, SET_ABOUT },
};
static int s_set_tab, s_set_pos;           // tab, row within the tab
static const StrId k_set_tab_name[SET_TABS] = { S_ST_PROFILE, S_ST_LOOK, S_ST_SOUND, S_ST_GAMES, S_ST_SYSTEM };
// "Back to my old menu" is listed only when Studio kept the menu the card had before DSplay
static bool s_has_oldmenu;
static bool set_row_shown(int id) { return id != SET_OLDMENU || s_has_oldmenu; }
static int set_row(int t, int p) {          // p-th row shown in tab t (-1 = none)
    for (int i = 0; i < SET_TAB_MAX && k_set_tabs[t][i] >= 0; i++)
        if (set_row_shown(k_set_tabs[t][i]) && p-- == 0) return k_set_tabs[t][i];
    return -1;
}
static int set_tab_n(int t) { int n = 0; while (set_row(t, n) >= 0) n++; return n; }
static void set_go(int tab, int pos) {
    s_set_tab = (tab + SET_TABS) % SET_TABS;
    int n = set_tab_n(s_set_tab);
    s_set_pos = pos < 0 ? 0 : pos >= n ? n - 1 : pos;
    s_set_sel = set_row(s_set_tab, s_set_pos);
}
static int s_he_sel, s_he_scroll;
static int s_ch_sel, s_ch_scroll, s_ch_lib, s_ch_ver;
static Screen s_ch_back;
static void open_cheats(int lib_idx);
static void open_apps(void);
static void render_top_apps(void);
static GameItem s_ap_list[APPS_MAX];
static uint8_t  s_ap_on[APPS_MAX];
static int s_ap_n, s_ap_sel, s_ap_scroll, s_ap_stage, s_ap_ver;   // stage 0: "reading" shown, 1: read now, 2: ready
#define CH_Y0   26
#define CH_RH   19
#define CH_VIEW 6

// OPTIONS
enum { OPT_START, OPT_FAV, OPT_SLOT, OPT_NOTES, OPT_RESTORE, OPT_HIDE, OPT_DELETE, OPT_FOLDER, OPT_CARDSAVE, OPT_GAMECFG, OPT_CHEATS, OPT_APP_REMOVE };
static int s_opt_rows[12], s_opt_n, s_opt_sel, s_opt_scroll;
#define OPT_VIS 8      // rows that fit above the hint bar; the sheet scrolls beyond that
static int s_opt_lib;               // library index of the item (or HOME_CARD)
static char s_toast[64];
static int  s_toast_t;

// DIALOG
static DialogKind s_dlg_kind;
static char       s_dlg_title[80], s_dlg_msg[256];
static IconId     s_dlg_icon;
static int        s_dlg_nbtn, s_dlg_sel;
static Overlay    s_dlg_prev_ovl;

// PROFILES / LOGIN
static int s_cur_profile = -1;      // profile slot that is logged in (-1 = login screen at boot)
static int s_login_sel;             // position on the login screen (profiles, then "new profile")
static int s_login_wp_slot = -1;    // whose wallpaper is loaded for the login screen
static int s_login_wp_wait;         // frames until the wallpaper of the selected profile is loaded

// PIN entry
typedef enum { PIN_LOGIN, PIN_SET_NEW, PIN_SET_CONFIRM, PIN_UNLOCK_REMOVE, PIN_UNLOCK_DELETE } PinMode;
static PinMode s_pin_mode;
static int     s_pin_slot;
static uint8_t s_pin[PIN_LEN], s_pin_first[PIN_LEN];
static int     s_pin_n;
static int     s_pin_shake;         // frames of the "wrong code" shake
static Screen  s_pin_return;

// STATS
static uint32_t s_st_total, s_st_games, s_st_launches, s_st_week[7];
static int      s_st_top[5], s_st_top_lib[5], s_st_top_n;
static ItemType s_st_sys[5];
static uint32_t s_st_sys_min[5];
static int      s_st_sys_n;

// FOLDERS
static int    s_fold_sel, s_fold_scroll;
static int    s_pick_coll = -1;     // >= 0: All Games is used to tick the games of this folder
// small list overlay (folder options, color choice, "add to folder")
typedef enum { MENU_FOLDER_OPTS, MENU_COLOR, MENU_ADD_TO, MENU_GAMECFG, MENU_POWER, MENU_PHOTO, MENU_PICK_GAME, MENU_PICK_FOLDER } MenuKind;
static MenuKind s_menu_kind;
static int      s_menu_sel, s_menu_n, s_menu_target;
static int      s_menu_ver;          // bumped when a menu value changes (redraw)
static int      s_menu_scroll;
static bool     s_menu_new;         // color chosen for a folder that was just created

// NOTES
static char s_notes[2048];
static int  s_notes_scroll;
static int  s_notes_lib;

// touch
static bool    s_touch_active, s_touch_moved;
static int     s_touch_x0, s_touch_y0, s_touch_x, s_touch_y;
static HitRect s_hits[6];
static int     s_hit_n;

// Render caches: a screen is fully redrawn only when its content changed
static Color    s_cache_top_px[SCREEN_WIDTH * SCREEN_HEIGHT];
static Color    s_cache_bot_px[SCREEN_WIDTH * SCREEN_HEIGHT];
static Canvas   s_cache_top, s_cache_bot;
static uint32_t s_last_sig, s_last_sig_top;
static bool     s_force_redraw = true;
static bool     s_present_top = true, s_present_bot = true;

static void render_pin(void);
static Color coll_rgb(int i);
static void folder_tile(Canvas *c, int x, int y, int size, Color col, const char *name, int count, bool big, const Color *pic);
static int  menu_rows_y0(int *h_out);
static void menu_activate(void);
static int  fold_items(void);
static void open_all(ItemType tab);
static void open_pad(void);
static void open_photos(void);
static void photos_tile(Canvas *c, int x, int y, int size);
static void render_top_photos(void);
static void render_photos(void);
static void input_photos(u32 k, u32 rep);
static void photos_step(int d);
static void tap_photos(int x, int y);
static void ph_menu_activate(int i);
static void ph_start_crop(int kind, int target);
static const Color *folder_pic(int i);
static void ph_crop_drag(const AppInput *in);
static void ss_frame(void);
static void ss_stop(void);
static void ss_check_idle(void);
static void render_top_ss(void);
static void render_bottom_ss(void);
static bool pod_ready(void);
static Color *s_pod_px;
static int    s_pod_day = -1;
static void close_pad(void);
static void pad_tile(Canvas *c, int x, int y, int size);
static void store_tile(Canvas *c, int x, int y, int size);
static void open_store(void);
static void open_touchcal(void);
static int  touchcal_sig(void);
static void render_top_touchcal(void);
static void render_touchcal(void);
static void touchcal_touch(const AppInput *in);
static void input_touchcal(u32 k);

// Touch feedback (like the Switch): the spot under the stylus lights up at once, a pressed tile
// or button sinks in, and a ring spreads out when the tap is taken
static bool s_fx_on;                 // stylus down, not dragging
static int  s_fx_x, s_fx_y, s_fx_t;  // s_fx_t: frames of the release ring (0 = none)
// A pressed: the selected element (its selection frame) gets the same press effect as a tap
static int  s_kp_t;                   // frames since A (0 = none)
static int  s_lv_t, s_lv_kind, s_lv_val, s_lv_max = 1;   // volume / brightness indicator (frames left)
static int  s_kp_x, s_kp_y, s_kp_w, s_kp_h, s_kp_r, s_kp_scr, s_kp_ovl;
static bool pressed_at(int x, int y, int w, int h) {
    return s_fx_on && s_fx_x >= x && s_fx_x < x + w && s_fx_y >= y && s_fx_y < y + h;
}
static void close_store(void);
static void render_top_store(void);
static void render_store(void);
static void input_store(u32 k, u32 rep);
static void store_frame(const AppInput *in);
static void store_touch(int x, int y);
static int  s_store_ver;
static void nearby_tile(Canvas *c, int x, int y, int size);
static void open_nearby(void);
static void close_nearby(void);
static void open_wifi(void);
static void wifi_keep_old(bool restore);
static void render_wifi(void);
static void render_top_wifi(void);
static void input_wifi(u32 k, u32 rep);
static void wifi_screen_frame(void);
static void render_top_nearby(void);
static void render_nearby(void);
static void input_nearby(u32 k, u32 rep);
static void nearby_app_frame(void);
static void nearby_touch(int x, int y);
static void nearby_save_greeting(const char *text);
static int  s_nb_ver;
static void open_menu(MenuKind kind, int target);
static void card_save_backup(void);
static void render_menu(void);
static void render_folders(void);
static void render_top_folders(void);
static void input_folders(u32 k, u32 rep);
static void input_menu(u32 k, u32 rep);
static void open_folders(void);
static void fmt_minutes(uint32_t m, char *out, size_t len);
static void profile_login(int slot);
static void profile_paths_for(int slot);
static int  login_items(void);
static int  login_slot_at(int i);

static uint8_t L(void) { return s_cfg.language; }
static void sfx(SfxEvent e) { sound_play(e, s_cfg.sfx_pack); }

static const GameItem *item_for(int lib_idx) {
    if (lib_idx == HOME_CARD) return slot1_item();
    return library_get(lib_idx);
}

// ---------------------------------------------------------------------------
// Data refresh
// ---------------------------------------------------------------------------
static int home_games_count(void) {
    if (s_cfg.home_games == HOME_GAMES_NONE) return 0;
    return (s_cfg.home_games == 0 || s_cfg.home_games > HOME_MAX) ? HOME_MAX : s_cfg.home_games;
}

static void build_home(void) {
    int prev_id = (s_home_n > 0 && s_home_sel < s_home_n) ? s_home[s_home_sel] : -1;
    s_home_n = 0;
    if (slot1_present() && !(s_cfg.home_hide & HOMEHIDE_CARD_TILE)) s_home[s_home_n++] = HOME_CARD;
    static int all[MAX_LIBRARY_ITEMS];
    int total = library_view(all, MAX_LIBRARY_ITEMS, TYPE_UNKNOWN, (SortMode)s_cfg.sort_mode, s_cfg.show_hidden != 0, &s_pt);
    int games = home_games_count();
    for (int i = 0; i < total && i < games; i++) s_home[s_home_n++] = all[i];
    if (!(s_cfg.home_hide & HOMEHIDE_ALL_TILE) || s_home_n == 0) s_home[s_home_n++] = HOME_ALLSW;
    if (s_cfg.home_show & HOMESHOW_PAD) s_home[s_home_n++] = HOME_PAD;
    if (s_photos_exist && !(s_cfg.home_show & HOMEOPT_NO_PHOTOS)) s_home[s_home_n++] = HOME_PHOTOS;
    if (s_cfg.beta && !(s_cfg.home_show & HOMEOPT_NO_NEARBY)) s_home[s_home_n++] = HOME_NEARBY;   // beta
    s_home_sel = 0;
    for (int i = 0; i < s_home_n; i++) if (s_home[i] == prev_id) { s_home_sel = i; break; }
}

static void build_tabs(void) {
    ItemType cur = (s_tab_n > 0 && s_tab < s_tab_n) ? s_tabs[s_tab] : TYPE_UNKNOWN;
    s_tab_n = 0;
    if (apps_available()) s_tabs[s_tab_n++] = TYPE_APP;   // DSi applications first
    s_tabs[s_tab_n++] = TYPE_UNKNOWN; // all
    s_tabs[s_tab_n++] = TYPE_FOLDER;  // favorites (L / R on the home screen)
    for (int i = 0; i < coll_count() && s_tab_n < 20; i++) s_tabs[s_tab_n++] = COLL_TAB(i);
    ItemType sys[10];
    int ns = library_systems(sys, 10, s_cfg.show_hidden != 0);
    for (int i = 0; i < ns && s_tab_n < 32; i++) s_tabs[s_tab_n++] = sys[i];
    s_tab = 0;
    for (int i = 0; i < s_tab_n; i++) if (s_tabs[i] == cur) { s_tab = i; break; }
}

static void build_view(void) {
    int prev = (s_view_n > 0 && s_grid_sel < s_view_n) ? s_view[s_grid_sel] : -1;
    ItemType tab = (s_tab_n > 0 && s_tab < s_tab_n) ? s_tabs[s_tab] : TYPE_UNKNOWN;
    if ((int)tab >= 64) {
        // a folder: every game, then keep the ones in the folder
        int c = (int)tab - 64, n = library_view(s_view, MAX_LIBRARY_ITEMS, TYPE_UNKNOWN, (SortMode)s_cfg.sort_mode, true, &s_pt);
        s_view_n = 0;
        for (int i = 0; i < n; i++) if (coll_has(c, library_get(s_view[i])->path)) s_view[s_view_n++] = s_view[i];
    } else {
        s_view_n = library_view(s_view, MAX_LIBRARY_ITEMS, tab, (SortMode)s_cfg.sort_mode, s_cfg.show_hidden != 0 || s_pick_coll >= 0, &s_pt);
    }
    s_grid_sel = 0;
    for (int i = 0; i < s_view_n; i++) if (s_view[i] == prev) { s_grid_sel = i; break; }
}

static void refresh_all(void) {
    ui_tile_cache_reset();
    build_home();
    build_tabs();
    build_view();
}

// ---------------------------------------------------------------------------
// Dialogs, toast
// ---------------------------------------------------------------------------
static void show_dialog(DialogKind kind, IconId icon, const char *title, const char *msg) {
    s_dlg_prev_ovl = (s_ovl == OVL_DIALOG) ? OVL_NONE : s_ovl;
    s_ovl = OVL_DIALOG;
    s_dlg_kind = kind;
    s_dlg_icon = icon;
    snprintf(s_dlg_title, sizeof(s_dlg_title), "%s", title);
    snprintf(s_dlg_msg, sizeof(s_dlg_msg), "%s", msg ? msg : "");
    s_dlg_nbtn = (kind == DLG_DELETE || kind == DLG_DELPROF || kind == DLG_DELFOLDER || kind == DLG_OLDMENU || kind == DLG_WIFI || kind == DLG_WPRESET) ? 2 : 1;
    s_dlg_sel = 0;
    sfx(kind == DLG_INFO ? SFX_ERROR : SFX_MODAL);
}

static void toast(const char *msg) {
    snprintf(s_toast, sizeof(s_toast), "%s", msg);
    s_toast_t = 120;
}

// ---------------------------------------------------------------------------
// Launch
// ---------------------------------------------------------------------------
static void render_top(void);
static void render_bottom(void);

#ifdef ARM9
static void hw_master(int level) {
    int base = (5 - s_cfg.brightness) * 2;
    if (level < base) level = base;
    if (level > 16) level = 16;
    u16 v = level ? (u16)((2 << 14) | level) : 0;
    REG_MASTER_BRIGHT = v;
    REG_MASTER_BRIGHT_SUB = v;
}
#endif

// Screens dim after 2 minutes without input (saves battery); the first press only wakes them
#define IDLE_DIM_FRAMES (60 * 120)
#define IDLE_DIM_LEVEL  11
static uint32_t s_idle;
static bool     s_wake_hold;

static uint32_t idle_dim_frames(void);
int app_fade_level(void) {
    int base = (5 - s_cfg.brightness) * 2;
    uint32_t dim_at = idle_dim_frames();
    if (s_idle >= dim_at) {
        uint32_t ramp = (s_idle - dim_at) / 3;   // ~0.5 s fade
        int dim = ramp > IDLE_DIM_LEVEL ? IDLE_DIM_LEVEL : (int)ramp;
        if (dim > base) base = dim;
    }
    return s_fade > base ? s_fade : base;
}

static void present_frame(void) {
    ui_rings_reset(&g_top);
    ui_rings_reset(&g_bot);
    render_top();
    render_bottom();
    for (int i = 0; i < ui_rings_top_n; i++) ui_ring_draw(&ui_rings_top[i], s_frame);
    for (int i = 0; i < ui_rings_bot_n; i++) ui_ring_draw(&ui_rings_bot[i], s_frame);
    s_force_redraw = true;
#ifdef ARM9
    swiWaitForVBlank();
    hw_master(app_fade_level());
#endif
    gfx_present(true, true);
}

static void fade(bool out) {
    for (int i = 0; i <= 16; i += 2) {
        s_fade = out ? i : 16 - i;
        s_frame++;
        present_frame();
    }
}

static bool s_launching;
static int  s_launch_lib;

// Before another program starts: black picture, normal brightness. The menu fades out with
// the display's master brightness; programs that never reset it (several emulators, their
// menus on the lower screen) would otherwise stay dark - also after leaving the game.
static void hand_over_display(void) {
    gfx_clear(&g_top, (Color)0x8000);
    gfx_clear(&g_bot, (Color)0x8000);
#ifdef ARM9
    swiWaitForVBlank();
    gfx_present(true, true);
    swiWaitForVBlank();
    REG_MASTER_BRIGHT = 0;
    REG_MASTER_BRIGHT_SUB = 0;
    lcdMainOnTop();
#endif
}

static void launch_error(LaunchResult r, const char *detail, int code) {
    char msg[256];
    switch (r) {
        case LAUNCH_ERR_GBA_BIOS: snprintf(msg, sizeof(msg), "%s", tr(S_ERR_GBA_BIOS, L())); break;
        case LAUNCH_ERR_NO_BOOTSTRAP: snprintf(msg, sizeof(msg), "%s", tr(S_ERR_BOOTSTRAP, L())); break;
        case LAUNCH_ERR_MISSING_EMU: snprintf(msg, sizeof(msg), "%s\n%s", tr(S_ERR_MISSING_EMU, L()), path_without_drive(detail)); break;
        case LAUNCH_ERR_NO_FILE: snprintf(msg, sizeof(msg), "%s", tr(S_ERR_NO_FILE, L())); break;
        case LAUNCH_ERR_LOADER: {
            char fmt[96];
            snprintf(fmt, sizeof(fmt), tr(S_ERR_LOADER, L()), code);
            snprintf(msg, sizeof(msg), "%s\n%s", fmt, path_without_drive(detail));
            break;
        }
        default: snprintf(msg, sizeof(msg), "-"); break;
    }
    show_dialog(DLG_INFO, ICON_WARNING_L, tr(S_ERR_TITLE, L()), msg);
}

// a launch that did not happen: the next start is a normal one again
static void launch_cancelled(void) {
#ifdef ARM9
    profiles_global()->resume = 0;
    profiles_global_save();
    launcher_mark_game_start_cancel();
#endif
}

static void do_launch(int lib_idx) {
    const GameItem *it = item_for(lib_idx);
    if (!it) { sfx(SFX_ERROR); return; }
    if (lib_idx == HOME_CARD) {
        sfx(SFX_LAUNCH);
        settings_save(&s_cfg);
        profiles_global()->resume = 1;          // back from the card: straight to this profile's HOME
        profiles_global_save();
        launcher_mark_game_start();
        s_launching = true; s_launch_lib = lib_idx;
        fade(true);
        hand_over_display();
        launch_slot1_card();
        launch_cancelled();                     // (only returns when the card could not start)
        s_launching = false;
        fade(false);
        return;
    }
    // validate first so errors appear instantly
    LaunchPlan plan;
    char detail[MAX_PATH_LEN];
    LaunchResult r = launcher_plan(it, &s_cfg, &plan, detail, sizeof(detail));
    if (r != LAUNCH_OK) { launch_error(r, detail, 0); return; }

    sfx(SFX_LAUNCH);
    settings_save(&s_cfg);
    playtime_begin_session(&s_pt, it);
    profiles_note_launch(s_cur_profile, it->title);
    profiles_global()->resume = 1;              // back from the game: straight to this profile's HOME
    profiles_global_save();
    launcher_mark_game_start();                 // tells the next start apart from switching the console on
    s_launching = true;
    s_launch_lib = lib_idx;
    for (int i = 0; i < 24; i++) { s_frame++; present_frame(); }
    fade(true);
    hand_over_display();
    int code = 0;
    r = launcher_run(it, &s_cfg, detail, sizeof(detail), &code);
    s_launching = false;
#ifdef ARM9
    // Only reached when the launch failed
    launch_cancelled();
    char sp[MAX_PATH_LEN];
    stats_path(sp, sizeof(sp), "session.bin");
    unlink(sp);
    fade(false);
    launch_error(r, detail, code);
#else
    (void)r;
    s_fade = 0;
#endif
}

// ---------------------------------------------------------------------------
// Options sheet
// ---------------------------------------------------------------------------
static void open_options(int lib_idx) {
    const GameItem *it = item_for(lib_idx);
    if (!it) { sfx(SFX_ERROR); return; }
    s_opt_lib = lib_idx;
    s_opt_n = 0;
    s_opt_rows[s_opt_n++] = OPT_START;
    if (it->type == TYPE_APP) {
        s_opt_rows[s_opt_n++] = OPT_FAV;
        s_opt_rows[s_opt_n++] = OPT_FOLDER;
        s_opt_rows[s_opt_n++] = OPT_HIDE;
        s_opt_rows[s_opt_n++] = OPT_APP_REMOVE;
    } else if (lib_idx != HOME_CARD) {
        s_opt_rows[s_opt_n++] = OPT_FAV;
        s_opt_rows[s_opt_n++] = OPT_FOLDER;
        if (it->type == TYPE_NDS && !(it->flags & ITEM_FLAG_HOMEBREW)) s_opt_rows[s_opt_n++] = OPT_SLOT;
        if (it->type == TYPE_NDS && !(it->flags & ITEM_FLAG_HOMEBREW)) s_opt_rows[s_opt_n++] = OPT_GAMECFG;
        if (it->type == TYPE_NDS && !(it->flags & ITEM_FLAG_HOMEBREW)) s_opt_rows[s_opt_n++] = OPT_CHEATS;
        s_opt_rows[s_opt_n++] = OPT_NOTES;
        s_opt_rows[s_opt_n++] = OPT_RESTORE;
        s_opt_rows[s_opt_n++] = OPT_HIDE;
        s_opt_rows[s_opt_n++] = OPT_DELETE;
    } else {
        s_opt_rows[s_opt_n++] = OPT_CARDSAVE;
    }
    s_opt_sel = 0;
    s_opt_scroll = 0;
    s_ovl = OVL_OPTIONS;
    sfx(SFX_MODAL);
}

static void load_notes(int lib_idx) {
    s_notes[0] = '\0';
    s_notes_scroll = 0;
    s_notes_lib = lib_idx;
    const GameItem *it = library_get(lib_idx);
    if (!it) return;
    char p[MAX_PATH_LEN];
    snprintf(p, sizeof(p), "%s", it->path);
    char *dot = strrchr(p, '.');
    if (dot) strcpy(dot, ".txt");
    FILE *f = fopen(p, "r");
    if (!f) return;
    size_t n = fread(s_notes, 1, sizeof(s_notes) - 1, f);
    s_notes[n] = '\0';
    fclose(f);
}

static void append_note(const char *text) {
    const GameItem *it = library_get(s_notes_lib);
    if (!it || !text[0]) return;
    char p[MAX_PATH_LEN];
    snprintf(p, sizeof(p), "%s", it->path);
    char *dot = strrchr(p, '.');
    if (dot) strcpy(dot, ".txt");
    FILE *f = fopen(p, "a");
    if (!f) return;
    fprintf(f, "%s\n", text);
    fclose(f);
    load_notes(s_notes_lib);
}

static void open_keyboard(KbMode mode, const char *initial) {
    s_kb_mode = mode;
    s_kb_sym = false;
    s_kb_return = s_scr;
    snprintf(s_kb, sizeof(s_kb), "%s", initial ? initial : "");
    s_res_n = 0;
    s_res_sel = 0;
    s_scr = SCR_KEYBOARD;
    sfx(SFX_MODAL);
}

static void delete_current(void) {
    GameItem *it = library_get(s_opt_lib);
    if (!it) return;
    launcher_keep_saves(it);          // saves are never erased: they move to _hyper/deleted_saves
    remove(it->path);
    pathset_forget(it->path);
    coll_forget(it->path);
    coll_save();
    library_remove(s_opt_lib);
    covers_reset();
    refresh_all();
    sfx(SFX_BACK);
}

// --- Going back to the menu the card had before DSplay --------------------------
// Studio keeps the old BOOT.NDS in _hyper/restore/original_boot.nds (older Studio versions:
// _hyper/backups/BOOT.NDS.<date>.bak). Only BOOT.NDS changes: games, saves and HyperLauncher.nds
// stay on the card, so DSplay can still be started from the other menu.
// true for the banner of DSplay / HyperLauncher (a copy of DSplay itself is no "old menu")
static bool nds_is_dsplay(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[0x6C];
    uint16_t t[128];
    bool ok = fread(h, 1, sizeof(h), f) == sizeof(h);
    uint32_t off = ok ? (uint32_t)h[0x68] | (uint32_t)h[0x69] << 8 | (uint32_t)h[0x6A] << 16 | (uint32_t)h[0x6B] << 24 : 0;
    ok = ok && off >= 0x200 && fseek(f, (long)off + 0x340, SEEK_SET) == 0 && fread(t, 2, 128, f) == 128;
    fclose(f);
    if (!ok) return false;
    char a[129];
    for (int i = 0; i < 128; i++) a[i] = (t[i] && t[i] < 128) ? (char)t[i] : (t[i] ? '?' : '\0');
    a[128] = '\0';
    return strstr(a, "DSplay") || strstr(a, "HyperLauncher");
}

static bool old_menu_path(char *out, size_t len) {
    snprintf(out, len, "%s/_hyper/restore/original_boot.nds", s_drv);
    if (file_exists(out) && !nds_is_dsplay(out)) return true;
    char dir[MAX_PATH_LEN], best[128] = "";
    snprintf(dir, sizeof(dir), "%s/_hyper/backups", s_drv);
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n > 13 && n < sizeof(best) && !strncasecmp(e->d_name, "BOOT.NDS.", 9) && !strcasecmp(e->d_name + n - 4, ".bak") &&
            strcmp(e->d_name, best) > 0) {
            char full[MAX_PATH_LEN + 128];
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            if (!nds_is_dsplay(full)) snprintf(best, sizeof(best), "%s", e->d_name);
        }
    }
    closedir(d);
    if (!best[0]) return false;
    snprintf(out, len, "%s/%s", dir, best);
    return true;
}

static void restore_old_menu(void) {
    char src[MAX_PATH_LEN], boot[MAX_PATH_LEN], hl[MAX_PATH_LEN];
    if (!old_menu_path(src, sizeof(src))) return;
    snprintf(boot, sizeof(boot), "%s/BOOT.NDS", s_drv);
    snprintf(hl, sizeof(hl), "%s/HyperLauncher.nds", s_drv);
    if (!file_exists(hl)) copy_file(boot, hl);          // DSplay stays startable
    settings_save(&s_cfg);
    if (copy_file(src, boot)) {
        sfx(SFX_DECIDE);
        show_dialog(DLG_INFO, ICON_POWER_L, tr(S_SET_OLDMENU, L()), tr(S_OLDMENU_DONE, L()));
    } else {
        sfx(SFX_ERROR);
        show_dialog(DLG_INFO, ICON_WARNING_L, tr(S_SET_OLDMENU, L()), tr(S_OLDMENU_FAIL, L()));
    }
}

// Copies the save chip of the inserted game card to <drv>/_hyper/cardsaves/<CODE>/<CODE>_<date>.sav
static void card_save_backup(void) {
    const GameItem *ci = slot1_item();
    if (!ci) { sfx(SFX_ERROR); return; }
    toast(tr(S_CARDSAVE_BUSY, L()));
    present_frame();
    uint8_t *data = NULL;
#ifdef ARM9
    uint32_t max = isDSiMode() ? 8u * 1024 * 1024 : 1024u * 1024;
#else
    uint32_t max = 1024u * 1024;
#endif
    uint32_t size = slot1_read_save(&data, max);
    if (!size || !data) { free(data); toast(tr(S_CARDSAVE_FAIL, L())); sfx(SFX_ERROR); return; }
    char dir[MAX_PATH_LEN], path[MAX_PATH_LEN], stamp[32];
    char code[5] = { 0 };
    memcpy(code, ci->game_code[0] ? ci->game_code : "CARD", 4);
    snprintf(dir, sizeof(dir), "%s/_hyper/cardsaves/%s", s_drv, code);
    make_dirs(dir);
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (tmv) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tmv);
    else snprintf(stamp, sizeof(stamp), "%lu", (unsigned long)now);
    snprintf(path, sizeof(path), "%s/%s_%s.sav", dir, code, stamp);
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(data, 1, size, f) == size;
    if (f) fclose(f);
    free(data);
    if (!ok) { toast(tr(S_CARDSAVE_FAIL, L())); sfx(SFX_ERROR); return; }
    char msg[96];
    unsigned kb = size / 1024;
    char nm[48];
    snprintf(nm, sizeof(nm), "%s_%s.sav (%u KB)", code, stamp, kb ? kb : 1);
    snprintf(msg, sizeof(msg), tr(S_CARDSAVE_OK, L()), nm);
    toast(msg);
    sfx(SFX_DECIDE);
}

static void options_activate(int dir) {
    GameItem *it = (s_opt_lib == HOME_CARD) ? NULL : library_get(s_opt_lib);
    int row = s_opt_rows[s_opt_sel];
    switch (row) {
        case OPT_START:
            s_ovl = OVL_NONE;
            do_launch(s_opt_lib);
            break;
        case OPT_FAV:
            if (!it) break;
            pathset_set_favorite(it->path, !(it->flags & ITEM_FLAG_FAVORITE));
            library_refresh_flags();
            build_tabs();
            sfx(SFX_FAVORITE);
            break;
        case OPT_SLOT:
            s_cfg.save_slot = (uint8_t)((s_cfg.save_slot + (dir < 0 ? 2 : 1)) % 3);
            settings_save(&s_cfg);
            sfx(SFX_TOGGLE);
            break;
        case OPT_NOTES:
            s_ovl = OVL_NONE;
            load_notes(s_opt_lib);
            s_scr = SCR_NOTES;
            sfx(SFX_DECIDE);
            break;
        case OPT_RESTORE:
            if (it && restore_game_save_backup(it, s_cfg.save_slot)) { toast(tr(S_RESTORE_OK, L())); sfx(SFX_TOGGLE); }
            else { toast(tr(S_RESTORE_NONE, L())); sfx(SFX_ERROR); }
            break;
        case OPT_HIDE:
            if (!it) break;
            pathset_set_hidden(it->path, !(it->flags & ITEM_FLAG_HIDDEN));
            library_refresh_flags();
            s_ovl = OVL_NONE;
            refresh_all();
            sfx(SFX_TOGGLE);
            break;
        case OPT_DELETE:
            show_dialog(DLG_DELETE, ICON_TRASH, tr(S_DELETE_TITLE, L()), tr(S_DELETE_MSG, L()));
            s_dlg_sel = 1; // default to Cancel
            break;
        case OPT_FOLDER:
            if (!coll_count()) { toast(tr(S_NO_FOLDERS, L())); sfx(SFX_ERROR); break; }
            open_menu(MENU_ADD_TO, s_opt_lib);
            break;
        case OPT_CARDSAVE:
            s_ovl = OVL_NONE;
            card_save_backup();
            break;
        case OPT_GAMECFG:
            open_menu(MENU_GAMECFG, s_opt_lib);
            break;
        case OPT_CHEATS:
            open_cheats(s_opt_lib);
            break;
        case OPT_APP_REMOVE:
            if (!it) break;
            apps_remove(it->path);
            apps_save();
            s_ovl = OVL_NONE;
            library_merge_apps();
            refresh_all();
            sfx(SFX_BACK);
            break;
    }
}

// ---------------------------------------------------------------------------
// Settings actions
// ---------------------------------------------------------------------------
// Theme + the accent color of the logged-in profile (+ wallpaper softened for the theme)
static void apply_theme(void) {
    theme_apply(s_cfg.theme);
    const ProfileInfo *pi = profiles_get(s_cur_profile >= 0 ? s_cur_profile : profiles_global()->last);
    uint16_t a, h, sl, sd;
    profile_color(pi ? pi->color : 0, &a, &h, &sl, &sd);
    theme_set_accent(a, h, sl, sd);
    ui_avatar_select(s_cur_profile >= 0 ? s_cur_profile : profiles_global()->last, a);
    ui_wallpaper_retint();
}

static void apply_settings(void) {
    apply_theme();
    sound_set_volume(s_cfg.volume);
    settings_save(&s_cfg);
}

static void rescan(void);

// Box art is optional: when off the SD card is never searched for pictures
static void apply_covers(void) {
    if (s_cfg.covers) covers_init(s_drv);
    else covers_disable();
    touchcal_load(s_drv);
    ui_tile_cache_reset();
}

static void open_pin(PinMode mode, int slot);
static void open_stats(void);
static void open_login(int preselect, bool ask_pin);

static Color profile_accent(int slot) {
    const ProfileInfo *pi = profiles_get(slot);
    uint16_t a;
    profile_color(pi ? pi->color : 0, &a, NULL, NULL, NULL);
    return a;
}

static void setting_change(int row, int dir) {
    switch (row) {
        case SET_COLOR: {
            const ProfileInfo *pi = profiles_get(s_cur_profile);
            if (!pi) return;
            ProfileInfo t = *pi;
            t.color = (uint8_t)((t.color + PROFILE_COLORS + dir) % PROFILE_COLORS);
            profiles_update(s_cur_profile, &t);
            break;
        }
        case SET_LOCK:
            open_pin(profiles_has_pin(s_cur_profile) ? PIN_UNLOCK_REMOVE : PIN_SET_NEW, s_cur_profile);
            return;
        case SET_STATS: open_stats(); return;
        case SET_HOME: s_scr = SCR_HOMEEDIT; s_he_sel = 0; sfx(SFX_DECIDE); return;
        case SET_PAD:
            s_cfg.home_show ^= HOMESHOW_PAD;
            settings_save(&s_cfg);
            build_home();
            toast(tr((s_cfg.home_show & HOMESHOW_PAD) ? S_PAD_ADDED : S_PAD_REMOVED, L()));
            sfx(SFX_TOGGLE);
            return;
        case SET_SWITCH: settings_save(&s_cfg); sfx(SFX_DECIDE); open_login(s_cur_profile, false); return;
        case SET_LOGIN: profiles_global()->login ^= 1; profiles_global_save(); sfx(SFX_TOGGLE); return;
        case SET_LIDLOGIN: {          // closing the lid: login screen + sound, login only, sound only, nothing
            int st = (s_cfg.lid_login_off ? 2 : 0) | (s_cfg.lid_sound_off ? 1 : 0);
            static const int order[4] = { 0, 1, 2, 3 };
            int cur = 0;
            while (order[cur] != st) cur++;
            cur = (cur + 4 + (dir < 0 ? -1 : 1)) % 4;
            s_cfg.lid_login_off = (order[cur] & 2) ? 1 : 0;
            s_cfg.lid_sound_off = (order[cur] & 1) ? 1 : 0;
            break;
        }
        case SET_LIDSOUND: s_cfg.lid_sound_off ^= 1; break;
        case SET_DELPROF:
            if (profiles_count() <= 1) { toast(tr(S_ONLY_PROFILE, L())); sfx(SFX_ERROR); return; }
            if (profiles_has_pin(s_cur_profile)) { open_pin(PIN_UNLOCK_DELETE, s_cur_profile); return; }
            {
                char msg[160];
                snprintf(msg, sizeof(msg), tr(S_DELPROF_MSG, L()), s_cfg.profile_name);
                show_dialog(DLG_DELPROF, ICON_WARNING_L, tr(S_DELPROF_TITLE, L()), msg);
            }
            return;
        case SET_THEME:  s_cfg.theme ^= 1; break;
        case SET_SPLASH: {
            // standard -> own (if made in Studio) -> off
            static const uint8_t order[3] = { SPLASH_ON, SPLASH_CUSTOM, SPLASH_OFF };
            int cur = s_cfg.splash == SPLASH_CUSTOM ? 1 : s_cfg.splash == SPLASH_OFF ? 2 : 0;
            for (int k = 0; k < 3; k++) {
                cur = (cur + 3 + (dir < 0 ? -1 : 1)) % 3;
                if (order[cur] != SPLASH_CUSTOM || splash_custom_available(s_drv)) break;
            }
            s_cfg.splash = order[cur];
            break;
        }
        case SET_LANG:   s_cfg.language = (uint8_t)((s_cfg.language + LANG_COUNT + dir) % LANG_COUNT); break;
        case SET_SOUNDS: s_cfg.sfx_pack = s_cfg.sfx_pack == SFX_PACK_MUTE ? SFX_PACK_HYPER : SFX_PACK_MUTE; break;
        case SET_VOLUME: { int v = s_cfg.volume + dir; if (v < 0) v = 0; if (v > 5) v = 5; s_cfg.volume = (uint8_t)v; break; }
        case SET_BRIGHT: { int v = s_cfg.brightness + dir; if (v < 1) v = 1; if (v > 5) v = 5; s_cfg.brightness = (uint8_t)v; break; }
        case SET_SORT:   s_cfg.sort_mode = (uint8_t)((s_cfg.sort_mode + SORT_COUNT + dir) % SORT_COUNT); break;
        case SET_SLOT:   s_cfg.save_slot = (uint8_t)((s_cfg.save_slot + 3 + dir) % 3); break;
        case SET_HIDDEN: s_cfg.show_hidden ^= 1; break;
        case SET_COVERS: s_cfg.covers ^= 1; apply_covers(); break;
        case SET_ANIM: s_cfg.anim_off ^= 1; break;
        case SET_TOUCH: open_touchcal(); return;
        case SET_GAMEINFO: {
            static const uint8_t order[3] = { GAMEINFO_REGION, GAMEINFO_CODE, GAMEINFO_NONE };
            int cur = (s_cfg.home_show & GAMEINFO_MASK) >> 5;
            cur = (cur + 3 + (dir < 0 ? -1 : 1)) % 3;
            s_cfg.home_show = (uint8_t)((s_cfg.home_show & ~GAMEINFO_MASK) | order[cur]);
            break;
        }
        case SET_PROFILE: open_keyboard(KB_PROFILE, s_cfg.profile_name); return;
        case SET_RESCAN: rescan(); return;
        case SET_OLDMENU: {
            char p[MAX_PATH_LEN];
            if (!old_menu_path(p, sizeof(p))) { show_dialog(DLG_INFO, ICON_WARNING_L, tr(S_SET_OLDMENU, L()), tr(S_OLDMENU_NONE, L())); return; }
            show_dialog(DLG_OLDMENU, ICON_WARNING_L, tr(S_SET_OLDMENU, L()), tr(S_OLDMENU_Q, L()));
            return;
        }
        case SET_ABOUT: return;
        case SET_BETA:
            s_cfg.beta ^= 1;
            settings_save(&s_cfg);
            build_home();
            if (s_home_sel >= s_home_n) s_home_sel = 0;
            sfx(SFX_TOGGLE);
            return;
        case SET_WIFI: open_wifi(); return;
        case SET_WPRESET:
            if (!ui_wallpaper_present()) { toast(tr(S_WP_DEFAULT, L())); sfx(SFX_ERROR); return; }
            show_dialog(DLG_WPRESET, ICON_WARNING_L, tr(S_SET_WPRESET, L()), tr(S_WPRESET_Q, L()));
            s_dlg_sel = 1;          // Cancel first
            return;
        case SET_SAVER: {
            // order shown: off, 1, 3, 5, 10 minutes (stored: 0 = 3 min, 1 = off, 2 = 1, 3 = 5, 4 = 10)
            static const uint8_t order[5] = { 1, 2, 0, 3, 4 };
            int cur = 0;
            for (int k = 0; k < 5; k++) if (order[k] == (s_cfg.photo_opts & PHOTOOPT_SAVER_MASK)) cur = k;
            cur = (cur + 5 + dir) % 5;
            s_cfg.photo_opts = (uint8_t)((s_cfg.photo_opts & ~PHOTOOPT_SAVER_MASK) | order[cur]);
            break;
        }
        case SET_SAVER_SRC: s_cfg.photo_opts ^= PHOTOOPT_FAVS_ONLY; break;
        case SET_FRAME: s_cfg.photo_opts ^= PHOTOOPT_NO_FRAME; break;
        case SET_POD: s_cfg.photo_opts ^= PHOTOOPT_NO_POD; s_pod_day = -1; break;
    }
    apply_settings();
    if (row == SET_SORT || row == SET_HIDDEN) refresh_all();
    sfx(SFX_TOGGLE);
}

static const char *setting_value(int row, char *buf, size_t len) {
    switch (row) {
        case SET_THEME:  return tr(s_cfg.theme ? S_THEME_LIGHT : S_THEME_DARK, L());
        case SET_SPLASH: return tr(s_cfg.splash == SPLASH_CUSTOM ? S_SPLASH_OWN : splash_enabled(s_cfg.splash) ? S_ON : S_OFF, L());
        case SET_LANG:   return lang_name(s_cfg.language);
        case SET_SOUNDS: return s_cfg.sfx_pack == SFX_PACK_MUTE ? tr(S_OFF, L()) : tr(S_SOUNDS_HYPER, L());
        case SET_VOLUME: snprintf(buf, len, "%d / 5", s_cfg.volume); return buf;
        case SET_BRIGHT: snprintf(buf, len, "%d / 5", s_cfg.brightness); return buf;
        case SET_SORT:   return tr(s_cfg.sort_mode == SORT_ALPHA ? S_SORT_ALPHA : (s_cfg.sort_mode == SORT_PLAYTIME ? S_SORT_PLAYTIME : S_SORT_RECENT), L());
        case SET_SLOT:   snprintf(buf, len, "%d", s_cfg.save_slot + 1); return buf;
        case SET_HIDDEN: return tr(s_cfg.show_hidden ? S_ON : S_OFF, L());
        case SET_COVERS: return tr(s_cfg.covers ? S_ON : S_OFF, L());
        case SET_ANIM: return tr(s_cfg.anim_off ? S_OFF : S_ON, L());
        case SET_TOUCH: return tr(touchcal_current() ? S_TC_ON : S_TC_OFF, L());
        case SET_GAMEINFO: {
            int gi = s_cfg.home_show & GAMEINFO_MASK;
            return tr(gi == GAMEINFO_CODE ? S_GI_CODE : gi == GAMEINFO_NONE ? S_GI_NONE : S_GI_REGION, L());
        }
        case SET_PROFILE: return s_cfg.profile_name;
        case SET_COLOR: {
            const ProfileInfo *pi = profiles_get(s_cur_profile);
            return tr((StrId)(S_COLOR_0 + (pi ? pi->color : 0)), L());
        }
        case SET_LOCK:   return tr(profiles_has_pin(s_cur_profile) ? S_ON : S_OFF, L());
        case SET_PAD:    return tr((s_cfg.home_show & HOMESHOW_PAD) ? S_ON_HOME : S_OFF, L());
        case SET_LOGIN:  return tr(profiles_global()->login ? S_ON : S_OFF, L());
        case SET_LIDLOGIN: return tr(s_cfg.lid_login_off ? (s_cfg.lid_sound_off ? S_LID_NONE : S_LID_SOUND)
                                                         : (s_cfg.lid_sound_off ? S_LID_LOGIN : S_LID_BOTH), L());
        case SET_LIDSOUND: return tr(s_cfg.lid_sound_off ? S_OFF : S_ON, L());
        case SET_ABOUT:  return HYPEROS_VERSION;
        case SET_BETA:   return tr(s_cfg.beta ? S_ON : S_OFF, L());
        case SET_SAVER: {
            static const int mins[5] = { 3, 0, 1, 5, 10 };
            int m = mins[s_cfg.photo_opts & PHOTOOPT_SAVER_MASK];
            if (!m) return tr(S_OFF, L());
            snprintf(buf, len, tr(S_MIN_FMT, L()), m);
            return buf;
        }
        case SET_SAVER_SRC: return tr((s_cfg.photo_opts & PHOTOOPT_FAVS_ONLY) ? S_PH_FAVS : S_PH_ALL, L());
        case SET_FRAME: return tr((s_cfg.photo_opts & PHOTOOPT_NO_FRAME) ? S_OFF : S_ON, L());
        case SET_POD:   return tr((s_cfg.photo_opts & PHOTOOPT_NO_POD) ? S_OFF : S_ON, L());
        case SET_WPRESET: return tr(ui_wallpaper_present() ? S_WP_CUSTOM : S_WP_DEFAULT, L());
        case SET_WIFI:   if (!wifi_saved(s_drv, buf, len)) return tr(S_WIFI_DSI, L()); return buf;
        default: return "";
    }
}

static const struct { StrId name, desc; IconId icon; } k_settings[SET_COUNT] = {
    { S_SET_PROFILE, S_DESC_PROFILE, ICON_USER },
    { S_SET_COLOR, S_DESC_COLOR, ICON_PALETTE },
    { S_SET_LOCK, S_DESC_LOCK, ICON_LOCK },
    { S_SET_STATS, S_DESC_STATS, ICON_CHART },
    { S_SET_SWITCH, S_DESC_SWITCH, ICON_USERS },
    { S_SET_HOME, S_DESC_HOME, ICON_HOME },
    { S_SET_PAD, S_DESC_PAD, ICON_GAMEPAD },
    { S_SET_THEME, S_DESC_THEME, ICON_THEME },
    { S_SET_SPLASH, S_DESC_SPLASH, ICON_PLAY },
    { S_SET_LOGIN, S_DESC_LOGIN, ICON_USERS },
    { S_SET_LID, S_DESC_LID, ICON_LOCK },
    { S_SET_LIDSOUND, S_DESC_LIDSOUND, ICON_SPEAKER },
    { S_SET_LANGUAGE, S_DESC_LANGUAGE, ICON_GLOBE },
    { S_SET_SOUNDS, S_DESC_SOUNDS, ICON_SPEAKER },
    { S_SET_VOLUME, S_DESC_VOLUME, ICON_SPEAKER },
    { S_SET_BRIGHTNESS, S_DESC_BRIGHTNESS, ICON_SUN },
    { S_SET_COVERS, S_DESC_COVERS, ICON_GRID },
    { S_SET_ANIM, S_DESC_ANIM, ICON_PLAY },
    { S_SET_GAMEINFO, S_DESC_GAMEINFO, ICON_INFO },
    { S_SET_TOUCH, S_DESC_TOUCH, ICON_GAMEPAD },
    { S_SET_SORT, S_DESC_SORT, ICON_SORT },
    { S_SET_SLOT, S_DESC_SLOT, ICON_SAVE },
    { S_SET_HIDDEN, S_DESC_HIDDEN, ICON_EYE_OFF },
    { S_SET_DELPROF, S_DESC_DELPROF, ICON_TRASH },
    { S_SET_RESCAN, S_DESC_RESCAN, ICON_RESTORE },
    { S_SET_OLDMENU, S_DESC_OLDMENU, ICON_HOME },
    { S_SET_ABOUT, S_DESC_ABOUT, ICON_INFO },
    { S_SET_BETA, S_DESC_BETA, ICON_BOLT },
    { S_SET_WIFI, S_DESC_WIFI, ICON_GLOBE },
    { S_SET_SAVER, S_DESC_SAVER, ICON_PHOTO },
    { S_SET_SAVER_SRC, S_DESC_SAVER_SRC, ICON_HEART },
    { S_SET_FRAME, S_DESC_FRAME, ICON_BOLT },
    { S_SET_POD, S_DESC_POD, ICON_CLOCK },
    { S_SET_WPRESET, S_DESC_WPRESET, ICON_PHOTO },
};

// ---------------------------------------------------------------------------
// Boot / scanning progress
// ---------------------------------------------------------------------------
static int s_scan_found;

// First start / rescan progress: the resting frame of the "Hinge" animation on top
static void draw_splash(const char *line) {
    Canvas *b = &g_bot;
    splash_draw(splash_frames() - splash_fade_frames() - 1);
    gfx_clear(b, T.bg);
    ui_spinner(b, 128, 82, 12, s_frame, T.accent);
    if (line) gfx_text_align(b, FONT_BODY, 0, 106, 256, line, T.text2, ALIGN_CENTER);
}

static volatile bool s_spl_init_done = true;   // false while the boot work runs next to the animation
static volatile bool s_spl_stop = false;

// Plays the boot animation. With wait_init the last frame before the fade-out is held
// until s_spl_init_done (the boot work running on the main thread) is set.
// The preview (not the boot) can be skipped with any button or a tap. Leaves the screens black for HOME to fade in.
static void splash_run(bool wait_init) {
    int n = splash_frames(), fo = splash_fade_frames();
    int hold_at = n - fo - 1;
    bool still = splash_is_custom();               // custom: a picture, only the brightness moves
    if (!still) sfx(SFX_BOOT);
    else if (s_cfg.sfx_pack == SFX_PACK_MUTE || !sound_play_boot()) { if (s_cfg.sfx_pack != SFX_PACK_MUTE) sfx(SFX_STARTUP); }
#ifdef ARM9
    scanKeys();
    const u64 tpf = ticksFromUsec(16715);          // ticks per 59.83 Hz frame
    u64 t0 = tickGetCount();
#endif
    bool drawn_hold = false;
    for (int f = 0; f < n && !s_spl_stop;) {
        bool hold = wait_init && !s_spl_init_done && f >= hold_at;
        if (hold) f = hold_at;
        bool draw = (!hold || !drawn_hold) && (!still || f == 0);   // a held frame is drawn once, then left alone
        if (draw) splash_draw(f);
        if (hold) drawn_hold = true;
        int lvl = 0;
        if (fo && f >= n - fo) lvl = 16 * (f - (n - fo) + 1) / fo;
        else if (still) lvl = splash_fade_in(f);
#ifdef ARM9
        threadWaitForVBlank();
        int fl = splash_flash(f);
        if (fl > 0 && lvl == 0) { REG_MASTER_BRIGHT = (u16)((1 << 14) | fl); REG_MASTER_BRIGHT_SUB = (u16)((1 << 14) | fl); }
        else hw_master(lvl);
        if (draw) gfx_present(true, true);
        scanKeys();
        // the boot animation always plays to the end (skipping it only left a black screen while
        // the boot work finished); the preview in Settings can be skipped
        if (!wait_init && (keysDown() & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R | KEY_START | KEY_SELECT |
                          KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_TOUCH))) break;
        // advance by real time, so a slow frame never stretches the animation
        u64 now = tickGetCount();
        if (hold) {
            t0 = now - (u64)(f + 1) * tpf;          // continue right after the held frame
        } else {
            int nf = tpf ? (int)((now - t0) / tpf) : f + 1;
            f = nf > f ? nf : f + 1;
        }
#else
        (void)lvl;
        if (draw) gfx_present(true, true);
        if (!hold) f++;
#endif
    }
#ifdef ARM9
    hw_master(16);
#endif
}

// Settings preview (runs on the main thread)
static void play_splash(void) {
    sound_prepare(SFX_BOOT, s_cfg.sfx_pack);
    if (s_cfg.splash == SPLASH_CUSTOM && splash_custom_load(s_drv)) {
        char p[MAX_PATH_LEN];
        snprintf(p, sizeof(p), "%s/_hyper/boot/boot.wav", s_drv);
        sound_load_boot(p);
    } else {
        splash_custom_off();
    }
    s_spl_init_done = true;
    splash_run(false);
    s_fade = 16;          // HOME fades in from black
    s_force_redraw = true;
}

// Boot: the animation runs on its own (higher priority) thread, woken at every VBlank,
// while the main thread keeps loading. The two meet again in splash_boot_finish().
static bool s_spl_thread_on = false;
#ifdef ARM9
static Thread s_spl_thread;
static u8 s_spl_stack[16 * 1024] __attribute__((aligned(8)));
static int splash_thread_fn(void *arg) {
    (void)arg;
    splash_run(true);
    return 0;
}
#endif

static void splash_boot_start(void) {
    sound_prepare(SFX_BOOT, s_cfg.sfx_pack);
    s_spl_init_done = false;
    s_spl_stop = false;
#ifdef ARM9
    threadPrepare(&s_spl_thread, splash_thread_fn, NULL, &s_spl_stack[sizeof(s_spl_stack)], MAIN_THREAD_PRIO - 2);
    threadAttachLocalStorage(&s_spl_thread, NULL);
    threadStart(&s_spl_thread);
    s_spl_thread_on = true;
#else
    splash_run(false);
    s_spl_init_done = true;
#endif
}

// abort: stop the animation now (e.g. the first-start progress screen needs the display)
static void splash_boot_finish(bool abort) {
    if (abort) s_spl_stop = true;
    s_spl_init_done = true;
#ifdef ARM9
    if (s_spl_thread_on) threadJoin(&s_spl_thread);
#endif
    s_spl_thread_on = false;
    s_spl_stop = false;
}

static void scan_progress(int found, const char *name) {
    (void)name;
    s_scan_found = found;
    static int last = -1;
    s_frame++;
    if ((s_frame & 3) && found == last) return;
    last = found;
    char line[64];
    snprintf(line, sizeof(line), "%s  %d", tr(S_SCANNING, L()), found);
    draw_splash(line);
#ifdef ARM9
    hw_master(0);
#endif
    gfx_present(true, true);
}

static void rescan(void) {
    s_scan_found = 0;
    library_sync(s_drv, true, scan_progress);
    apply_covers();
    refresh_all();
    char msg[64];
    snprintf(msg, sizeof(msg), tr(S_RESCAN_DONE, L()), library_count());
    toast(msg);
    sfx(SFX_DECIDE);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
static bool s_gfx_ready;

void app_boot_logo(void) {
    settings_defaults(&s_cfg);
    theme_apply(0);
    gfx_init();
    s_gfx_ready = true;
    // keep the screens black until the chosen boot animation starts
    gfx_clear(&g_top, (Color)0x8000);
    gfx_clear(&g_bot, (Color)0x8000);
    gfx_present(true, true);
}

// ---------------------------------------------------------------------------
// Lid: a short sound, the screens fade out, and (login screen on / locked profile)
// the console wakes up on the login screen
// ---------------------------------------------------------------------------
// DSplay puts the console to sleep itself (sleep on hinge close is turned off in main.c),
// so there is time for the sound and the switch to the login screen first.
static void lid_closed(void) {
    ss_stop();
#ifdef ARM9
    if (s_scr == SCR_PAD) close_pad();     // Wi-Fi goes off while asleep
    if (s_scr == SCR_STORE) close_store();
    if (s_scr == SCR_NEARBY) close_nearby();
    if (!s_cfg.lid_sound_off) sfx(SFX_LID_CLOSE);
    for (int i = 1; i <= 8; i++) { swiWaitForVBlank(); hw_master(i * 2); }   // ~0.15 s: the sound plays out
    if (s_cur_profile >= 0) settings_save(&s_cfg);
    bool locked = profiles_has_pin(s_cur_profile);
    bool to_login = (profiles_global()->login && !s_cfg.lid_login_off) || locked;
    if (s_cur_profile >= 0 && to_login && s_scr != SCR_LOGIN) open_login(s_cur_profile, locked && !profiles_global()->login);
    pmEnterSleep();
    // woken up (lid opened)
    if (!s_cfg.lid_sound_off) sfx(SFX_LID_OPEN);
    s_idle = 0;
    s_fade = 16;
    s_fade_slow = true;              // the screen fades in gently
    s_force_redraw = true;
    scanKeys();
#endif
}

bool app_init(bool fat_ok, bool quick_resume) {
    snprintf(s_drv, sizeof(s_drv), "%s", get_drive_prefix());
    stats_set_drive(s_drv);
    settings_defaults(&s_cfg);
    theme_apply(0);
    if (!s_gfx_ready) gfx_init();
    sound_init();
    if (!fat_ok) {
        s_scr = SCR_FATAL;
        return false;
    }
    char p[MAX_PATH_LEN];
    snprintf(p, sizeof(p), "%s/_hyper", s_drv);
    make_dirs(p);
    snprintf(p, sizeof(p), "%s/_hyper/covers", s_drv);
    make_dirs(p);
    launcher_dsiware_resume();      // a DSiWare save swapped in for the last game goes back to its profile
    {
        char om[MAX_PATH_LEN];
        s_has_oldmenu = old_menu_path(om, sizeof(om));
    }
    // profiles: the login screen uses the look of the profile that was used last
    profiles_init(s_drv);
    ProfilesGlobal *g = profiles_global();
    int last = g->last;
    // Back from a game (DSplay's ARM7 notes the start in the MCU, cleared when the console is
    // switched on): no boot animation, no login screen - straight to that profile's HOME.
    // The stock ARM7 cannot tell: the "resume" flag alone decides, as before.
    int kind = launcher_boot_kind();
    bool resume = g->resume != 0 && kind != 0;
    s_back_from_game = resume;
    profiles_activate(last);
    profile_paths_for(last);
    settings_load(&s_cfg);
    {
        const ProfileInfo *pi = profiles_get(last);
        if (pi) snprintf(s_cfg.profile_name, sizeof(s_cfg.profile_name), "%s", pi->name);
    }
    apply_theme();
    sound_set_volume(s_cfg.volume);
    snprintf(p, sizeof(p), "%s/_hyper/sfx", s_drv);
    sound_load_pack(p);
    s_photos_exist = photos_exist(s_drv);
    // start the boot animation now; everything below loads while it plays
    bool animated = !quick_resume && !(resume && kind == 1) && splash_enabled(s_cfg.splash);   // stock ARM7: as before
    splash_custom_off();
    if (animated && s_cfg.splash == SPLASH_CUSTOM) {
        // the user's boot screen: two pictures and a sound (~0.05 s to read), else the built-in one
        if (splash_custom_load(s_drv)) {
            snprintf(p, sizeof(p), "%s/_hyper/boot/boot.wav", s_drv);
            sound_load_boot(p);
        }
    }
    if (animated) splash_boot_start();
    for (int i = 0; i < profiles_count(); i++) {
        int slot = profiles_slot(i);
        profiles_file(slot, "avatar.bin", p, sizeof(p));
        ui_avatar_load_slot(slot, p);
    }
    s_view = malloc(sizeof(int) * MAX_LIBRARY_ITEMS);
    library_init();

    apps_load();
    library_load_cache(s_drv);
    if (library_count() == 0) {
        // first start: nothing cached yet, read the card with a progress screen
        splash_boot_finish(true);
        draw_splash(tr(S_SCANNING, L()));
#ifdef ARM9
        hw_master(0);
#endif
        gfx_present(true, true);
        library_sync(s_drv, false, scan_progress);
    } else {
        // menu appears at once from the cache; the card is checked in the background
        library_scan_begin(s_drv, false, NULL);
    }
    slot1_init(strcmp(s_drv, "sd:") == 0);   // the card is read a moment after the menu appears

    bool locked = profiles_has_pin(last);
    if (resume || (quick_resume && !locked) || (!g->login && !locked)) {
        // back from a game / START held / no login screen: open the last profile directly
        profile_login(last);
        if (quick_resume && !resume) {
            // Hold START at boot: resume the most recently played title
            int best = -1;
            uint32_t ts = 0;
            for (int i = 0; i < library_count(); i++) {
                const PlaytimeEntry *e = playtime_get(&s_pt, library_get(i)->filename);
                if (e && e->last_played_ts > ts) { ts = e->last_played_ts; best = i; }
            }
            if (best >= 0) do_launch(best);
        }
    } else {
        s_cur_profile = -1;
        apply_covers();
        refresh_all();
        open_login(last, !g->login);     // login screen off but the profile is locked: ask for its code
    }
    splash_boot_finish(false);           // waits for the animation to end
    if (!animated) {
        if (!resume) sfx(SFX_STARTUP);   // (back from a game: quietly)
        s_fade_slow = !resume;           // no animation: the menu fades in gently from black
    }
    s_fade = 16;
    s_force_redraw = true;
    return true;
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------
static int home_tile_x(int i) { return TILE_X0 + i * (TILE + TILE_GAP) - s_home_scroll; }

// Animated icon (DSi titles) of the game on the upper screen: loaded when that game is shown
static bool home_clock_on(void);
static NdsAnim s_anim;
static char    s_anim_path[MAX_PATH_LEN];
static bool    s_anim_ok;
static int     s_anim_t0;

static bool anim_for(const GameItem *it) {
    if (s_cfg.anim_off || !it || it->type != TYPE_NDS || !(it->flags & ITEM_FLAG_DSI)) { s_anim_path[0] = '\0'; s_anim_ok = false; return false; }
    if (strcmp(s_anim_path, it->path) != 0) {
        snprintf(s_anim_path, sizeof(s_anim_path), "%s", it->path);
        s_anim_ok = romdb_load_anim(it->path, &s_anim);
        s_anim_t0 = s_frame;
    }
    return s_anim_ok;
}

// The current frame of the animated icon of it (NULL: not animated), for the selected tile too
static const GameItem *anim_frame(const GameItem *it) {
    if (!anim_for(it)) return NULL;
    static GameItem frame;
    frame = *it;
    romdb_anim_icon(&s_anim, romdb_anim_step(&s_anim, (uint32_t)(s_frame - s_anim_t0)), frame.icon_data, frame.icon_palette);
    return &frame;
}

// Box art of an item for the upper screen (the game card's comes from the app)
static const Color *cover_of(int lib_idx, const GameItem *it) {
    if (!s_cfg.covers && lib_idx != HOME_CARD) return NULL;
    if (lib_idx >= 0) return covers_get(lib_idx, it);
    if (it && it->type == TYPE_SLOT1) return ui_card_cover();
    return NULL;
}

static int anim_sig(void) {
    bool shown = (s_scr == SCR_HOME && s_home_focus == 0 && !home_clock_on()) || s_scr == SCR_ALL;
    return shown && s_anim_ok && !s_cfg.anim_off && s_ovl == OVL_NONE ? romdb_anim_step(&s_anim, (uint32_t)(s_frame - s_anim_t0)) + 1 : 0;
}

// last letter of a DS game code -> its region (NULL: not known)
static const char *game_region(const char *code) {
    if (!code || strlen(code) < 4 || !strcmp(code, "####")) return NULL;
    switch (code[3]) {
        case 'E': case 'T': return tr(S_REG_US, L());
        case 'P': case 'X': case 'Y': case 'Z': case 'V': case 'L': return tr(S_REG_EU, L());
        case 'J': return tr(S_REG_JP, L());
        case 'K': return tr(S_REG_KR, L());
        case 'U': return tr(S_REG_AU, L());
        case 'D': return tr(S_REG_DE, L());
        case 'F': return tr(S_REG_FR, L());
        case 'S': return tr(S_REG_ES, L());
        case 'I': return tr(S_REG_IT, L());
        case 'H': return tr(S_REG_NL, L());
        case 'C': return tr(S_REG_CN, L());
        case 'O': case 'A': return tr(S_REG_WORLD, L());
        default: return NULL;
    }
}

static void hero(Canvas *c, int lib_idx, const GameItem *it) {
    Color tint = T.bg_top;
    if (it && (it->type == TYPE_NDS || it->type == TYPE_APP)) tint = gfx_nds_icon_tint(it->icon_data, it->icon_palette);
    else if (it) tint = ui_system_color((ItemType)it->type, false);
    if (ui_wallpaper_present()) {
        ui_wallpaper_draw(c);
    } else {
        Color top = gfx_mix(T.bg_top, tint, T.light ? 50 : 70);
        gfx_vgradient(c, 0, 0, 256, 120, top, T.bg_top);
        gfx_rect(c, 0, 120, 256, 72, T.bg_top);
    }
    ui_status_bar(c, &s_cfg);
    if (!it) return;

    const GameItem *frame = lib_idx >= 0 ? anim_frame(it) : NULL;
    if (!ui_cover(c, cover_of(lib_idx, it), 14, 32, 96, 6)) {   // box art, else the (animated) icon
        gfx_shadow(c, 14, 32, 96, 96, 8, 4, 24);
        if (frame) ui_tile_uncached(c, frame, 14, 32, 96, 8);
        else ui_tile(c, lib_idx, it, 14, 32, 96, 8);
    }

    int x = 122, w = 124;
    const char *sys = romdb_system_name((ItemType)it->type);
    if (lib_idx == HOME_CARD) sys = tr(S_GAME_CARD, L());
    gfx_text(c, FONT_SMALL, x, 32, sys, T.text2, w);
    int lines = gfx_text_wrap(c, FONT_TITLE, x, 45, w, 3, 18, it->title, T.text);
    int y = 45 + lines * 18 + 1;
    if (it->subtitle[0]) {
        gfx_text(c, FONT_SMALL, x, y, it->subtitle, T.text2, w);
        y += 14;
    }
    // play statistics
    y = y < 104 ? 104 : y + 4;
    const PlaytimeEntry *e = playtime_get(&s_pt, it->filename);
    char line[96];
    if (e && e->total_minutes) {
        if (e->total_minutes >= 60) snprintf(line, sizeof(line), tr(S_PLAYED_HM, L()), (unsigned)(e->total_minutes / 60), (unsigned)(e->total_minutes % 60));
        else snprintf(line, sizeof(line), tr(S_PLAYED_M, L()), (unsigned)e->total_minutes);
    } else {
        snprintf(line, sizeof(line), "%s", tr((e && e->last_played_ts) ? S_PLAYED_LT1 : S_NEVER_PLAYED, L()));
    }
    gfx_icon(c, ICON_CLOCK, x, y + 1, T.text2);
    gfx_text(c, FONT_SMALL, x + 16, y, line, T.text2, w - 16);
    y += 14;
    if (e && e->last_played_ts) {
        char d[24], l2[64];
        ui_format_date(e->last_played_ts, d, sizeof(d));
        snprintf(l2, sizeof(l2), tr(S_LAST_PLAYED, L()), d);
        gfx_text(c, FONT_SMALL, x + 16, y, l2, T.text3, w - 16);
        y += 14;
    }
    if (it->flags & ITEM_FLAG_FAVORITE) {
        gfx_icon(c, ICON_HEART, x, y + 1, C8(255, 90, 120));
        gfx_text(c, FONT_SMALL, x + 16, y, tr(S_FAVORITES, L()), T.text2, w - 16);
        y += 14;
    }
    if (e && e->total_minutes >= 60) {
        // play-time badge: 1 h bronze, 10 h silver, 50 h gold, 100 h diamond
        static const struct { uint32_t min; StrId name; uint8_t r, g, b; } tiers[4] = {
            { 6000, S_BADGE_4, 90, 210, 240 }, { 3000, S_BADGE_3, 240, 190, 40 },
            { 600, S_BADGE_2, 190, 198, 210 }, { 60, S_BADGE_1, 205, 127, 50 } };
        for (int i = 0; i < 4; i++) {
            if (e->total_minutes < tiers[i].min) continue;
            gfx_icon(c, ICON_MEDAL, x - 1, y, C8(tiers[i].r, tiers[i].g, tiers[i].b));
            gfx_text(c, FONT_SMALL, x + 16, y, tr(tiers[i].name, L()), T.text2, w - 16);
            break;
        }
    }

    // footer: file details
    char foot[128];
    unsigned kb = it->file_size / 1024;
    char size[24];
    if (kb >= 1024) snprintf(size, sizeof(size), "%u.%u MB", kb / 1024, (kb % 1024) * 10 / 1024);
    else snprintf(size, sizeof(size), "%u KB", kb);
    // the game's code, its region or nothing (Settings > Game code)
    char code[48] = "";
    int gi = s_cfg.home_show & GAMEINFO_MASK;
    if (gi == GAMEINFO_CODE) snprintf(code, sizeof(code), "%s", it->game_code);
    else if (gi == GAMEINFO_REGION) { const char *r = game_region(it->game_code); if (r) snprintf(code, sizeof(code), "%s", r); }
    const char *sep = code[0] ? "  ·  " : "";
    if (lib_idx == HOME_CARD) snprintf(foot, sizeof(foot), "%s", code);
    else if (it->type == TYPE_APP) snprintf(foot, sizeof(foot), "%s%s%s", tr(S_APP_KIND, L()), sep, code);
    else if (it->type == TYPE_NDS && (it->flags & ITEM_FLAG_HOMEBREW)) snprintf(foot, sizeof(foot), "Homebrew  ·  %s  ·  %s", size, it->filename);
    else if (it->type == TYPE_NDS) snprintf(foot, sizeof(foot), "%s%s%s  ·  %s %d", code, sep, size,
                                             tr(S_OPT_SAVE_SLOT, L()), s_cfg.save_slot + 1);
    else snprintf(foot, sizeof(foot), "%s  ·  %s", size, it->filename);
    gfx_rect(c, 14, 166, 228, 1, T.divider);
    gfx_text(c, FONT_SMALL, 14, 172, foot, T.text3, 228);
}

static void hero_simple(Canvas *c, IconId icon, Color icon_col, const char *title, const char *line1, const char *line2) {
    if (ui_wallpaper_present()) {
        ui_wallpaper_draw(c);
    } else {
        gfx_vgradient(c, 0, 0, 256, 120, gfx_mix(T.bg_top, icon_col, T.light ? 40 : 50), T.bg_top);
        gfx_rect(c, 0, 120, 256, 72, T.bg_top);
    }
    ui_status_bar(c, &s_cfg);
    gfx_shadow(c, 14, 32, 96, 96, 8, 4, 20);
    gfx_round_rect(c, 14, 32, 96, 96, 8, T.tile, 32);
    gfx_icon_centered(c, icon, 62, 80, icon_col);
    int lines = gfx_text_wrap(c, FONT_TITLE, 122, 45, 124, 2, 18, title, T.text);
    int y = 45 + lines * 18 + 4;
    if (line1) { y += 18 * (gfx_text_wrap(c, FONT_BODY, 122, y, 124, 4, 15, line1, T.text2) - 1) + 18; }
    if (line2) gfx_text_wrap(c, FONT_SMALL, 122, y, 124, 3, 13, line2, T.text3);
}

static const IconId k_home_btn_icon[HB_COUNT] = { ICON_CART, ICON_GRID, ICON_SEARCH, ICON_BAG, ICON_GAMEPAD, ICON_USERS, ICON_PHOTO, ICON_GEAR, ICON_POWER };

static Color home_btn_color(int b) {
    switch (b) {
        case HB_CARD: return slot1_present() ? C8(255, 160, 40) : T.text3;
        case HB_ALL: return C8(0, 190, 230);
        case HB_SEARCH: return C8(60, 200, 110);
        case HB_STORE: return C8(255, 120, 60);
        case HB_PAD: return C8(140, 90, 230);
        case HB_NEARBY: return C8(90, 120, 255);
        case HB_PHOTOS: return C8(40, 190, 140);
        default: return T.btn_icon;
    }
}

static const char *home_btn_label(int b) {
    switch (b) {
        case HB_CARD: return tr(S_GAME_CARD, L());
        case HB_ALL: return tr(S_ALL_SOFTWARE, L());
        case HB_SEARCH: return tr(S_SEARCH, L());
        case HB_STORE: return tr(S_STORE, L());
        case HB_SETTINGS: return tr(S_SETTINGS, L());
        case HB_PAD: return tr(S_PAD_HOME, L());
        case HB_NEARBY: return tr(S_NEARBY, L());
        case HB_PHOTOS: return tr(S_PHOTOS, L());
        default: return tr(S_POWER, L());
    }
}

// Home buttons the user kept (Settings always stays; the power button is off by default because
// the console's POWER button opens the same menu - except with the stock ARM7, where it cannot)
static bool hb_visible(int b) {
    static const uint8_t bit[HB_COUNT] = { HOMEHIDE_BTN_CARD, HOMEHIDE_BTN_ALL, HOMEHIDE_BTN_SEARCH, 0, 0, 0, 0, 0, 0 };
    switch (b) {
        case HB_STORE: return s_cfg.beta && !(s_cfg.home_show & HOMEOPT_NO_STORE);   // beta
        case HB_PAD: return (s_cfg.home_btns & HOMEBTN_PAD) != 0;
        case HB_NEARBY: return s_cfg.beta && (s_cfg.home_btns & HOMEBTN_NEARBY) != 0;   // beta
        case HB_PHOTOS: return (s_cfg.home_btns & HOMEBTN_PHOTOS) != 0;
        case HB_SETTINGS: return true;
        case HB_POWER: return (s_cfg.home_btns & HOMEBTN_POWER) || !power_off_supported();
        default: return b >= 0 && b < HB_COUNT && !(s_cfg.home_hide & bit[b]);
    }
}

static int btn_cx(int b) {
    int n = 0, rank = 0;
    for (int i = 0; i < HB_COUNT; i++) {
        if (!hb_visible(i)) continue;
        if (i < b) rank++;
        n++;
    }
    int step = n > 1 && (n - 1) * 35 > 222 ? 222 / (n - 1) : 35;   // many buttons: closer together
    return 128 - (n - 1) * step / 2 + rank * step;
}

// buttons get a little smaller when there are many of them (up to nine)
static int hb_r(void) {
    int n = 0;
    for (int i = 0; i < HB_COUNT; i++) n += hb_visible(i);
    return n >= 9 ? 12 : n >= 8 ? 13 : HB_R;
}

static int hb_step(int b, int dir) {
    for (int i = b + dir; i >= 0 && i < HB_COUNT; i += dir) if (hb_visible(i)) return i;
    return b;
}

// ---------------------------------------------------------------------------
// Screens: TOP
// ---------------------------------------------------------------------------
// Big clock on the upper screen after 30 s without input on HOME (Settings > Home screen)
static bool home_clock_on(void) {
    return s_scr == SCR_HOME && s_ovl == OVL_NONE && !(s_cfg.home_show & HOMEOPT_NO_CLOCK) && s_idle >= 60 * 30;
}

// one 7-segment digit, w x h, segment thickness t
static void seg_digit(Canvas *c, int x, int y, int w, int h, int t, int d, Color col) {
    static const uint8_t segs[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    uint8_t m = segs[d % 10];
    int hh = h / 2, r = t / 2;
    if (m & 0x01) gfx_round_rect(c, x + t / 2, y, w - t, t, r, col, 32);                 // a
    if (m & 0x02) gfx_round_rect(c, x + w - t, y + t / 2, t, hh - t / 2, r, col, 32);     // b
    if (m & 0x04) gfx_round_rect(c, x + w - t, y + hh, t, hh - t / 2, r, col, 32);        // c
    if (m & 0x08) gfx_round_rect(c, x + t / 2, y + h - t, w - t, t, r, col, 32);          // d
    if (m & 0x10) gfx_round_rect(c, x, y + hh, t, hh - t / 2, r, col, 32);                // e
    if (m & 0x20) gfx_round_rect(c, x, y + t / 2, t, hh - t / 2, r, col, 32);             // f
    if (m & 0x40) gfx_round_rect(c, x + t / 2, y + hh - t / 2, w - t, t, r, col, 32);     // g
}

static void render_top_clock(void) {
    Canvas *c = &g_top;
    if (pod_ready()) {                       // photo of the day
        memcpy(c->px, s_pod_px, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
        gfx_rect_a(c, 0, 0, 256, 192, (Color)0x8000, 13);
    } else if (ui_wallpaper_present()) {
        ui_wallpaper_draw(c);
        gfx_rect_a(c, 0, 0, 256, 192, (Color)0x8000, 14);
    } else {
        gfx_vgradient(c, 0, 0, 256, 192, gfx_mix(T.bg_top, T.accent, T.light ? 40 : 60), T.bg_top);
    }
    ui_status_bar(c, &s_cfg);
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (!tmv) return;
    const int w = 36, h = 66, t = 8, gap = 10;
    int x0 = 128 - (4 * w + 3 * gap + 14) / 2, y0 = 46;
    Color col = T.text;
    seg_digit(c, x0, y0, w, h, t, tmv->tm_hour / 10, col);
    seg_digit(c, x0 + w + gap, y0, w, h, t, tmv->tm_hour % 10, col);
    int cx = x0 + 2 * w + gap + 7 + gap / 2;
    gfx_circle(c, cx, y0 + 20, 4, T.accent, 32);
    gfx_circle(c, cx, y0 + h - 20, 4, T.accent, 32);
    seg_digit(c, x0 + 2 * w + 2 * gap + 14, y0, w, h, t, tmv->tm_min / 10, col);
    seg_digit(c, x0 + 3 * w + 3 * gap + 14, y0, w, h, t, tmv->tm_min % 10, col);
    // weekday + date
    char wd[64], line[64];
    snprintf(wd, sizeof(wd), "%s", tr(S_WEEKDAYS, L()));
    const char *name = wd;
    char *p = wd;
    for (int i = 0; i <= tmv->tm_wday && p; i++) {
        name = p;
        char *sp = strchr(p, ' ');
        if (sp) { *sp = '\0'; p = sp + 1; } else p = NULL;
    }
    snprintf(line, sizeof(line), "%s  ·  %02d.%02d.%04d", name, tmv->tm_mday, tmv->tm_mon + 1, tmv->tm_year + 1900);
    gfx_text_align(c, FONT_TITLE, 0, 128, 256, line, T.text2, ALIGN_CENTER);
}

static void render_top_home(void) {
    Canvas *c = &g_top;
    if (s_home_focus == 1) {
        int b = s_home_btn;
        char line[96] = "";
        const char *l2 = NULL;
        if (b == HB_CARD) {
            const GameItem *ci = slot1_item();
            if (ci) { hero(c, HOME_CARD, ci); return; }
            snprintf(line, sizeof(line), "%s", tr(S_NO_CARD, L()));
        } else if (b == HB_ALL) {
            snprintf(line, sizeof(line), tr(S_SOFTWARE_COUNT, L()), library_count());
        } else if (b == HB_STORE) {
            snprintf(line, sizeof(line), "%s", tr(S_STORE_DESC, L()));
            l2 = tr(S_BETA, L());
        } else if (b == HB_SETTINGS) {
            snprintf(line, sizeof(line), "DSplay %s", HYPEROS_VERSION);
        } else if (b == HB_POWER) {
            snprintf(line, sizeof(line), "%s", tr(power_off_supported() ? S_POWER_DESC : S_POWER_DESC2, L()));
        } else if (b == HB_PAD) {
            snprintf(line, sizeof(line), "%s", tr(S_PAD_STUDIO_HINT, L()));
        } else if (b == HB_PHOTOS) {
            snprintf(line, sizeof(line), "%s", tr(S_PHOTOS_DESC, L()));
        }
        IconId big = (b == HB_CARD) ? ICON_CART_L : (b == HB_ALL) ? ICON_GRID_L : (b == HB_SEARCH) ? ICON_SEARCH_L :
                     (b == HB_STORE) ? ICON_BAG_L : (b == HB_SETTINGS) ? ICON_GEAR_L : (b == HB_PAD) ? ICON_GAMEPAD_L :
                     (b == HB_PHOTOS) ? ICON_PHOTO_L : (b == HB_NEARBY) ? ICON_USERS_L : ICON_POWER_L;
        if (b == HB_NEARBY) snprintf(line, sizeof(line), "%s", tr(S_NEARBY_DESC, L()));
        hero_simple(c, big, home_btn_color(b), home_btn_label(b), line[0] ? line : NULL, l2);
        return;
    }
    int id = s_home[s_home_sel];
    if (id == HOME_PAD) {
        hero_simple(c, ICON_GAMEPAD_L, C8(140, 90, 230), tr(S_PAD_HOME, L()), tr(S_PAD_STUDIO_HINT, L()), NULL);
        return;
    }
    if (id == HOME_PHOTOS) {
        hero_simple(c, ICON_PHOTO_L, C8(40, 190, 140), tr(S_PHOTOS, L()), tr(S_PHOTOS_DESC, L()), NULL);
        return;
    }
    if (id == HOME_STORE) {
        hero_simple(c, ICON_BAG_L, C8(255, 120, 60), tr(S_STORE, L()), tr(S_STORE_DESC, L()), NULL);
        return;
    }
    if (id == HOME_NEARBY) {
        hero_simple(c, ICON_USERS_L, C8(90, 120, 250), tr(S_NEARBY, L()), tr(S_NEARBY_DESC, L()), NULL);
        return;
    }
    if (id == HOME_ALLSW) {
        char line[64];
        snprintf(line, sizeof(line), tr(S_SOFTWARE_COUNT, L()), library_count());
        if (library_count() == 0) hero_simple(c, ICON_GAMEPAD_L, T.accent, tr(S_NO_GAMES, L()), tr(S_NO_GAMES_HINT, L()), NULL);
        else hero_simple(c, ICON_GRID_L, C8(0, 190, 230), tr(S_ALL_SOFTWARE, L()), line, NULL);
        return;
    }
    hero(c, id, item_for(id));
}

static void render_top_settings(void) {
    Canvas *c = &g_top;
    gfx_vgradient(c, 0, 0, 256, 120, gfx_mix(T.bg_top, T.accent, 40), T.bg_top);
    gfx_rect(c, 0, 120, 256, 72, T.bg_top);
    ui_status_bar(c, &s_cfg);
    int r = s_set_sel;
    gfx_circle(c, 40, 56, 22, T.tile, 32);
    gfx_icon_centered(c, k_settings[r].icon, 40, 56, T.accent);
    {
        char hd[64];
        snprintf(hd, sizeof(hd), "%s  ·  %s", tr(S_SETTINGS, L()), tr(k_set_tab_name[s_set_tab], L()));
        gfx_text(c, FONT_SMALL, 72, 38, hd, T.text2, 170);
    }
    gfx_text_wrap(c, FONT_TITLE, 72, 52, 172, 2, 18, tr(k_settings[r].name, L()), T.text);
    if (r == SET_ABOUT) {
        char l[96];
        int y = 92;
        snprintf(l, sizeof(l), "DSplay %s  ·  %s", HYPEROS_VERSION, tr(S_ABOUT_TEXT, L()));
        gfx_text(c, FONT_SMALL, 20, y, l, T.text2, 220); y += 15;
        snprintf(l, sizeof(l), "%s: 3g2h1n", tr(S_MADE_BY, L()));
        gfx_text(c, FONT_BOLD, 20, y, l, T.accent, 220); y += 17;
        snprintf(l, sizeof(l), tr(S_SOFTWARE_COUNT, L()), library_count());
        gfx_text(c, FONT_SMALL, 20, y, l, T.text2, 220); y += 15;
        bool bios = gba_bios_present();
        snprintf(l, sizeof(l), "%s: %s", tr(S_GBA_BIOS, L()), tr(bios ? S_FOUND : S_MISSING, L()));
        gfx_icon(c, bios ? ICON_CHECK : ICON_WARNING, 20, y, bios ? T.ok : T.danger);
        gfx_text(c, FONT_SMALL, 38, y + 1, l, T.text2, 200); y += 17;
        char p[MAX_PATH_LEN];
        snprintf(p, sizeof(p), "%s/_nds/nds-bootstrap-release.nds", s_drv);
        bool nb = file_exists(p);
        snprintf(l, sizeof(l), "nds-bootstrap: %s", tr(nb ? S_FOUND : S_MISSING, L()));
        gfx_icon(c, nb ? ICON_CHECK : ICON_WARNING, 20, y, nb ? T.ok : T.danger);
        gfx_text(c, FONT_SMALL, 38, y + 1, l, T.text2, 200); y += 17;
        snprintf(l, sizeof(l), "Inter typeface  ·  SIL OFL 1.1");
        if (y + 4 < 178) gfx_text(c, FONT_SMALL, 20, y + 4, l, T.text3, 220);
        return;
    }
    gfx_text_wrap(c, FONT_BODY, 20, 94, 216, 4, 15, tr(k_settings[r].desc, L()), T.text2);
    char buf[32];
    const char *v = setting_value(r, buf, sizeof(buf));
    if (v && v[0]) ui_pill(c, 20, 160, FONT_BOLD, v, T.accent, C8(255, 255, 255));
}

static void render_top_keyboard(void) {
    Canvas *c = &g_top;
    gfx_rect(c, 0, 0, 256, 192, T.bg_top);
    ui_status_bar(c, &s_cfg);
    if (s_kb_mode == KB_WIFIPASS) {
        char t[80];
        snprintf(t, sizeof(t), tr(S_WIFI_PASS, L()), s_wf_ssid);
        gfx_icon_centered(c, ICON_GLOBE, 128, 60, T.accent);
        gfx_text_align(c, FONT_TITLE, 0, 80, 256, t, T.text, ALIGN_CENTER);
        gfx_text_wrap(c, FONT_BODY, 20, 112, 216, 4, 15, s_kb, T.text2);
        return;
    }
    if (s_kb_mode != KB_SEARCH) {
        bool prof = s_kb_mode == KB_PROFILE || s_kb_mode == KB_NEWPROFILE;
        bool fold = s_kb_mode == KB_FOLDER || s_kb_mode == KB_FOLDER_RENAME;
        const char *title = tr(fold ? S_FOLDER_NAME : s_kb_mode == KB_NEWPROFILE ? S_NEW_PROFILE_NAME : prof ? S_PROFILE_TITLE :
                               s_kb_mode == KB_GREETING ? S_NEARBY_GREET_TITLE : S_NOTES_TITLE, L());
        gfx_icon_centered(c, fold ? ICON_FOLDER : prof ? ICON_USER : ICON_NOTE, 128, 60, T.accent);
        if (fold) folder_tile(c, 128 - 26, 112, 52, s_kb_mode == KB_FOLDER_RENAME ? coll_rgb(s_fold_sel) : T.accent, s_kb, 0, false, NULL);
        gfx_text_align(c, FONT_TITLE, 0, 80, 256, title, T.text, ALIGN_CENTER);
        if (s_kb_mode == KB_PROFILE) ui_avatar(c, 128, 140, 22, s_kb[0] ? s_kb : "?");
        else if (s_kb_mode == KB_NEWPROFILE) {
            uint16_t a;
            profile_color(profiles_free_color(), &a, NULL, NULL, NULL);
            ui_avatar_slot(c, -1, 128, 140, 22, s_kb[0] ? s_kb : "?", a);
        }
        else if (s_kb_mode == KB_NOTE || s_kb_mode == KB_GREETING) gfx_text_wrap(c, FONT_BODY, 20, 112, 216, 4, 15, s_kb, T.text2);
        return;
    }
    char head[64];
    if (!s_kb[0]) snprintf(head, sizeof(head), "%s", tr(S_SEARCH_HINT, L()));
    else if (s_res_n == 0) snprintf(head, sizeof(head), "%s", tr(S_NO_RESULTS, L()));
    else snprintf(head, sizeof(head), tr(S_RESULTS, L()), s_res_n);
    gfx_icon(c, ICON_SEARCH, 12, 28, T.text2);
    gfx_text(c, FONT_BODY, 32, 28, head, T.text2, 200);
    int rows = 6, rh = 24, y0 = 48;
    int first = s_res_sel - rows + 1;
    if (first < 0) first = 0;
    for (int i = 0; i < rows && first + i < s_res_n; i++) {
        int idx = s_results[first + i];
        const GameItem *it = library_get(idx);
        if (!it) continue;
        int y = y0 + i * rh;
        bool sel = (first + i == s_res_sel);
        if (sel) gfx_round_rect(c, 8, y - 1, 240, rh - 2, 4, T.panel, 32);
        ui_thumb(c, idx, it, 14, y + 1, 18);
        gfx_text(c, FONT_BODY, 40, y + 4, it->title, sel ? T.sel_text : T.text, 160);
        gfx_text_align(c, FONT_SMALL, 200, y + 6, 42, romdb_system_short((ItemType)it->type), T.text3, ALIGN_RIGHT);
        if (sel) ui_sel_frame(c, 8 + 3, y + 2, 240 - 6, rh - 8, 2, s_frame);
    }
}

static void render_top_notes(void) {
    const GameItem *it = library_get(s_notes_lib);
    hero(&g_top, s_notes_lib, it);
}

static void render_top_fatal(void) {
    Canvas *c = &g_top;
    gfx_rect(c, 0, 0, 256, 192, T.bg_top);
    gfx_icon_centered(c, ICON_WARNING_L, 128, 64, T.danger);
    gfx_text_align(c, FONT_TITLE, 0, 92, 256, tr(S_SD_ERROR, L()), T.text, ALIGN_CENTER);
    gfx_text_wrap(c, FONT_BODY, 24, 118, 208, 3, 15, tr(S_SD_ERROR_HINT, L()), T.text2);
}

// Login screen (Switch-like "who is playing?")
static void login_status(Canvas *c) {
    char d[24], clock[16];
    ui_format_date((uint32_t)time(NULL), d, sizeof(d));
    gfx_text(c, FONT_BODY, 10, 5, d, T.text, 120);
    ui_format_clock(clock, sizeof(clock));
    int tw = gfx_text_width(FONT_BODY, clock);
    gfx_text(c, FONT_BODY, 220 - tw - 8, 5, clock, T.text, 0);
    ui_battery(c, 228, 7);
}

static void render_top_login(void) {
    Canvas *c = &g_top;
    int slot = login_slot_at(s_login_sel);
    const ProfileInfo *pi = profiles_get(slot);
    Color col = pi ? profile_accent(slot) : T.text3;
    if (pi && s_login_wp_slot == slot && ui_wallpaper_present()) ui_wallpaper_draw(c);
    else gfx_vgradient(c, 0, 0, 256, 192, gfx_mix(T.bg_top, col, T.light ? 60 : 80), T.bg_top);
    login_status(c);
    const int cx = 128, cy = 76;
    if (!pi) {
        gfx_shadow(c, cx - 32, cy - 32, 64, 64, 32, 4, 16);
        gfx_circle(c, cx, cy, 32, T.tile, 32);
        gfx_icon_centered(c, ICON_PLUS_L, cx, cy, T.text2);
        gfx_text_align(c, FONT_TITLE, 0, 120, 256, tr(S_NEW_PROFILE, L()), T.text, ALIGN_CENTER);
        return;
    }
    gfx_shadow(c, cx - 35, cy - 35, 70, 70, 35, 4, 18);
    gfx_circle(c, cx, cy, 36, col, 32);
    ui_avatar_slot(c, slot, cx, cy, 32, pi->name, col);
    int tw = gfx_text_width(FONT_TITLE, pi->name);
    if (tw > 200) tw = 200;
    int nx = 128 - tw / 2 - (pi->pin_len ? 9 : 0);
    gfx_text(c, FONT_TITLE, nx, 120, pi->name, T.text, 200);
    if (pi->pin_len) gfx_icon(c, ICON_LOCK_S, nx + tw + 6, 124, T.text2);
    if (pi->last_title[0]) {
        char l[96], d[24];
        snprintf(l, sizeof(l), tr(S_LAST_GAME, L()), pi->last_title);
        gfx_text_align(c, FONT_SMALL, 10, 146, 236, l, T.text2, ALIGN_CENTER);
        ui_format_date(pi->last_ts, d, sizeof(d));
        gfx_text_align(c, FONT_SMALL, 10, 160, 236, d, T.text3, ALIGN_CENTER);
    }
}

// Profile statistics: totals and the last 7 days on top
static void render_top_stats(void) {
    Canvas *c = &g_top;
    gfx_vgradient(c, 0, 0, 256, 120, gfx_mix(T.bg_top, T.accent, T.light ? 40 : 50), T.bg_top);
    gfx_rect(c, 0, 120, 256, 72, T.bg_top);
    ui_status_bar(c, &s_cfg);
    gfx_text(c, FONT_BOLD, 12, 26, tr(S_SET_STATS, L()), T.text, 232);
    // three numbers
    char v[3][24];
    fmt_minutes(s_st_total, v[0], sizeof(v[0]));
    snprintf(v[1], sizeof(v[1]), "%u", (unsigned)s_st_games);
    snprintf(v[2], sizeof(v[2]), "%u", (unsigned)s_st_launches);
    const StrId lab[3] = { S_STATS_TOTAL, S_STATS_GAMES, S_STATS_LAUNCHES };
    const int bx[3] = { 10, 104, 180 }, bw[3] = { 90, 72, 66 };
    for (int i = 0; i < 3; i++) {
        gfx_round_rect(c, bx[i], 46, bw[i], 38, 6, T.panel, 28);
        gfx_text_align(c, FONT_BOLD, bx[i], 51, bw[i], v[i], i == 0 ? T.sel_text : T.text, ALIGN_CENTER);
        gfx_text_align(c, FONT_SMALL, bx[i], 68, bw[i], tr(lab[i], L()), T.text2, ALIGN_CENTER);
    }
    // last 7 days
    gfx_text(c, FONT_SMALL, 12, 92, tr(S_STATS_WEEK, L()), T.text2, 200);
    uint32_t mx = 1;
    for (int i = 0; i < 7; i++) if (s_st_week[i] > mx) mx = s_st_week[i];
    char wd[64];
    snprintf(wd, sizeof(wd), "%s", tr(S_WEEKDAYS, L()));
    const char *names[7];
    int nn = 0;
    for (char *p = wd; *p && nn < 7;) {
        names[nn++] = p;
        char *sp = strchr(p, ' ');
        if (!sp) break;
        *sp = '\0';
        p = sp + 1;
    }
    uint32_t today = (uint32_t)time(NULL) / 86400u;
    const int base = 168, hmax = 52, w = 22, gap = 12, x0 = (256 - (7 * w + 6 * gap)) / 2;
    gfx_rect(c, x0 - 4, base, 7 * w + 6 * gap + 8, 1, T.divider);
    for (int i = 0; i < 7; i++) {
        int h = (int)(s_st_week[i] * hmax / mx);
        if (s_st_week[i] && h < 3) h = 3;
        int x = x0 + i * (w + gap);
        if (h > 0) gfx_round_rect(c, x, base - h, w, h + 3, 4, i == 6 ? T.accent : gfx_mix(T.panel, T.accent, 110), 32);
        else gfx_rect(c, x + 4, base - 2, w - 8, 2, T.divider);
        if (s_st_week[i]) {
            char m[16];
            if (s_st_week[i] >= 60) snprintf(m, sizeof(m), "%u.%u", (unsigned)(s_st_week[i] / 60), (unsigned)(s_st_week[i] % 60 * 10 / 60));
            else snprintf(m, sizeof(m), "%u'", (unsigned)s_st_week[i]);
            gfx_text_align(c, FONT_SMALL, x - 6, base - h - 13, w + 12, m, T.text2, ALIGN_CENTER);
        }
        int dow = (int)((today - (uint32_t)(6 - i) + 4) % 7);   // 1 Jan 1970 was a Thursday
        if (nn == 7) gfx_text_align(c, FONT_SMALL, x - 6, base + 4, w + 12, names[dow], i == 6 ? T.sel_text : T.text3, ALIGN_CENTER);
    }
}

static void render_top(void) {
    if (s_ss_on) { render_top_ss(); return; }
    if (s_launching) {
        Canvas *c = &g_top;
        const GameItem *it = item_for(s_launch_lib);
        gfx_rect(c, 0, 0, 256, 192, T.bg_top);
        if (it) {
            int sz = 96;
            if (!ui_cover(c, cover_of(s_launch_lib, it), 80, 28, sz, 8)) {
                gfx_shadow(c, 80, 28, sz, sz, 10, 5, 26);
                ui_tile(c, s_launch_lib, it, 80, 28, sz, 10);
            }
            gfx_text_align(c, FONT_TITLE, 10, 136, 236, it->title, T.text, ALIGN_CENTER);
        }
        ui_spinner(c, 128, 172, 7, s_frame, T.accent);
        return;
    }
    switch (s_scr) {
        case SCR_HOME: if (home_clock_on()) render_top_clock(); else render_top_home(); break;
        case SCR_ALL:
            if (s_view_n > 0) hero(&g_top, s_view[s_grid_sel], library_get(s_view[s_grid_sel]));
            else if (s_tab_n > 0 && s_tabs[s_tab] == TYPE_APP) hero_simple(&g_top, ICON_GRID_L, T.accent, tr(S_APPS, L()), tr(S_APPS_PICK_HINT, L()), NULL);
            else hero_simple(&g_top, ICON_GAMEPAD_L, T.accent, tr(S_NO_GAMES, L()), tr(S_NO_GAMES_HINT, L()), NULL);
            break;
        case SCR_SETTINGS: render_top_settings(); break;
        case SCR_KEYBOARD: render_top_keyboard(); break;
        case SCR_NOTES: render_top_notes(); break;
        case SCR_FATAL: render_top_fatal(); break;
        case SCR_LOGIN: render_top_login(); break;
        case SCR_STATS: render_top_stats(); break;
        case SCR_TOUCHCAL: render_top_touchcal(); break;
        case SCR_FOLDERS: render_top_folders(); break;
        case SCR_HOMEEDIT: render_top_settings(); break;
        case SCR_WIFI: render_top_wifi(); break;
        case SCR_PHOTOS: render_top_photos(); break;
        case SCR_CHEATS: { const GameItem *ci = item_for(s_ch_lib); hero(&g_top, s_ch_lib, ci); break; }
        case SCR_APPS: render_top_apps(); break;
        case SCR_STORE: render_top_store(); break;
        case SCR_NEARBY: render_top_nearby(); break;
        case SCR_PAD: pad_render_top(&g_top); break;
    }
    if (s_ovl == OVL_DIALOG || s_ovl == OVL_PIN || s_ovl == OVL_MENU) ui_scrim(&g_top, 14);
}

// ---------------------------------------------------------------------------
// Screens: BOTTOM
// ---------------------------------------------------------------------------

// Caches a band of rows that only changes with a few settings (button row + hint bar):
// drawn once, then copied while the key stays the same - saves most of the anti-aliasing work
typedef struct {
    uint32_t key;
    bool     valid;
    int      y0, y1;
    Color   *px;
    int      hit_n;
    HitRect  hits[6];
} RowCache;

static bool rowcache_restore(RowCache *rc, Canvas *c, int y0, int y1, uint32_t key) {
    if (!rc->valid || rc->key != key || rc->y0 != y0 || rc->y1 != y1 || !rc->px) return false;
    memcpy(c->px + y0 * c->w, rc->px, (size_t)(y1 - y0) * c->w * sizeof(Color));
    s_hit_n = rc->hit_n;
    memcpy(s_hits, rc->hits, sizeof(s_hits));
    return true;
}

static void rowcache_store(RowCache *rc, const Canvas *c, int y0, int y1, uint32_t key) {
    if (!rc->px) rc->px = malloc((size_t)(SCREEN_HEIGHT) * SCREEN_WIDTH * sizeof(Color) / 2);
    if (!rc->px || (y1 - y0) > SCREEN_HEIGHT / 2) { rc->valid = false; return; }
    memcpy(rc->px, c->px + y0 * c->w, (size_t)(y1 - y0) * c->w * sizeof(Color));
    rc->key = key; rc->y0 = y0; rc->y1 = y1; rc->valid = true;
    rc->hit_n = s_hit_n;
    memcpy(rc->hits, s_hits, sizeof(s_hits));
}

static RowCache s_rc_home_bar;

static void render_home(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    // keep the selected tile visible (Switch-style margin of one tile)
    int sel_left = s_home_sel * (TILE + TILE_GAP);
    if (!s_home_dragging) {
        if (sel_left - s_home_scroll_target < 0) s_home_scroll_target = sel_left;
        if (sel_left + TILE - s_home_scroll_target > 256 - 2 * TILE_X0) s_home_scroll_target = sel_left + TILE - (256 - 2 * TILE_X0);
        int max_scroll = s_home_n * (TILE + TILE_GAP) - TILE_GAP - (256 - 2 * TILE_X0);
        if (max_scroll < 0) max_scroll = 0;
        if (s_home_scroll_target > max_scroll) s_home_scroll_target = max_scroll;
        if (s_home_scroll_target < 0) s_home_scroll_target = 0;
        s_home_scroll = ui_ease(s_home_scroll, s_home_scroll_target, 2);
    }

    // label above the selected tile
    if (s_home_focus == 0 && s_home_n > 0) {
        int id = s_home[s_home_sel];
        const char *label = (id == HOME_ALLSW) ? tr(S_ALL_SOFTWARE, L()) : (id == HOME_PAD) ? tr(S_PAD_HOME, L()) : (id == HOME_PHOTOS) ? tr(S_PHOTOS, L()) : (id == HOME_STORE) ? tr(S_STORE, L()) : (id == HOME_NEARBY) ? tr(S_NEARBY, L()) : (item_for(id) ? item_for(id)->title : "");
        int tw = gfx_text_width(FONT_BOLD, label);
        if (tw > 224) tw = 224;
        int lx = home_tile_x(s_home_sel);
        if (lx + tw > 248) lx = 248 - tw;
        if (lx < 8) lx = 8;
        gfx_text(c, FONT_BOLD, lx, 20, label, T.sel_text, 224);
    }

    for (int i = 0; i < s_home_n; i++) {
        int x = home_tile_x(i);
        if (x > 256 || x + TILE < 0) continue;
        int id = s_home[i];
        if (id == HOME_ALLSW) ui_tile_allsoft(c, x, TILE_Y, TILE, 6, tr(S_ALL_SOFTWARE, L()));
        else if (id == HOME_PAD) pad_tile(c, x, TILE_Y, TILE);
        else if (id == HOME_PHOTOS) photos_tile(c, x, TILE_Y, TILE);
        else if (id == HOME_STORE) store_tile(c, x, TILE_Y, TILE);
        else if (id == HOME_NEARBY) nearby_tile(c, x, TILE_Y, TILE);
        else if (id == HOME_CARD && (s_cfg.home_btns & HOMEBTN_CARTBOX) && ui_card_cover()) {
            // the game card's box art on the tile too (Settings > Home screen)
            gfx_round_rect(c, x, TILE_Y, TILE, TILE, 6, T.panel, 32);
            ui_cover(c, ui_card_cover(), x + 2, TILE_Y + 2, TILE - 4, 4);
        } else {
            const GameItem *fr = (i == s_home_sel && s_home_focus == 0 && id >= 0) ? anim_frame(item_for(id)) : NULL;
            if (fr) ui_tile_uncached(c, fr, x, TILE_Y, TILE, 6);       // the selected game's icon moves
            else ui_tile(c, id, item_for(id), x, TILE_Y, TILE, 6);
        }
        if (pressed_at(x, TILE_Y, TILE, TILE)) gfx_round_rect(c, x, TILE_Y, TILE, TILE, 6, C8(0, 0, 0), 9);
        if (id == HOME_CARD) {
            gfx_circle(c, x + TILE - 9, TILE_Y + 9, 7, C8(255, 160, 40), 32);
            gfx_icon_centered(c, ICON_CART, x + TILE - 9, TILE_Y + 9, C8(255, 255, 255));
        }
    }
    if (s_home_focus == 0 && s_home_n > 0) ui_sel_frame(c, home_tile_x(s_home_sel), TILE_Y, TILE, TILE, 6, s_frame);

    bool game = s_home_focus == 0 && s_home_n > 0 && s_home[s_home_sel] != HOME_ALLSW && s_home[s_home_sel] != HOME_PAD && s_home[s_home_sel] != HOME_PHOTOS && s_home[s_home_sel] != HOME_STORE && s_home[s_home_sel] != HOME_NEARBY;
    const int bar_y0 = HB_Y - HB_R - 4;
    uint32_t bar_key = 0x80000000u | ((uint32_t)T.light << 0) | ((uint32_t)s_cfg.language << 1) | ((uint32_t)game << 5) |
                       ((uint32_t)slot1_present() << 6) | ((uint32_t)s_cfg.home_hide << 8) | ((uint32_t)s_cfg.home_show << 16);
    if (s_home_focus == 0 && rowcache_restore(&s_rc_home_bar, c, bar_y0, SCREEN_HEIGHT, bar_key)) return;
    gfx_rect(c, 0, bar_y0, SCREEN_WIDTH, SCREEN_HEIGHT - bar_y0, T.bg);
    if (!hb_visible(s_home_btn)) s_home_btn = HB_SETTINGS;
    for (int b = 0; b < HB_COUNT; b++) {
        if (!hb_visible(b)) continue;
        int r = hb_r();
        ui_round_button(c, btn_cx(b), HB_Y, r, k_home_btn_icon[b], home_btn_color(b), s_home_focus == 1 && s_home_btn == b, s_frame);
        if (pressed_at(btn_cx(b) - r, HB_Y - r, r * 2, r * 2)) gfx_circle(c, btn_cx(b), HB_Y, r, C8(0, 0, 0), 9);
    }
    if (s_home_focus == 1) {
        const char *lbl = home_btn_label(s_home_btn);
        int tw = gfx_text_width(FONT_SMALL, lbl);
        int lx = btn_cx(s_home_btn) - tw / 2;
        if (lx < 4) lx = 4;
        if (lx + tw > 252) lx = 252 - tw;
        gfx_text(c, FONT_SMALL, lx, HB_Y + HB_R + 5, lbl, T.text, 0);
    }

    Hint h[3];
    int n = 0;
    if (game) { h[n].icon = BTN_Y; h[n].text = tr(S_OPTIONS, L()); h[n].key = KEY_Y; n++; }
    h[n].icon = BTN_A;
    h[n].text = game ? tr(S_START, L()) : tr(S_OK, L());
    h[n].key = KEY_A; n++;
    s_hit_n = ui_hint_bar(c, h, n, s_hits);
    if (s_home_focus == 0) rowcache_store(&s_rc_home_bar, c, bar_y0, SCREEN_HEIGHT, bar_key);
}

static int grid_row_y(int row) { return GRID_Y0 + row * GRID_STRIDE - s_grid_scroll; }

static const char *tab_label(ItemType t) {
    if (t == TYPE_UNKNOWN) return tr(S_TAB_ALL, L());
    if (t == TYPE_APP) return tr(S_APPS, L());
    if (t == TYPE_FOLDER) return "★";
    if ((int)t >= 64) return coll_name((int)t - 64);
    return romdb_system_short(t);
}

static void render_all(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    char head[64];
    if (s_pick_coll >= 0) snprintf(head, sizeof(head), tr(S_PICK_TITLE, L()), coll_name(s_pick_coll));
    else snprintf(head, sizeof(head), "%s", tr(S_ALL_SOFTWARE, L()));
    gfx_text(c, FONT_BOLD, 12, 4, head, T.text, 170);
    char cnt[32];
    snprintf(cnt, sizeof(cnt), tr(S_SOFTWARE_COUNT, L()), s_pick_coll >= 0 ? coll_size(s_pick_coll) : s_view_n);
    gfx_text_align(c, FONT_SMALL, 150, 6, 94, cnt, T.text2, ALIGN_RIGHT);

    // category tabs (the strip scrolls so the active tab is always visible)
    gfx_icon(c, BTN_L, 8, 26, T.text3);
    gfx_icon(c, BTN_R, 236, 26, T.text3);
    int tab_x[12], tab_w[12], total = 0;
    for (int i = 0; i < s_tab_n; i++) {
        tab_w[i] = gfx_text_width(FONT_SMALL, tab_label(s_tabs[i])) + 14;
        tab_x[i] = total;
        total += tab_w[i] + 3;
    }
    int strip_x0 = 23, strip_w = 232 - strip_x0;
    int off = 0;
    if (total > strip_w) {
        off = tab_x[s_tab] + tab_w[s_tab] / 2 - strip_w / 2;
        if (off < 0) off = 0;
        if (off > total - strip_w) off = total - strip_w;
    }
    s_tab_scroll = off;
    gfx_set_clip(c, strip_x0, 22, strip_w, 20);
    for (int i = 0; i < s_tab_n; i++) {
        int x = strip_x0 + tab_x[i] - off;
        Color pill = T.accent;
        if ((int)s_tabs[i] >= 64) {
            uint16_t a;
            profile_color(coll_color((int)s_tabs[i] - 64), &a, NULL, NULL, NULL);
            pill = a;
            if (i != s_tab) gfx_circle(c, x + 3, 31, 2, pill, 32);
        }
        if (i == s_tab) gfx_round_rect(c, x, 24, tab_w[i], 15, 7, pill, 32);
        gfx_text(c, FONT_SMALL, x + 7, 26, tab_label(s_tabs[i]), i == s_tab ? C8(255, 255, 255) : T.text2, 0);
    }
    gfx_reset_clip(c);

    // grid with smooth scrolling
    int rows = (s_view_n + GRID_COLS - 1) / GRID_COLS;
    int sel_row = s_grid_sel / GRID_COLS;
    if (!s_grid_dragging) {
        int top = sel_row * GRID_STRIDE;
        int view_h = GRID_BOTTOM - GRID_Y0;
        if (top < s_grid_scroll_target) s_grid_scroll_target = top;
        if (top + GRID_TILE + 6 > s_grid_scroll_target + view_h) s_grid_scroll_target = top + GRID_TILE + 6 - view_h;
        int max_scroll = rows * GRID_STRIDE - view_h;
        if (max_scroll < 0) max_scroll = 0;
        if (s_grid_scroll_target > max_scroll) s_grid_scroll_target = max_scroll;
        if (s_grid_scroll_target < 0) s_grid_scroll_target = 0;
        s_grid_scroll = ui_ease(s_grid_scroll, s_grid_scroll_target, 2);
    }
    gfx_set_clip(c, 0, GRID_Y0 - 6, 256, GRID_BOTTOM - GRID_Y0 + 6);
    for (int i = 0; i < s_view_n; i++) {
        int row = i / GRID_COLS, col = i % GRID_COLS;
        int y = grid_row_y(row);
        if (y > GRID_BOTTOM || y + GRID_TILE < GRID_Y0 - 6) continue;
        int tx = GRID_X0 + col * GRID_STRIDE;
        const GameItem *fr = (i == s_grid_sel) ? anim_frame(library_get(s_view[i])) : NULL;
        if (fr) ui_tile_uncached(c, fr, tx, y, GRID_TILE, 5);
        else ui_tile(c, s_view[i], library_get(s_view[i]), tx, y, GRID_TILE, 5);
        if (pressed_at(tx, y, GRID_TILE, GRID_TILE)) gfx_round_rect(c, tx, y, GRID_TILE, GRID_TILE, 5, C8(0, 0, 0), 9);
        if (s_pick_coll >= 0) {
            bool on = coll_has(s_pick_coll, library_get(s_view[i])->path);
            if (!on) gfx_rect_a(c, tx, y, GRID_TILE, GRID_TILE, T.bg, 14);
            gfx_circle(c, tx + GRID_TILE - 9, y + 9, 7, on ? T.accent : C8(255, 255, 255), on ? 32 : 22);
            if (on) gfx_icon_centered(c, ICON_CHECK, tx + GRID_TILE - 9, y + 9, C8(255, 255, 255));
            else gfx_ring(c, tx + GRID_TILE - 9, y + 9, 7, 1, T.text3, 32);
        } else if (library_get(s_view[i])->flags & ITEM_FLAG_FAVORITE) {
            // just the heart (a dark copy 1 px lower keeps it readable on light icons)
            gfx_icon_centered(c, ICON_HEART, tx + GRID_TILE - 7, y + 8, C8(40, 10, 20));
            gfx_icon_centered(c, ICON_HEART, tx + GRID_TILE - 7, y + 7, C8(255, 80, 110));
        }
    }
    if (s_view_n > 0) {
        int sy = grid_row_y(sel_row);
        ui_sel_frame(c, GRID_X0 + (s_grid_sel % GRID_COLS) * GRID_STRIDE, sy, GRID_TILE, GRID_TILE, 5, s_frame);
    } else {
        bool folder = s_tab_n > 0 && (int)s_tabs[s_tab] >= 64;
        bool apps = s_tab_n > 0 && s_tabs[s_tab] == TYPE_APP;
        bool favs = s_tab_n > 0 && s_tabs[s_tab] == TYPE_FOLDER;
        if (apps) gfx_text_wrap(c, FONT_BODY, 24, 84, 208, 4, 15, tr(S_APPS_EMPTY, L()), T.text2);
        else if (favs) gfx_text_wrap(c, FONT_BODY, 24, 90, 208, 3, 15, tr(S_FAVS_EMPTY, L()), T.text2);
        else if (folder) gfx_text_wrap(c, FONT_BODY, 24, 90, 208, 3, 15, tr(S_FOLDER_EMPTY, L()), T.text2);
        else gfx_text_align(c, FONT_BODY, 0, 96, 256, tr(S_NO_GAMES, L()), T.text2, ALIGN_CENTER);
    }
    gfx_reset_clip(c);
    // scroll indicator
    int view_h = GRID_BOTTOM - GRID_Y0;
    int content = rows * GRID_STRIDE;
    if (content > view_h) {
        int bar_h = view_h * view_h / content;
        if (bar_h < 12) bar_h = 12;
        int bar_y = GRID_Y0 + (view_h - bar_h) * s_grid_scroll / (content - view_h);
        gfx_round_rect(c, 251, bar_y, 3, bar_h, 1, T.text3, 20);
    }

    if (s_pick_coll >= 0) {
        Hint hp[2] = { { BTN_B, tr(S_DONE, L()), KEY_B }, { BTN_A, tr(S_TICK, L()), KEY_A } };
        s_hit_n = ui_hint_bar(c, hp, s_view_n > 0 ? 2 : 1, s_hits);
        return;
    }
    if (s_tab_n > 0 && s_tabs[s_tab] == TYPE_APP) {
        Hint ha[4] = {
            { BTN_B, tr(S_BACK, L()), KEY_B },
            { BTN_START, tr(S_ADD, L()), KEY_START },
            { BTN_Y, tr(S_OPTIONS, L()), KEY_Y },
            { BTN_A, tr(S_START, L()), KEY_A },
        };
        s_hit_n = ui_hint_bar(c, ha, s_view_n > 0 ? 4 : 2, s_hits);
        return;
    }
    Hint h[4] = {
        { BTN_B, tr(S_BACK, L()), KEY_B },
        { BTN_START, tr(S_FOLDERS, L()), KEY_START },
        { BTN_Y, tr(S_OPTIONS, L()), KEY_Y },
        { BTN_A, tr(S_START, L()), KEY_A },
    };
    if (s_view_n > 0) s_hit_n = ui_hint_bar(c, h, 4, s_hits);
    else s_hit_n = ui_hint_bar(c, h, 2, s_hits);
}

// Keyboard layout (10 columns)
static const char *const k_keys[5][10] = {
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
    { "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ş" },
    { "Z", "X", "C", "V", "B", "N", "M", "Ç", "Ö", "Ü" },
    { "Ğ", "İ", "-", " ", " ", " ", " ", "<", "<", "OK" },
};
// Symbol page (profile names): the "☆" key switches pages
static const char *const k_syms[5][10] = {
    { "★", "☆", "♥", "♡", "✿", "❀", "♪", "♫", "☺", "ツ" },
    { "༺", "༻", "꧁", "꧂", "『", "』", "【", "】", "«", "»" },
    { "✦", "✧", "❤", "♛", "♚", "☾", "☀", "⚡", "✓", "✗" },
    { "♠", "♣", "♦", "●", "○", "◆", "▲", "▼", "∞", "-" },
    { ".", "_", "ABC", " ", " ", " ", " ", "<", "<", "OK" },
};
// Wi-Fi passwords: plain ASCII, small and capital letters (↑), a page of symbols (#+=)
static const char *const k_wkeys[5][10] = {
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l", "@" },
    { "z", "x", "c", "v", "b", "n", "m", ".", "_", "!" },
    { "↑", "#+=", "-", " ", " ", " ", " ", "<", "<", "OK" },
};
static const char *const k_wsyms[5][10] = {
    { "!", "@", "#", "$", "%", "^", "&", "*", "(", ")" },
    { "-", "_", "=", "+", "[", "]", "{", "}", ";", ":" },
    { "'", "\"", ",", ".", "/", "?", "\\", "|", "~", "`" },
    { "<", ">", "1", "2", "3", "4", "5", "6", "7", "8" },
    { "↑", "abc", "-", " ", " ", " ", " ", "<", "<", "OK" },
};
static const char *kb_key(int r, int col) {
    if (s_kb_mode == KB_WIFIPASS) {
        const char *k = s_kb_sym ? k_wsyms[r][col] : k_wkeys[r][col];
        if (!s_kb_sym && s_kb_caps && k[0] >= 'a' && k[0] <= 'z' && !k[1]) {
            static char up[2];
            up[0] = (char)(k[0] - 32); up[1] = '\0';
            return up;
        }
        return k;
    }
    if (r == 4 && col == 2 && !s_kb_sym) return "☆";
    if (r == 1 && col == 1 && !s_kb_sym && L() == LANG_AZ) return "Ə";      // Azerbaijani has no W
    return s_kb_sym ? k_syms[r][col] : k_keys[r][col];
}
#define KB_X0 4
#define KB_Y0 40
#define KB_KW 23
#define KB_KH 24
#define KB_GAP 2

static void kb_key_rect(int r, int col, int *x, int *y, int *w) {
    *x = KB_X0 + col * (KB_KW + KB_GAP);
    *y = KB_Y0 + r * (KB_KH + KB_GAP);
    *w = KB_KW;
    if (r == 4) {
        if (col >= 3 && col <= 6) { *x = KB_X0 + 3 * (KB_KW + KB_GAP); *w = 4 * KB_KW + 3 * KB_GAP; }
        if (col >= 7 && col <= 8) { *x = KB_X0 + 7 * (KB_KW + KB_GAP); *w = 2 * KB_KW + KB_GAP; }
    }
}

static void render_keyboard(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    // text field
    gfx_round_rect(c, 8, 6, 240, 26, 6, T.panel, 32);
    gfx_icon(c, s_kb_mode == KB_SEARCH ? ICON_SEARCH : s_kb_mode == KB_WIFIPASS ? ICON_LOCK_S : (s_kb_mode == KB_NOTE ? ICON_NOTE : (s_kb_mode == KB_FOLDER || s_kb_mode == KB_FOLDER_RENAME) ? ICON_FOLDER : ICON_USER), 16, 11, T.text2);
    const char *shown = s_kb;
    int tw = gfx_text_width(FONT_BODY, shown);
    while (tw > 200 && *shown) { shown++; while ((*shown & 0xC0) == 0x80) shown++; tw = gfx_text_width(FONT_BODY, shown); }
    gfx_text(c, FONT_BODY, 38, 11, shown, T.text, 204);
    if ((s_frame / 20) & 1) gfx_rect(c, 38 + tw + 1, 10, 1, 16, T.accent);
    for (int r = 0; r < 5; r++) {
        for (int col = 0; col < 10; col++) {
            if (r == 4 && (col == 4 || col == 5 || col == 6 || col == 8)) continue;
            int x, y, w;
            kb_key_rect(r, col, &x, &y, &w);
            const char *k = kb_key(r, col);
            bool special = (r == 4 && col >= 2);
            Color bg = special ? T.panel_hi : T.panel;
            if (r == 4 && col == 9) bg = T.accent;
            if (r == 4 && col == 0 && s_kb_mode == KB_WIFIPASS && s_kb_caps) bg = T.accent;
            gfx_round_rect(c, x, y, w, KB_KH, 5, bg, 32);
            if (r == 4 && col == 3) gfx_text_align(c, FONT_SMALL, x, y + 6, w, tr(S_KB_SPACE, L()), T.text2, ALIGN_CENTER);
            else if (r == 4 && col == 7) gfx_icon_centered(c, ICON_CHEV_L, x + w / 2, y + KB_KH / 2, T.text);
            else if (r == 4 && col == 9) gfx_icon_centered(c, ICON_CHECK, x + w / 2, y + KB_KH / 2, C8(255, 255, 255));
            else gfx_text_align(c, FONT_BODY, x, y + 5, w, k, T.text, ALIGN_CENTER);
        }
    }
    Hint h[3] = {
        { BTN_B, tr(S_BACK, L()), KEY_B },
        { BTN_START, tr(S_KB_DONE, L()), KEY_START },
        { BTN_A, tr(S_START, L()), KEY_A },
    };
    s_hit_n = ui_hint_bar(c, h, (s_kb_mode == KB_SEARCH && s_res_n > 0) ? 3 : 2, s_hits);
}


// the tab pills; their x ranges are kept for taps
static int s_set_tab_x[SET_TABS + 1];
static void set_tabs_bar(Canvas *c) {
    int w[SET_TABS], total = 0;
    for (int i = 0; i < SET_TABS; i++) { w[i] = gfx_text_width(FONT_SMALL, tr(k_set_tab_name[i], L())) + 12; total += w[i]; }
    int gap = (244 - total) / (SET_TABS - 1);
    if (gap > 6) gap = 6;
    if (gap < 1) gap = 1;
    int x = (256 - (total + gap * (SET_TABS - 1))) / 2;
    for (int i = 0; i < SET_TABS; i++) {
        s_set_tab_x[i] = x;
        bool on = (i == s_set_tab);
        gfx_round_rect(c, x, 23, w[i], 15, 7, on ? T.accent : T.panel, 32);
        gfx_text_align(c, FONT_SMALL, x, 25, w[i], tr(k_set_tab_name[i], L()), on ? C8(255, 255, 255) : T.text2, ALIGN_CENTER);
        x += w[i] + gap;
    }
    s_set_tab_x[SET_TABS] = x;
}

static void render_settings(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_GEAR, 10, 4, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_SETTINGS, L()), T.text, 200);
    set_tabs_bar(c);
    int rh = 25, y0 = 44, view = (HINT_BAR_Y - y0) / rh;
    int n = set_tab_n(s_set_tab);
    if (s_set_pos < s_set_scroll) s_set_scroll = s_set_pos;
    if (s_set_pos >= s_set_scroll + view) s_set_scroll = s_set_pos - view + 1;
    if (s_set_scroll > n - view) s_set_scroll = n - view;
    if (s_set_scroll < 0) s_set_scroll = 0;
    gfx_set_clip(c, 0, y0 - 2, 256, HINT_BAR_Y - y0 + 1);
    for (int p = 0; p < n; p++) {
        int i = set_row(s_set_tab, p);
        int y = y0 + (p - s_set_scroll) * rh;
        if (y > HINT_BAR_Y || y + rh < y0 - 4) continue;
        bool sel = (i == s_set_sel);
        gfx_round_rect(c, 8, y, 240, rh - 3, 4, sel ? T.panel_hi : T.panel, 32);
        if (pressed_at(8, y, 240, rh - 3)) gfx_round_rect(c, 8, y, 240, rh - 3, 4, C8(0, 0, 0), 7);
        gfx_icon(c, k_settings[i].icon, 16, y + 3, sel ? T.accent : T.text2);
        gfx_text(c, FONT_BODY, 40, y + 4, tr(k_settings[i].name, L()), T.text, 120);
        char buf[32];
        const char *v = setting_value(i, buf, sizeof(buf));
        if (i == SET_RESCAN || i == SET_STATS || i == SET_SWITCH || i == SET_DELPROF || i == SET_HOME) gfx_icon(c, ICON_CHEV_R, 230, y + 5, i == SET_DELPROF ? T.danger : T.text3);
        else gfx_text_align(c, FONT_BODY, 150, y + 4, 92, v, sel ? T.sel_text : T.text2, ALIGN_RIGHT);
        if (sel) ui_sel_frame(c, 8 + 3, y + 3, 240 - 6, rh - 9, 2, s_frame);
    }
    gfx_reset_clip(c);
    Hint h[3] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_Y, tr(S_PREVIEW, L()), KEY_Y }, { BTN_A, tr(S_CHANGE, L()), KEY_A } };
    if (s_set_sel == SET_SPLASH) s_hit_n = ui_hint_bar(c, h, 3, s_hits);
    else { h[1] = h[2]; s_hit_n = ui_hint_bar(c, h, 2, s_hits); }
}

// ---------------------------------------------------------------------------
// Home screen editor: what the home screen shows (Settings > Home screen)
// ---------------------------------------------------------------------------
enum { HE_PAD, HE_PHOTOS, HE_NEARBY, HE_CLOCK, HE_GAMES, HE_CARD_TILE, HE_ALL_TILE, HE_BTN_CARD, HE_BTN_ALL, HE_BTN_SEARCH, HE_STORE,
       HE_BTN_PAD, HE_BTN_NEARBY, HE_BTN_PHOTOS, HE_BTN_POWER, HE_CARD_BOX, HE_COUNT };
static const struct { StrId name; IconId icon; uint8_t hide_bit; } k_he[HE_COUNT] = {
    { S_HE_PAD, ICON_GAMEPAD, 0 },
    { S_HE_PHOTOS, ICON_PHOTO, 0 },
    { S_HE_NEARBY, ICON_USERS, 0 },
    { S_HE_CLOCK, ICON_CLOCK, 0 },
    { S_HE_GAMES, ICON_GRID, 0 },
    { S_HE_CARD_TILE, ICON_CART, HOMEHIDE_CARD_TILE },
    { S_HE_ALL_TILE, ICON_GRID, HOMEHIDE_ALL_TILE },
    { S_HE_BTN_CARD, ICON_CART, HOMEHIDE_BTN_CARD },
    { S_HE_BTN_ALL, ICON_GRID, HOMEHIDE_BTN_ALL },
    { S_HE_BTN_SEARCH, ICON_SEARCH, HOMEHIDE_BTN_SEARCH },
    { S_HE_STORE, ICON_BAG, 0 },
    { S_HE_BTN_PAD, ICON_GAMEPAD, 0 },
    { S_HE_BTN_NEARBY, ICON_USERS, 0 },
    { S_HE_BTN_PHOTOS, ICON_PHOTO, 0 },
    { S_HE_BTN_POWER, ICON_POWER, 0 },
    { S_HE_CARD_BOX, ICON_CART, 0 },
};
static const uint8_t k_he_btn[HE_COUNT] = { [HE_BTN_PAD] = HOMEBTN_PAD, [HE_BTN_NEARBY] = HOMEBTN_NEARBY,
                                            [HE_BTN_PHOTOS] = HOMEBTN_PHOTOS, [HE_BTN_POWER] = HOMEBTN_POWER,
                                            [HE_CARD_BOX] = HOMEBTN_CARTBOX };
static const uint8_t k_he_games[] = { 4, 8, 0, HOME_GAMES_NONE };   // 0 = all (14)

// rows shown: Store and Nearby only with "Try beta features"
static bool he_shown(int i) { return s_cfg.beta || (i != HE_NEARBY && i != HE_STORE && i != HE_BTN_NEARBY); }
static int he_count(void) { int n = 0; for (int i = 0; i < HE_COUNT; i++) n += he_shown(i); return n; }
static int he_row(int p) { for (int i = 0; i < HE_COUNT; i++) if (he_shown(i) && p-- == 0) return i; return 0; }

static bool he_on(int i) {
    if (i == HE_PAD) return (s_cfg.home_show & HOMESHOW_PAD) != 0;
    if (i == HE_PHOTOS) return !(s_cfg.home_show & HOMEOPT_NO_PHOTOS);
    if (i == HE_STORE) return !(s_cfg.home_show & HOMEOPT_NO_STORE);
    if (i == HE_NEARBY) return !(s_cfg.home_show & HOMEOPT_NO_NEARBY);
    if (i == HE_CLOCK) return !(s_cfg.home_show & HOMEOPT_NO_CLOCK);
    if (i == HE_BTN_POWER && !power_off_supported()) return true;   // stock ARM7: POWER cannot open the menu
    if (k_he_btn[i]) return (s_cfg.home_btns & k_he_btn[i]) != 0;
    return !(s_cfg.home_hide & k_he[i].hide_bit);
}

static void he_change(int i, int dir) {
    if (i == HE_PAD) s_cfg.home_show ^= HOMESHOW_PAD;
    else if (i == HE_PHOTOS) s_cfg.home_show ^= HOMEOPT_NO_PHOTOS;
    else if (i == HE_STORE) s_cfg.home_show ^= HOMEOPT_NO_STORE;
    else if (i == HE_NEARBY) s_cfg.home_show ^= HOMEOPT_NO_NEARBY;
    else if (i == HE_CLOCK) s_cfg.home_show ^= HOMEOPT_NO_CLOCK;
    else if (k_he_btn[i]) {
        if (i == HE_BTN_POWER && !power_off_supported()) { sfx(SFX_ERROR); return; }
        s_cfg.home_btns ^= k_he_btn[i];
    } else if (i == HE_GAMES) {
        int n = (int)sizeof(k_he_games), cur = 2;
        for (int k = 0; k < n; k++) if (k_he_games[k] == s_cfg.home_games) cur = k;
        s_cfg.home_games = k_he_games[(cur + n + dir) % n];
    } else s_cfg.home_hide ^= k_he[i].hide_bit;
    settings_save(&s_cfg);
    build_home();
    if (s_home_sel >= s_home_n) s_home_sel = 0;
    sfx(SFX_TOGGLE);
}

static void render_homeedit(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_HOME, 10, 4, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_SET_HOME, L()), T.text, 200);
    int rh = 26, y0 = 26, view = 6;
    if (s_he_sel < s_he_scroll) s_he_scroll = s_he_sel;
    if (s_he_sel >= s_he_scroll + view) s_he_scroll = s_he_sel - view + 1;
    gfx_set_clip(c, 0, y0 - 4, 256, HINT_BAR_Y - y0 + 2);
    for (int p = 0; p < he_count(); p++) {
        int i = he_row(p);
        int y = y0 + (p - s_he_scroll) * rh;
        if (y > HINT_BAR_Y || y + rh < y0 - 4) continue;
        bool sel = (p == s_he_sel);
        gfx_round_rect(c, 8, y, 240, rh - 3, 4, sel ? T.panel_hi : T.panel, 32);
        gfx_icon(c, k_he[i].icon, 16, y + 3, sel ? T.accent : T.text2);
        gfx_text(c, FONT_BODY, 40, y + 4, tr(k_he[i].name, L()), T.text, 150);
        char buf[16];
        const char *v;
        if (i == HE_GAMES) {
            int g = home_games_count();
            if (g == HOME_MAX) v = tr(S_HE_ALL, L());
            else { snprintf(buf, sizeof(buf), "%d", g); v = buf; }
        } else {
            v = tr(he_on(i) ? S_ON : S_OFF, L());
        }
        gfx_text_align(c, FONT_BODY, 160, y + 4, 82, v, sel ? T.sel_text : T.text2, ALIGN_RIGHT);
        if (sel) ui_sel_frame(c, 8 + 3, y + 3, 240 - 6, rh - 9, 2, s_frame);
    }
    gfx_reset_clip(c);
    Hint h[2] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_A, tr(S_CHANGE, L()), KEY_A } };
    s_hit_n = ui_hint_bar(c, h, 2, s_hits);
}

static void input_homeedit(u32 k, u32 rep) {
    if ((rep & KEY_DOWN) && s_he_sel < he_count() - 1) { s_he_sel++; sfx(SFX_TICK); }
    if ((rep & KEY_UP) && s_he_sel > 0) { s_he_sel--; sfx(SFX_TICK); }
    if (k & (KEY_A | KEY_RIGHT)) he_change(he_row(s_he_sel), 1);
    else if (k & KEY_LEFT) he_change(he_row(s_he_sel), -1);
    if (k & KEY_B) { s_scr = SCR_SETTINGS; sfx(SFX_BACK); }
}

// --- Cheats (Action Replay, usrcheat.dat) ------------------------------------------------
static int ch_next(int from, int dir) {
    for (int i = from; i >= 0 && i < cheats_count(); i += dir) {
        const CheatRow *r = cheats_row(i);
        if (r && !r->folder) return i;
    }
    return -1;
}

static void open_cheats(int lib_idx) {
    const GameItem *it = item_for(lib_idx);
    if (!it) return;
    int n = cheats_open(it->path);
    if (n <= 0 || ch_next(0, 1) < 0) {
        cheats_close();
        toast(tr(n == -1 ? S_CHEATS_NODB : n < 0 ? S_CHEATS_ERR : S_CHEATS_NONE, L()));
        sfx(SFX_ERROR);
        return;
    }
    s_ovl = OVL_NONE;
    s_ch_back = s_scr;
    s_ch_lib = lib_idx;
    s_ch_sel = ch_next(0, 1);
    s_ch_scroll = 0;
    s_ch_ver++;
    s_scr = SCR_CHEATS;
    sfx(SFX_DECIDE);
}

static void close_cheats(void) {
    if (!cheats_save()) toast(tr(S_CHEATS_SAVE_ERR, L()));
    cheats_close();
    s_scr = s_ch_back;
    sfx(SFX_BACK);
}

static void render_cheats(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_BOLT, 10, 4, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_OPT_CHEATS, L()), T.text, 130);
    char buf[48];
    snprintf(buf, sizeof(buf), tr(S_CHEATS_N_ON, L()), cheats_chosen());
    gfx_text_align(c, FONT_BODY, 150, 5, 98, buf, cheats_chosen() ? T.accent : T.text2, ALIGN_RIGHT);
    int n = cheats_count();
    // keep the heading of the chosen cheat's folder in view when moving up
    int top_want = s_ch_sel;
    const CheatRow *cur = cheats_row(s_ch_sel);
    if (cur && cur->parent >= 0 && s_ch_sel - cur->parent < CH_VIEW) top_want = cur->parent;
    if (top_want < s_ch_scroll) s_ch_scroll = top_want;
    if (s_ch_sel >= s_ch_scroll + CH_VIEW) s_ch_scroll = s_ch_sel - CH_VIEW + 1;
    gfx_set_clip(c, 0, CH_Y0 - 2, 256, CH_VIEW * CH_RH + 2);
    for (int i = s_ch_scroll; i < n && i < s_ch_scroll + CH_VIEW; i++) {
        const CheatRow *r = cheats_row(i);
        int y = CH_Y0 + (i - s_ch_scroll) * CH_RH;
        if (r->folder) {
            gfx_icon(c, ICON_FOLDER, 10, y + 1, T.text2);
            gfx_text(c, FONT_BOLD, 32, y + 2, r->name, T.text2, 214);
            continue;
        }
        bool sel = (i == s_ch_sel);
        int x = r->parent >= 0 ? 18 : 8;
        gfx_round_rect(c, x, y, 248 - x, CH_RH - 2, 4, sel ? T.panel_hi : T.panel, 32);
        // check box
        gfx_round_rect(c, x + 5, y + 3, 11, 11, 3, r->on ? T.accent : T.bg, 32);
        if (!r->on) gfx_round_rect(c, x + 6, y + 4, 9, 9, 2, sel ? T.panel_hi : T.panel, 32);
        else gfx_icon(c, ICON_CHECK, x + 3, y + 1, T.light ? C8(255, 255, 255) : C8(20, 20, 20));
        gfx_text(c, FONT_BODY, x + 22, y + 2, r->name, T.text, 248 - x - 28);
        if (sel) ui_sel_frame(c, x + 2, y + 2, 248 - x - 4, CH_RH - 6, 2, s_frame);
    }
    gfx_reset_clip(c);
    if (cur && cur->note && cur->note[0])
        gfx_text_wrap(c, FONT_SMALL, 10, CH_Y0 + CH_VIEW * CH_RH + 2, 236, 2, 10, cur->note, T.text2);
    Hint h[3] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_X, tr(S_CHEATS_ALL_OFF, L()), KEY_X }, { BTN_A, tr(S_TOGGLE, L()), KEY_A } };
    s_hit_n = ui_hint_bar(c, h, 3, s_hits);
}

static void input_cheats(u32 k, u32 rep) {
    if (rep & (KEY_DOWN | KEY_RIGHT)) {
        int step = (rep & KEY_RIGHT) ? CH_VIEW : 1, to = s_ch_sel;
        for (int s = 0; s < step; s++) { int nx = ch_next(to + 1, 1); if (nx < 0) break; to = nx; }
        if (to != s_ch_sel) { s_ch_sel = to; sfx(SFX_TICK); }
    }
    if (rep & (KEY_UP | KEY_LEFT)) {
        int step = (rep & KEY_LEFT) ? CH_VIEW : 1, to = s_ch_sel;
        for (int s = 0; s < step; s++) { int nx = ch_next(to - 1, -1); if (nx < 0) break; to = nx; }
        if (to != s_ch_sel) { s_ch_sel = to; sfx(SFX_TICK); }
        else if (s_ch_scroll > 0 && to == ch_next(0, 1)) s_ch_scroll = 0;   // show the first heading
    }
    if (k & KEY_A) { cheats_toggle(s_ch_sel); s_ch_ver++; sfx(SFX_TOGGLE); }
    if (k & KEY_X) {
        for (int i = 0; i < cheats_count(); i++) { const CheatRow *r = cheats_row(i); if (r && r->on) cheats_toggle(i); }
        s_ch_ver++;
        sfx(SFX_TOGGLE);
    }
    if (k & KEY_B) close_cheats();
}

// --- Adding DSi applications (Flipnote Studio, DSi Camera...) -----------------------------
#define AP_Y0   26
#define AP_RH   22
#define AP_VIEW 6

static void open_apps(void) {
    s_ap_n = 0;
    s_ap_sel = 0;
    s_ap_scroll = 0;
    s_ap_stage = 0;
    s_ap_ver++;
    s_scr = SCR_APPS;
    sfx(SFX_DECIDE);
}

// runs on the frame after "reading" is on screen: the NAND takes a few seconds the first time
static void apps_read_now(void) {
    int n = apps_scan(s_ap_list, APPS_MAX, s_cfg.language);
    s_ap_stage = 2;
    s_ap_ver++;
    if (n <= 0) {
        s_ap_n = 0;
        s_scr = SCR_ALL;
        toast(tr(n < 0 ? S_APPS_NAND_FAIL : S_APPS_NONE, L()));
        sfx(SFX_ERROR);
        return;
    }
    s_ap_n = n;
    for (int i = 0; i < n; i++) s_ap_on[i] = apps_has(s_ap_list[i].path);
}

static void close_apps(void) {
    for (int i = 0; i < s_ap_n; i++) {
        if (s_ap_on[i]) apps_add(&s_ap_list[i]);
        else apps_remove(s_ap_list[i].path);
    }
    apps_save();
    library_merge_apps();
    refresh_all();
    s_scr = SCR_ALL;
    sfx(SFX_BACK);
}

static void render_top_apps(void) {
    if (s_ap_stage == 1) apps_read_now();
    if (s_ap_stage == 2 && s_ap_n > 0) {
        hero(&g_top, -100 - s_ap_sel, &s_ap_list[s_ap_sel]);   // own tile-cache key per entry
        return;
    }
    hero_simple(&g_top, ICON_GRID_L, T.accent, tr(S_APPS_ADD, L()), tr(S_APPS_PICK_HINT, L()), NULL);
}

static void render_apps(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_GRID, 10, 4, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_APPS_ADD, L()), T.text, 200);
    if (s_ap_stage < 2) {
        ui_spinner(c, 128, 80, 12, s_frame, T.accent);
        gfx_text_align(c, FONT_BODY, 0, 104, 256, tr(S_APPS_READING, L()), T.text2, ALIGN_CENTER);
        if (s_ap_stage == 0) s_ap_stage = 1;          // read on the next frame
        return;
    }
    if (s_ap_sel < s_ap_scroll) s_ap_scroll = s_ap_sel;
    if (s_ap_sel >= s_ap_scroll + AP_VIEW) s_ap_scroll = s_ap_sel - AP_VIEW + 1;
    gfx_set_clip(c, 0, AP_Y0 - 2, 256, AP_VIEW * AP_RH + 2);
    for (int i = s_ap_scroll; i < s_ap_n && i < s_ap_scroll + AP_VIEW; i++) {
        int y = AP_Y0 + (i - s_ap_scroll) * AP_RH;
        bool sel = (i == s_ap_sel);
        gfx_round_rect(c, 8, y, 240, AP_RH - 2, 4, sel ? T.panel_hi : T.panel, 32);
        ui_thumb(c, -1, &s_ap_list[i], 12, y + 2, 16);
        gfx_text(c, FONT_BODY, 34, y + 3, s_ap_list[i].title, T.text, 180);
        int bx = 226;
        gfx_round_rect(c, bx, y + 4, 12, 12, 3, s_ap_on[i] ? T.accent : T.bg, 32);
        if (!s_ap_on[i]) gfx_round_rect(c, bx + 1, y + 5, 10, 10, 2, sel ? T.panel_hi : T.panel, 32);
        else gfx_icon(c, ICON_CHECK, bx - 2, y + 2, T.light ? C8(255, 255, 255) : C8(20, 20, 20));
        if (sel) ui_sel_frame(c, 10, y + 2, 236, AP_RH - 6, 2, s_frame);
    }
    gfx_reset_clip(c);
    Hint h[2] = { { BTN_B, tr(S_DONE, L()), KEY_B }, { BTN_A, tr(S_TOGGLE, L()), KEY_A } };
    s_hit_n = ui_hint_bar(c, h, 2, s_hits);
}

static void input_apps(u32 k, u32 rep) {
    if (s_ap_stage < 2) return;
    if ((rep & KEY_DOWN) && s_ap_sel < s_ap_n - 1) { s_ap_sel++; sfx(SFX_TICK); }
    if ((rep & KEY_UP) && s_ap_sel > 0) { s_ap_sel--; sfx(SFX_TICK); }
    if ((k & KEY_A) && s_ap_n) { s_ap_on[s_ap_sel] ^= 1; s_ap_ver++; sfx(s_ap_on[s_ap_sel] ? SFX_TOGGLE : SFX_BACK); }
    if (k & (KEY_B | KEY_START)) close_apps();
}

// --- Store (homebrew from Universal-DB, through DSplay Studio; see store.h / pad.h) --------
#define SO_TABS  4
#define SO_Y0    44
#define SO_RH    21
#define SO_VIEW  6
static const int k_so_cat[SO_TABS] = { STORE_CAT_ALL, STORE_CAT_GAME, STORE_CAT_UTIL, STORE_CAT_EMU };
static int  s_so_tab, s_so_sel, s_so_scroll, s_so_view[256], s_so_n;
static bool s_so_have, s_so_asked, s_so_rescan;
static char s_so_busy[40];        // slug being installed
static int  s_so_updating;        // frames left showing "updating the catalog"

static void store_tile(Canvas *c, int x, int y, int size) {
    if (T.light) gfx_shadow(c, x, y, size, size, 6, 3, 14);
    gfx_round_rect_vgrad(c, x, y, size, size, 6, C8(255, 150, 70), C8(230, 80, 60));
    gfx_icon_centered(c, size >= 60 ? ICON_BAG_L : ICON_BAG, x + size / 2, y + size / 2 - 7, C8(255, 255, 255));
    gfx_text_align(c, FONT_SMALL, x + 2, y + size - 18, size - 4, tr(S_STORE, L()), C8(255, 255, 255), ALIGN_CENTER);
}

static void store_rebuild(void) {
    s_so_n = store_view(k_so_cat[s_so_tab], s_so_view, 256);
    if (s_so_sel >= s_so_n) s_so_sel = s_so_n ? s_so_n - 1 : 0;
    s_store_ver++;
}

static void open_store(void) {
    sfx(SFX_DECIDE);
    s_so_have = store_load(s_drv);
    s_so_tab = s_so_sel = s_so_scroll = 0;
    s_so_asked = s_so_rescan = false;
    s_so_busy[0] = '\0';
    s_so_updating = 0;
    store_rebuild();
    pad_enter_store(s_drv, s_cfg.profile_name, s_cfg.language);
    s_scr = SCR_STORE;
}

static void close_store(void) {
    pad_leave();
    if (pad_take_rescan() || s_so_rescan) {      // programs arrived over Wi-Fi
        library_scan_begin(s_drv, false, NULL);
        covers_reset();
    }
    store_free();
    sfx(SFX_BACK);
    s_scr = SCR_HOME;
    s_force_redraw = true;
}

// every frame while the store is open: the connection, answers and arriving files
static void store_frame(const AppInput *in) {
    PadInput pi = { 0, 0, false, 0, 0 };
    (void)in;
    pad_frame(&pi);
    if (pad_connected() && !s_so_asked) {         // fresh catalog from Studio
        s_so_asked = pad_store_request(1, "");
        s_so_updating = 60 * 30;
        s_store_ver++;
    }
    if (!pad_connected()) s_so_asked = false;
    if (s_so_updating > 0) s_so_updating--;
    int op, st;
    char slug[40];
    if (pad_store_result(&op, &st, slug, sizeof(slug))) {
        if (op == 1) s_so_updating = 0;
        if (op == 2) s_so_busy[0] = '\0';
        if (st == STORE_RES_OK && op == 2) { toast(tr(S_STORE_DONE, L())); sfx(SFX_LAUNCH); }
        else if (st == STORE_RES_OFFLINE) { toast(tr(S_STORE_OFFLINE, L())); sfx(SFX_ERROR); }
        else if (st == STORE_RES_SPACE) { toast(tr(S_STORE_SPACE, L())); sfx(SFX_ERROR); }
        else if (st != STORE_RES_OK) { toast(tr(S_STORE_FAIL, L())); sfx(SFX_ERROR); }
        s_store_ver++;
    }
    if (pad_take_rescan()) {                       // new catalog or a program was installed
        s_so_rescan = true;
        char keep[40];
        const StoreEntry *cur = (s_so_n > 0) ? store_get(s_so_view[s_so_sel]) : NULL;
        snprintf(keep, sizeof(keep), "%s", cur ? cur->slug : "");
        s_so_have = store_load(s_drv);
        store_rebuild();
        for (int i = 0; i < s_so_n; i++) if (!strcmp(store_get(s_so_view[i])->slug, keep)) { s_so_sel = i; break; }
        s_so_updating = 0;
    }
}

static const char *store_tab_label(int t) {
    static const StrId ids[SO_TABS] = { S_STORE_POPULAR, S_STORE_GAMES, S_STORE_TOOLS, S_STORE_EMUS };
    return tr(ids[t], L());
}

static void fmt_size(uint32_t b, char *out, size_t len) {
    if (!b) snprintf(out, len, "—");
    else if (b >= 1024 * 1024) snprintf(out, len, "%u.%u MB", (unsigned)(b >> 20), (unsigned)((b & 0xFFFFF) * 10 >> 20));
    else snprintf(out, len, "%u KB", (unsigned)((b + 1023) >> 10));
}

// status of the connection, for both screens
static void store_status(char *out, size_t len, Color *col) {
    *col = T.text3;
    int ps = pad_status();
    if (s_so_busy[0] && xfer_name()[0]) { snprintf(out, len, "%d%%", xfer_percent()); *col = T.accent; }
    else if (s_so_busy[0]) { snprintf(out, len, "…"); *col = T.accent; }
    else if (ps == PAD_ST_CONNECTED) { snprintf(out, len, "%s", s_so_updating ? tr(S_STORE_UPDATING, L()) : "Studio ✓"); *col = s_so_updating ? T.accent : T.text2; }
    else if (ps == PAD_ST_WIFI_FAIL) snprintf(out, len, "Wi-Fi ✗");
    else snprintf(out, len, "%s", ps == PAD_ST_WIFI ? "Wi-Fi…" : "Studio…");
}

static void render_top_store(void) {
    Canvas *c = &g_top;
    const StoreEntry *e = (s_so_have && s_so_n > 0) ? store_get(s_so_view[s_so_sel]) : NULL;
    if (!e) {
        char line[96];
        if (pad_status() == PAD_ST_UNPAIRED) snprintf(line, sizeof(line), tr(S_STORE_PAIR, L()), (unsigned)pad_pair_code());
        else snprintf(line, sizeof(line), "%s", s_so_updating ? tr(S_STORE_UPDATING, L()) : tr(S_STORE_NO_CATALOG, L()));
        hero_simple(c, ICON_BAG_L, C8(255, 120, 60), tr(S_STORE, L()), line, tr(S_BETA, L()));
        return;
    }
    if (ui_wallpaper_present()) ui_wallpaper_draw(c);
    else {
        gfx_vgradient(c, 0, 0, 256, 110, gfx_mix(T.bg_top, (e->flags & 1) ? (Color)(e->icon[16 * 32 + 16] | 0x8000) : C8(255, 120, 60), T.light ? 40 : 55), T.bg_top);
        gfx_rect(c, 0, 110, 256, 82, T.bg_top);
    }
    ui_status_bar(c, &s_cfg);
    gfx_shadow(c, 14, 28, 68, 68, 8, 3, 18);
    gfx_round_rect(c, 14, 28, 68, 68, 8, T.tile, 32);
    if (e->flags & 1) gfx_blit_scaled(c, 16, 30, 64, 64, e->icon, 32, 32);
    else gfx_icon_centered(c, ICON_BAG_L, 48, 62, C8(255, 120, 60));
    int lines = gfx_text_wrap(c, FONT_TITLE, 92, 30, 156, 2, 18, e->title, T.text);
    int y = 30 + lines * 18 + 2;
    gfx_text(c, FONT_SMALL, 92, y, e->author, T.text2, 156);
    char meta[64], sz[16];
    fmt_size(e->size, sz, sizeof(sz));
    snprintf(meta, sizeof(meta), "%s%s%s  ·  ★ %u", e->version, e->version[0] ? "  ·  " : "", sz, (unsigned)e->stars);
    gfx_text(c, FONT_SMALL, 92, y + 14, meta, T.text3, 156);
    if (store_installed(e->slug)) {
        int tw = gfx_text_width(FONT_SMALL, tr(S_STORE_INSTALLED, L())) + 12;
        gfx_round_rect(c, 92, y + 30, tw, 14, 7, C8(40, 190, 100), 32);
        gfx_text(c, FONT_SMALL, 98, y + 31, tr(S_STORE_INSTALLED, L()), C8(255, 255, 255), 0);
    }
    gfx_text_wrap(c, FONT_BODY, 14, 112, 228, 5, 15, e->desc, T.text2);
}

static void render_store(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_BAG, 10, 4, T.text);
    int tw0 = gfx_text(c, FONT_BOLD, 30, 4, tr(S_STORE, L()), T.text, 80);
    gfx_round_rect(c, 30 + tw0 + 5, 6, 28, 11, 5, C8(255, 120, 60), 32);
    gfx_text(c, FONT_SMALL, 30 + tw0 + 8, 5, "BETA", C8(255, 255, 255), 0);
    char st[48];
    Color sc;
    store_status(st, sizeof(st), &sc);
    gfx_text_align(c, FONT_SMALL, 130, 6, 118, st, sc, ALIGN_RIGHT);
    if (s_so_busy[0] && xfer_name()[0]) {
        gfx_round_rect(c, 10, 19, 236, 3, 1, T.panel, 32);
        gfx_round_rect(c, 10, 19, 236 * xfer_percent() / 100, 3, 1, T.accent, 32);
    }
    // tabs
    int x = 10;
    gfx_icon(c, BTN_L, x, 27, T.text3);
    x += 16;
    for (int t = 0; t < SO_TABS; t++) {
        const char *lb = store_tab_label(t);
        int tw = gfx_text_width(FONT_SMALL, lb) + 12;
        if (t == s_so_tab) gfx_round_rect(c, x, 25, tw, 15, 7, T.accent, 32);
        gfx_text(c, FONT_SMALL, x + 6, 27, lb, t == s_so_tab ? C8(255, 255, 255) : T.text2, 0);
        x += tw + 3;
    }
    gfx_icon(c, BTN_R, x + 2, 27, T.text3);
    if (!s_so_have || s_so_n == 0) {
        gfx_text_wrap(c, FONT_BODY, 24, 84, 208, 4, 15, tr(S_STORE_NO_CATALOG, L()), T.text2);
    } else {
        if (s_so_sel < s_so_scroll) s_so_scroll = s_so_sel;
        if (s_so_sel >= s_so_scroll + SO_VIEW) s_so_scroll = s_so_sel - SO_VIEW + 1;
        gfx_set_clip(c, 0, SO_Y0 - 2, 256, SO_VIEW * SO_RH + 2);
        for (int i = s_so_scroll; i < s_so_n && i < s_so_scroll + SO_VIEW; i++) {
            const StoreEntry *e = store_get(s_so_view[i]);
            int y = SO_Y0 + (i - s_so_scroll) * SO_RH;
            bool sel = i == s_so_sel;
            gfx_round_rect(c, 8, y, 240, SO_RH - 2, 4, sel ? T.panel_hi : T.panel, 32);
            if (e->flags & 1) gfx_blit_scaled(c, 11, y + 1, 17, 17, e->icon, 32, 32);
            else gfx_icon(c, ICON_BAG, 12, y + 2, T.text3);
            gfx_text(c, FONT_BODY, 34, y + 3, e->title, T.text, 150);
            char right[24];
            Color rc = T.text3;
            if (!strcmp(e->slug, s_so_busy)) { snprintf(right, sizeof(right), "%d%%", xfer_name()[0] ? xfer_percent() : 0); rc = T.accent; }
            else if (store_installed(e->slug)) { snprintf(right, sizeof(right), "%s", tr(S_STORE_INSTALLED, L())); rc = C8(40, 190, 100); }
            else fmt_size(e->size, right, sizeof(right));
            gfx_text_align(c, FONT_SMALL, 180, y + 4, 62, right, rc, ALIGN_RIGHT);
            if (sel) ui_sel_frame(c, 10, y + 2, 236, SO_RH - 6, 2, s_frame);
        }
        gfx_reset_clip(c);
    }
    const StoreEntry *cur = (s_so_have && s_so_n) ? store_get(s_so_view[s_so_sel]) : NULL;
    Hint h[2] = { { BTN_B, tr(S_BACK, L()), KEY_B },
                  { BTN_A, tr(cur && store_installed(cur->slug) ? S_STORE_AGAIN : S_STORE_GET, L()), KEY_A } };
    s_hit_n = ui_hint_bar(c, h, cur ? 2 : 1, s_hits);
}

static void store_get_selected(void) {
    if (!s_so_have || !s_so_n) return;
    const StoreEntry *e = store_get(s_so_view[s_so_sel]);
    if (s_so_busy[0]) { sfx(SFX_ERROR); return; }
    if (!pad_store_request(2, e->slug)) { toast(tr(S_STORE_NEED_PC, L())); sfx(SFX_ERROR); return; }
    snprintf(s_so_busy, sizeof(s_so_busy), "%s", e->slug);
    char msg[96];
    snprintf(msg, sizeof(msg), tr(S_STORE_GETTING, L()), e->title);
    toast(msg);
    sfx(SFX_DECIDE);
    s_store_ver++;
}

static void input_store(u32 k, u32 rep) {
    if ((rep & KEY_DOWN) && s_so_sel < s_so_n - 1) { s_so_sel++; sfx(SFX_TICK); }
    if ((rep & KEY_UP) && s_so_sel > 0) { s_so_sel--; sfx(SFX_TICK); }
    if (k & (KEY_R | KEY_L | KEY_RIGHT | KEY_LEFT)) {
        int d = (k & (KEY_R | KEY_RIGHT)) ? 1 : -1;
        s_so_tab = (s_so_tab + SO_TABS + d) % SO_TABS;
        s_so_sel = s_so_scroll = 0;
        store_rebuild();
        sfx(SFX_PAGE);
    }
    if (k & KEY_A) store_get_selected();
    if (k & KEY_B) close_store();
}

static void store_touch(int x, int y) {
    if (y >= 24 && y < 41) {                        // tabs
        int tx = 26;
        for (int t = 0; t < SO_TABS; t++) {
            int tw = gfx_text_width(FONT_SMALL, store_tab_label(t)) + 12;
            if (x >= tx && x < tx + tw && t != s_so_tab) { s_so_tab = t; s_so_sel = s_so_scroll = 0; store_rebuild(); sfx(SFX_PAGE); return; }
            tx += tw + 3;
        }
        return;
    }
    int row = s_so_scroll + (y - SO_Y0) / SO_RH;
    if (y >= SO_Y0 && y < SO_Y0 + SO_VIEW * SO_RH && row < s_so_n) {
        if (row == s_so_sel) store_get_selected();
        else { s_so_sel = row; sfx(SFX_TICK); }
    }
}

// --- Nearby (StreetPass-like, see nearby.h) -----------------------------------------------
#define NB_Y0   26
#define NB_RH   22
#define NB_VIEW 6
static int  s_nb_sel, s_nb_scroll;
static char s_nb_greet[48];

static void nearby_tile(Canvas *c, int x, int y, int size) {
    if (T.light) gfx_shadow(c, x, y, size, size, 6, 3, 14);
    gfx_round_rect_vgrad(c, x, y, size, size, 6, C8(110, 140, 255), C8(70, 80, 220));
    gfx_icon_centered(c, size >= 60 ? ICON_USERS_L : ICON_USERS, x + size / 2, y + size / 2 - 7, C8(255, 255, 255));
    gfx_text_align(c, FONT_SMALL, x + 2, y + size - 18, size - 4, tr(S_NEARBY, L()), C8(255, 255, 255), ALIGN_CENTER);
}

static void greet_path(char *out, size_t len) { stats_path(out, len, "nearby_msg.txt"); }

// the card the others receive: name, color, picture, greeting and the most played game
static void nearby_make_me(void) {
    NearbyCard me;
    memset(&me, 0, sizeof(me));
    me.id = pad_console_id() * 4u + (uint32_t)(s_cur_profile & 3);
    if (!me.id) me.id = 1;
    snprintf(me.name, sizeof(me.name), "%s", s_cfg.profile_name[0] ? s_cfg.profile_name : "DSi");
    const ProfileInfo *pi = profiles_get(s_cur_profile);
    me.color = pi ? pi->color : 0;
    snprintf(me.greet, sizeof(me.greet), "%s", s_nb_greet);
    uint32_t best = 0, total = 0;
    const char *fav = NULL;
    for (int i = 0; i < s_pt.count; i++) {
        total += s_pt.entries[i].total_minutes;
        if (s_pt.entries[i].total_minutes > best) { best = s_pt.entries[i].total_minutes; fav = s_pt.entries[i].filename; }
    }
    me.minutes = total;
    me.games = (uint16_t)(s_pt.count > 65535 ? 65535 : s_pt.count);
    if (fav) {
        const char *title = fav;
        for (int i = 0; i < library_count(); i++) if (!strcmp(library_get(i)->filename, fav)) { title = library_get(i)->title; break; }
        snprintf(me.game, sizeof(me.game), "%s", title);
    }
    // 64 x 64 profile picture (Studio) -> 24 x 24
    char p[MAX_PATH_LEN];
    profiles_file(s_cur_profile, "avatar.bin", p, sizeof(p));
    FILE *f = fopen(p, "rb");
    if (f) {
        uint8_t h[8];
        static uint16_t px[64 * 64];
        if (fread(h, 1, 8, f) == 8 && !memcmp(h, "HAV1", 4) && h[4] == 64 && h[6] == 64 && fread(px, 2, 64 * 64, f) == 64 * 64) {
            for (int y = 0; y < NEARBY_AV; y++)
                for (int x = 0; x < NEARBY_AV; x++) me.avatar[y * NEARBY_AV + x] = px[(y * 64 / NEARBY_AV) * 64 + x * 64 / NEARBY_AV] | 0x8000;
            me.has_avatar = 1;
        }
        fclose(f);
    }
    nearby_set_me(&me);
}

static void nearby_save_greeting(const char *text) {
    snprintf(s_nb_greet, sizeof(s_nb_greet), "%s", text && text[0] ? text : tr(S_NEARBY_DEFAULT_GREET, L()));
    char p[MAX_PATH_LEN];
    greet_path(p, sizeof(p));
    FILE *f = fopen(p, "wb");
    if (f) { fputs(s_nb_greet, f); fclose(f); }
    nearby_make_me();
    s_nb_ver++;
}

static void open_nearby(void) {
    sfx(SFX_DECIDE);
    char p[MAX_PATH_LEN];
    greet_path(p, sizeof(p));
    snprintf(s_nb_greet, sizeof(s_nb_greet), "%s", tr(S_NEARBY_DEFAULT_GREET, L()));
    FILE *f = fopen(p, "rb");
    if (f) {
        size_t n = fread(s_nb_greet, 1, sizeof(s_nb_greet) - 1, f);
        s_nb_greet[n] = '\0';
        s_nb_greet[strcspn(s_nb_greet, "\r\n")] = '\0';
        fclose(f);
    }
    pad_enter_nearby(s_drv, s_cfg.profile_name, s_cfg.language);
    nearby_make_me();
    stats_path(p, sizeof(p), "nearby.bin");
    nearby_open(p);
    {   // the photo I share (Photos > Share with Nearby)
        char pp[MAX_PATH_LEN];
        profiles_file(s_cur_profile >= 0 ? s_cur_profile : 0, "nearby_photo.bin", pp, sizeof(pp));
        if (!s_nb_my_ph) s_nb_my_ph = malloc(NEARBY_PH_W * NEARBY_PH_H * sizeof(Color));
        if (!s_nb_ph) s_nb_ph = malloc(NEARBY_PH_W * NEARBY_PH_H * sizeof(Color));
        if (s_nb_my_ph && photo_read_raw(pp, "HFC1", s_nb_my_ph, NEARBY_PH_W, NEARBY_PH_H)) {
            uint32_t h = 2166136261u;
            for (int i = 0; i < NEARBY_PH_W * NEARBY_PH_H; i += 7) h = (h ^ s_nb_my_ph[i]) * 16777619u;
            nearby_set_photo(s_nb_my_ph, h);
        } else {
            nearby_set_photo(NULL, 0);
        }
    }
    s_nb_ph_id = 0;
    s_nb_sel = s_nb_scroll = 0;
    s_nb_ver++;
    s_scr = SCR_NEARBY;
}

static void close_nearby(void) {
    nearby_close();
    nearby_set_photo(NULL, 0);
    free(s_nb_my_ph); free(s_nb_ph);
    s_nb_my_ph = s_nb_ph = NULL;
    s_nb_ph_id = 0;
    pad_leave();
    sfx(SFX_BACK);
    s_scr = SCR_HOME;
    s_force_redraw = true;
}

// photos: the one I share, the selected friend's

static const Color *nearby_person_photo(const NearbyPerson *p) {
    if (!p) return NULL;
    if (!s_nb_ph) return NULL;
    if (s_nb_ph_id != p->c.id) {
        s_nb_ph_id = p->c.id;
        char path[MAX_PATH_LEN];
        s_nb_ph_ok = nearby_photo_path(p->c.id, path, sizeof(path)) && photo_read_raw(path, "HFC1", s_nb_ph, NEARBY_PH_W, NEARBY_PH_H);
    }
    return s_nb_ph_ok ? s_nb_ph : NULL;
}

static void nearby_save_photo(const NearbyPerson *p) {
    const Color *ph = nearby_person_photo(p);
    if (!ph) return;
    Color *big = malloc(256 * 192 * sizeof(Color));
    if (!big) { sfx(SFX_ERROR); return; }
    photo_sample(ph, NEARBY_PH_W, NEARBY_PH_H, 0, 0, NEARBY_PH_W, NEARBY_PH_H, big, 256, 192);
    char dir[MAX_PATH_LEN], path[MAX_PATH_LEN + 64], name[24];
    snprintf(dir, sizeof(dir), "%s/photos/Nearby", s_drv);
    make_dirs(dir);
    snprintf(name, sizeof(name), "%s", p->c.name);
    for (char *q = name; *q; q++) if (strchr("/\\:*?\"<>|", *q)) *q = '_';
    snprintf(path, sizeof(path), "%s/%s_%08x.png", dir, name, (unsigned)p->c.id);
    bool ok = photo_write_png(path, big, 256, 192);
    free(big);
    toast(tr(ok ? S_NB_PHOTO_SAVED : S_PH_FAIL, L()));
    sfx(ok ? SFX_DECIDE : SFX_ERROR);
    if (ok) { s_photos_exist = true; s_ph_scanned = false; }
}

static void nearby_app_frame(void) {
    PadInput pi = { 0, 0, false, 0, 0 };
    pad_frame(&pi);
    uint32_t ip, mask;
    if (pad_net_info(&ip, &mask)) {
        int before = nearby_count();
        nearby_frame(ip, mask);
        char name[24];
        if (nearby_take_met(name, sizeof(name))) {
            char msg[64];
            snprintf(msg, sizeof(msg), tr(S_NEARBY_MET, L()), name);
            toast(msg);
            sfx(SFX_LAUNCH);
            s_nb_sel = 0;
        }
        if (nearby_count() != before) s_nb_ver++;
        if (nearby_take_photo()) { s_nb_ph_id = 0; s_nb_ver++; }   // a friend's photo arrived
        if ((s_frame % 60) == 0) s_nb_ver++;         // "here now" fades after 10 s
    }
}

static void fmt_date(uint32_t ts, char *out, size_t len) {
    time_t t = (time_t)ts;
    struct tm *tm = localtime(&t);
    if (tm) snprintf(out, len, "%02d.%02d.%04d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
    else out[0] = '\0';
}

static void draw_person_pic(Canvas *c, const NearbyCard *p, int x, int y, int size) {
    uint16_t col;
    profile_color(p->color, &col, NULL, NULL, NULL);
    if (p->has_avatar) {
        gfx_round_rect(c, x - 1, y - 1, size + 2, size + 2, 4, col, 32);
        gfx_blit_scaled(c, x, y, size, size, p->avatar, NEARBY_AV, NEARBY_AV);
    } else {
        gfx_circle(c, x + size / 2, y + size / 2, size / 2, col, 32);
        char ini[8] = { 0 };
        size_t k = 1;
        while (p->name[k] && ((unsigned char)p->name[k] & 0xC0) == 0x80) k++;
        memcpy(ini, p->name, k < 7 ? k : 7);
        gfx_text_align(c, size >= 40 ? FONT_TITLE : FONT_BODY, x, y + size / 2 - (size >= 40 ? 9 : 7), size, ini, C8(255, 255, 255), ALIGN_CENTER);
    }
}

static void render_top_nearby(void) {
    Canvas *c = &g_top;
    const NearbyPerson *p = nearby_get(s_nb_sel);
    if (!p) {
        hero_simple(c, ICON_USERS_L, C8(90, 120, 250), tr(S_NEARBY, L()), tr(S_NEARBY_EMPTY, L()), NULL);
        return;
    }
    uint16_t col;
    profile_color(p->c.color, &col, NULL, NULL, NULL);
    const Color *ph = nearby_person_photo(p);
    if (ph) {                                  // the photo the friend shares
        photo_sample(ph, NEARBY_PH_W, NEARBY_PH_H, 0, 0, NEARBY_PH_W, NEARBY_PH_H, c->px, 256, 192);
        gfx_rect_a(c, 0, 0, 256, 192, (Color)0x8000, 16);
    } else if (ui_wallpaper_present()) ui_wallpaper_draw(c);
    else { gfx_vgradient(c, 0, 0, 256, 110, gfx_mix(T.bg_top, col, T.light ? 45 : 60), T.bg_top); gfx_rect(c, 0, 110, 256, 82, T.bg_top); }
    ui_status_bar(c, &s_cfg);
    draw_person_pic(c, &p->c, 16, 30, 56);
    gfx_text(c, FONT_TITLE, 84, 32, p->c.name, T.text, 160);
    char line[64];
    snprintf(line, sizeof(line), tr(S_NEARBY_TIMES, L()), (unsigned)p->times);
    gfx_text(c, FONT_SMALL, 84, 54, nearby_here(s_nb_sel) ? tr(S_NEARBY_HERE, L()) : line, nearby_here(s_nb_sel) ? C8(40, 200, 110) : T.text2, 160);
    char d[24];
    fmt_date(p->first_met, d, sizeof(d));
    snprintf(line, sizeof(line), tr(S_NEARBY_FIRST, L()), d);
    gfx_text(c, FONT_SMALL, 84, 68, line, T.text3, 160);
    // speech bubble with the greeting
    gfx_round_rect(c, 14, 96, 228, 44, 10, T.tile, 32);
    gfx_text_wrap(c, FONT_BODY, 24, 103, 210, 2, 15, p->c.greet[0] ? p->c.greet : "…", T.text);
    if (p->c.game[0]) {
        gfx_icon(c, ICON_STAR, 16, 150, T.accent);
        snprintf(line, sizeof(line), "%s: %s", tr(S_NEARBY_FAV, L()), p->c.game);
        gfx_text(c, FONT_SMALL, 36, 152, line, T.text2, 206);
    }
    char m[24];
    fmt_minutes(p->c.minutes, m, sizeof(m));
    snprintf(line, sizeof(line), "%s  ·  %s", m, tr(S_SOFTWARE_COUNT, L()));
    char line2[64];
    snprintf(line2, sizeof(line2), line, (int)p->c.games);
    gfx_icon(c, ICON_CLOCK, 18, 170, T.text3);
    gfx_text(c, FONT_SMALL, 36, 170, line2, T.text3, 206);
}

static void render_nearby(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_USERS, 10, 4, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_NEARBY, L()), T.text, 110);
    int ps = pad_status();
    char st[48];
    bool searching = ps != PAD_ST_WIFI && ps != PAD_ST_WIFI_FAIL;
    if (ps == PAD_ST_WIFI_FAIL) snprintf(st, sizeof(st), "Wi-Fi ✗");
    else if (!searching) snprintf(st, sizeof(st), "Wi-Fi…");
    else snprintf(st, sizeof(st), tr(S_NEARBY_PEOPLE, L()), nearby_count());
    gfx_text_align(c, FONT_SMALL, 130, 6, 100, st, T.text3, ALIGN_RIGHT);
    if (searching) ui_spinner(c, 240, 11, 5, s_frame, T.accent);
    int n = nearby_count();
    if (!n) {
        gfx_text_wrap(c, FONT_BODY, 24, 64, 208, 3, 15, searching ? tr(S_NEARBY_SEARCHING, L()) : tr(S_PAD_WIFI, L()), T.text2);
        gfx_round_rect(c, 14, 112, 228, 44, 10, T.panel, 32);
        gfx_text_wrap(c, FONT_SMALL, 22, 118, 212, 3, 12, s_nb_greet, T.text2);
    } else {
        if (s_nb_sel >= n) s_nb_sel = n - 1;
        if (s_nb_sel < s_nb_scroll) s_nb_scroll = s_nb_sel;
        if (s_nb_sel >= s_nb_scroll + NB_VIEW) s_nb_scroll = s_nb_sel - NB_VIEW + 1;
        gfx_set_clip(c, 0, NB_Y0 - 2, 256, NB_VIEW * NB_RH + 2);
        for (int i = s_nb_scroll; i < n && i < s_nb_scroll + NB_VIEW; i++) {
            const NearbyPerson *p = nearby_get(i);
            int y = NB_Y0 + (i - s_nb_scroll) * NB_RH;
            bool sel = i == s_nb_sel;
            gfx_round_rect(c, 8, y, 240, NB_RH - 2, 4, sel ? T.panel_hi : T.panel, 32);
            draw_person_pic(c, &p->c, 12, y + 2, 16);
            gfx_text(c, FONT_BODY, 34, y + 3, p->c.name, T.text, 120);
            char right[24];
            Color rc = T.text3;
            if (nearby_here(i)) { snprintf(right, sizeof(right), "%s", tr(S_NEARBY_HERE, L())); rc = C8(40, 200, 110); }
            else fmt_date(p->last_met, right, sizeof(right));
            gfx_text_align(c, FONT_SMALL, 150, y + 4, 92, right, rc, ALIGN_RIGHT);
            if (sel) ui_sel_frame(c, 10, y + 2, 236, NB_RH - 6, 2, s_frame);
        }
        gfx_reset_clip(c);
    }
    Hint h[4] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_Y, tr(S_DELETE, L()), KEY_Y }, { BTN_X, tr(S_NEARBY_GREET, L()), KEY_X },
                  { BTN_A, tr(S_PH_SAVE, L()), KEY_A } };
    if (n) s_hit_n = ui_hint_bar(c, h, nearby_person_photo(nearby_get(s_nb_sel)) ? 4 : 3, s_hits);
    else { Hint h2[2] = { h[0], h[2] }; s_hit_n = ui_hint_bar(c, h2, 2, s_hits); }
}

static void input_nearby(u32 k, u32 rep) {
    int n = nearby_count();
    if ((rep & KEY_DOWN) && s_nb_sel < n - 1) { s_nb_sel++; sfx(SFX_TICK); s_nb_ver++; }
    if ((rep & KEY_UP) && s_nb_sel > 0) { s_nb_sel--; sfx(SFX_TICK); s_nb_ver++; }
    if (k & KEY_X) { open_keyboard(KB_GREETING, s_nb_greet); return; }
    if ((k & KEY_Y) && n) { nearby_remove(s_nb_sel); sfx(SFX_BACK); s_nb_ver++; s_nb_ph_id = 0; }
    if ((k & KEY_A) && n) nearby_save_photo(nearby_get(s_nb_sel));
    if (k & KEY_B) close_nearby();
}

static void nearby_touch(int x, int y) {
    (void)x;
    int row = s_nb_scroll + (y - NB_Y0) / NB_RH;
    if (y >= NB_Y0 && y < NB_Y0 + NB_VIEW * NB_RH && row < nearby_count() && row != s_nb_sel) { s_nb_sel = row; sfx(SFX_TICK); s_nb_ver++; }
}

static void render_notes(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_NOTE, 10, 4, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_OPT_NOTES, L()), T.text, 200);
    gfx_round_rect(c, 8, 24, 240, 144, 6, T.panel, 32);
    gfx_set_clip(c, 8, 26, 240, 140);
    if (!s_notes[0]) {
        gfx_text_wrap(c, FONT_BODY, 18, 34, 220, 3, 15, tr(S_NOTES_EMPTY, L()), T.text2);
    } else {
        // render paragraphs line by line with scrolling
        int y = 30 - s_notes_scroll * 15;
        const char *p = s_notes;
        char line[200];
        while (*p && y < 168) {
            const char *nl = strchr(p, '\n');
            size_t n = nl ? (size_t)(nl - p) : strlen(p);
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, p, n);
            line[n] = '\0';
            int lines = gfx_text_wrap(c, FONT_BODY, 16, y, 224, 8, 15, line, T.text);
            y += (lines ? lines : 1) * 15;
            p = nl ? nl + 1 : p + strlen(p);
        }
    }
    gfx_reset_clip(c);
    Hint h[2] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_X, tr(S_OPT_NOTES, L()), KEY_X } };
    s_hit_n = ui_hint_bar(c, h, 2, s_hits);
}

static void render_fatal(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_text_wrap(c, FONT_BODY, 20, 70, 216, 4, 15, tr(S_SD_ERROR_HINT, L()), T.text2);
}

static const char *opt_label(int row, char *buf, size_t len) {
    const GameItem *it = item_for(s_opt_lib);
    switch (row) {
        case OPT_START: return tr(S_START, L());
        case OPT_FAV: return tr((it && (it->flags & ITEM_FLAG_FAVORITE)) ? S_OPT_FAV_REMOVE : S_OPT_FAV_ADD, L());
        case OPT_SLOT: snprintf(buf, len, "%s", tr(S_OPT_SAVE_SLOT, L())); return buf;
        case OPT_NOTES: return tr(S_OPT_NOTES, L());
        case OPT_RESTORE: return tr(S_OPT_RESTORE, L());
        case OPT_HIDE: return tr((it && (it->flags & ITEM_FLAG_HIDDEN)) ? S_OPT_UNHIDE : S_OPT_HIDE, L());
        case OPT_DELETE: return tr(S_OPT_DELETE, L());
        case OPT_FOLDER: return tr(S_OPT_FOLDER, L());
        case OPT_CARDSAVE: return tr(S_OPT_CARDSAVE, L());
        case OPT_GAMECFG: return tr(S_OPT_GAMECFG, L());
        case OPT_CHEATS: return tr(S_OPT_CHEATS, L());
        case OPT_APP_REMOVE: return tr(S_OPT_APP_REMOVE, L());
    }
    return "";
}

static const IconId k_opt_icons[] = { ICON_PLAY, ICON_HEART, ICON_SAVE, ICON_NOTE, ICON_RESTORE, ICON_EYE_OFF, ICON_TRASH, ICON_FOLDER, ICON_SAVE, ICON_GEAR, ICON_BOLT, ICON_TRASH };
static int opt_rh(void) { return s_opt_n > 7 ? 15 : 18; }
static int opt_vis(void) { return s_opt_n < OPT_VIS ? s_opt_n : OPT_VIS; }
static void opt_fix_scroll(void) {
    if (s_opt_sel < s_opt_scroll) s_opt_scroll = s_opt_sel;
    if (s_opt_sel >= s_opt_scroll + opt_vis()) s_opt_scroll = s_opt_sel - opt_vis() + 1;
    if (s_opt_scroll < 0) s_opt_scroll = 0;
}   // 8+ rows must still fit above the hint bar

static void render_options(void) {
    Canvas *c = &g_bot;
    ui_scrim(c, 16);
    const GameItem *it = item_for(s_opt_lib);
    int rh = opt_rh();
    opt_fix_scroll();
    int h = 30 + opt_vis() * rh + 6;
    int y0 = (HINT_BAR_Y - h) / 2 + 2;
    if (y0 < 4) y0 = 4;
    gfx_shadow(c, 16, y0, 224, h, 8, 4, 30);
    gfx_round_rect(c, 16, y0, 224, h, 8, T.panel, 32);
    if (it) {
        ui_thumb(c, s_opt_lib, it, 24, y0 + 6, 18);
        gfx_text(c, FONT_BOLD, 48, y0 + 8, it->title, T.text, 184);
    }
    gfx_rect(c, 24, y0 + 28, 208, 1, T.divider);
    if (s_opt_scroll > 0) gfx_text(c, FONT_SMALL, 226, y0 + 31, "▲", T.text3, 0);
    if (s_opt_scroll + opt_vis() < s_opt_n) gfx_text(c, FONT_SMALL, 226, y0 + 31 + opt_vis() * rh - 11, "▼", T.text3, 0);
    for (int i = s_opt_scroll; i < s_opt_n && i < s_opt_scroll + opt_vis(); i++) {
        int y = y0 + 31 + (i - s_opt_scroll) * rh;
        int row = s_opt_rows[i];
        bool sel = (i == s_opt_sel);
        if (sel) gfx_round_rect(c, 22, y, 212, rh - 1, 4, T.panel_hi, 32);
        Color ic = (row == OPT_DELETE) ? T.danger : (sel ? T.accent : T.text2);
        gfx_icon(c, k_opt_icons[row], 28, y + 1, ic);
        char buf[48];
        gfx_text(c, FONT_BODY, 50, y + 2, opt_label(row, buf, sizeof(buf)), row == OPT_DELETE ? T.danger : T.text, 140);
        if (row == OPT_SLOT) {
            char v[8];
            snprintf(v, sizeof(v), "%d", s_cfg.save_slot + 1);
            gfx_icon(c, ICON_CHEV_L, 196, y + 3, T.text3);
            gfx_text_align(c, FONT_BODY, 206, y + 2, 14, v, T.sel_text, ALIGN_CENTER);
            gfx_icon(c, ICON_CHEV_R, 218, y + 3, T.text3);
        }
        if (sel) ui_sel_frame(c, 22 + 3, y + 3, 212 - 6, rh - 7, 2, s_frame);
    }
    Hint hh[2] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_A, tr(S_OK, L()), KEY_A } };
    s_hit_n = ui_hint_bar(c, hh, 2, s_hits);
}

static void render_dialog(void) {
    Canvas *c = &g_bot;
    ui_scrim(c, 18);
    int x = 18, y = 30, w = 220, h = 132;
    gfx_shadow(c, x, y, w, h, 10, 5, 34);
    gfx_round_rect(c, x, y, w, h, 10, T.panel, 32);
    int ty = y + 10;
    if (s_dlg_icon < ICON_COUNT) {
        int isz = gfx_icon_size(s_dlg_icon);
        gfx_icon(c, s_dlg_icon, x + (w - isz) / 2, ty, s_dlg_kind != DLG_INFO ? T.danger : C8(255, 180, 40));
        ty += isz + 4;
    }
    gfx_text_align(c, FONT_BOLD, x + 8, ty, w - 16, s_dlg_title, T.text, ALIGN_CENTER);
    ty += 18;
    gfx_text_wrap(c, FONT_SMALL, x + 14, ty, w - 28, 4, 13, s_dlg_msg, T.text2);
    int by = y + h - 28;
    gfx_rect(c, x, by, w, 1, T.divider);
    if (s_dlg_nbtn == 2) {
        gfx_rect(c, x + w / 2, by, 1, 28, T.divider);
        bool del = s_dlg_kind != DLG_OLDMENU && s_dlg_kind != DLG_WIFI;
        const char *b0 = tr(s_dlg_kind == DLG_WPRESET ? S_WPRESET_BTN : del ? S_DELETE : s_dlg_kind == DLG_WIFI ? S_WIFI_RETRY : S_OK, L());
        const char *b1 = tr(s_dlg_kind == DLG_WIFI ? S_WIFI_SETTINGS : S_CANCEL, L());
        gfx_text_align(c, FONT_BODY, x, by + 7, w / 2, b0, s_dlg_sel == 0 ? (del ? T.danger : T.sel_text) : T.text2, ALIGN_CENTER);
        gfx_text_align(c, FONT_BODY, x + w / 2, by + 7, w / 2, b1, s_dlg_sel == 1 ? T.sel_text : T.text2, ALIGN_CENTER);
        ui_sel_frame(c, x + (s_dlg_sel ? w / 2 : 0) + 6, by + 5, w / 2 - 12, 18, 3, s_frame);
    } else {
        gfx_text_align(c, FONT_BODY, x, by + 7, w, tr(S_OK, L()), T.sel_text, ALIGN_CENTER);
        ui_sel_frame(c, x + 40, by + 5, w - 80, 18, 3, s_frame);
    }
    Hint hh[1] = { { BTN_A, tr(S_OK, L()), KEY_A } };
    s_hit_n = ui_hint_bar(c, hh, 1, s_hits);
}

// Paints the selection frames of the screen underneath into it (static), so a dialog or
// the launch scrim covers them instead of the animated frames floating on top
static void flatten_bottom_rings(void) {
    for (int i = 0; i < ui_rings_bot_n; i++) ui_ring_draw(&ui_rings_bot[i], 0);
    ui_rings_reset(&g_bot);
}

static void login_tile_pos(int i, int *cx, int *cy) {
    int n = login_items();
    *cx = 128 - (n - 1) * 30 + i * 60;
    *cy = 84;
}

static void render_login(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_text_align(c, FONT_TITLE, 0, 18, 256, tr(S_WHO_PLAYS, L()), T.text, ALIGN_CENTER);
    int n = login_items();
    for (int i = 0; i < n; i++) {
        int cx, cy;
        login_tile_pos(i, &cx, &cy);
        int slot = login_slot_at(i);
        const ProfileInfo *pi = profiles_get(slot);
        bool sel = i == s_login_sel;
        if (pi) {
            Color col = profile_accent(slot);
            gfx_shadow(c, cx - 24, cy - 24, 48, 48, 24, 3, 14);
            ui_avatar_slot(c, slot, cx, cy, 24, pi->name, col);
            if (pi->pin_len) {
                gfx_circle(c, cx + 18, cy + 18, 8, T.panel, 32);
                gfx_icon_centered(c, ICON_LOCK_S, cx + 18, cy + 18, T.text2);
            }
            gfx_text_align(c, FONT_SMALL, cx - 30, cy + 32, 60, pi->name, sel ? T.sel_text : T.text2, ALIGN_CENTER);
        } else {
            gfx_circle(c, cx, cy, 24, T.tile, 32);
            gfx_icon_centered(c, ICON_PLUS_L, cx, cy, T.text2);
            gfx_text_align(c, FONT_SMALL, cx - 30, cy + 32, 60, tr(S_NEW_PROFILE, L()), sel ? T.sel_text : T.text2, ALIGN_CENTER);
        }
        if (sel) ui_sel_frame(c, cx - 28, cy - 28, 56, 56, 28, s_frame);
    }
    Hint h[1] = { { BTN_A, tr(S_OK, L()), KEY_A } };
    s_hit_n = ui_hint_bar(c, h, 1, s_hits);
}

static void render_stats(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_STAR, 10, 4, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_STATS_TOP, L()), T.text, 210);
    if (!s_st_top_n) {
        gfx_text_align(c, FONT_BODY, 0, 70, 256, tr(S_STATS_EMPTY, L()), T.text2, ALIGN_CENTER);
    }
    for (int i = 0; i < s_st_top_n; i++) {
        const PlaytimeEntry *e = &s_pt.entries[s_st_top[i]];
        int y = 24 + i * 22;
        gfx_round_rect(c, 8, y, 240, 20, 4, T.panel, 32);
        gfx_text(c, FONT_SMALL, 14, y + 4, (const char[]){ (char)('1' + i), 0 }, T.text3, 0);
        int li = s_st_top_lib[i];
        const GameItem *it = li >= 0 ? library_get(li) : NULL;
        if (it) ui_thumb(c, li, it, 26, y + 1, 18);
        gfx_text(c, FONT_SMALL, 50, y + 4, it ? it->title : e->filename, T.text, 140);
        char m[24];
        fmt_minutes(e->total_minutes, m, sizeof(m));
        gfx_text_align(c, FONT_SMALL, 180, y + 4, 62, m, T.sel_text, ALIGN_RIGHT);
    }
    // share of play time per system
    if (s_st_sys_n) {
        gfx_text(c, FONT_SMALL, 10, 136, tr(S_STATS_SYSTEMS, L()), T.text2, 120);
        uint32_t sum = 0;
        for (int i = 0; i < s_st_sys_n; i++) sum += s_st_sys_min[i];
        int x = 10, wtot = 236;
        for (int i = 0; i < s_st_sys_n; i++) {
            int w = (int)(s_st_sys_min[i] * (uint32_t)wtot / (sum ? sum : 1));
            if (i == s_st_sys_n - 1) w = 10 + wtot - x;
            if (w <= 0) continue;
            Color col = ui_system_color(s_st_sys[i], false);
            gfx_rect(c, x, 152, w, 12, col);
            const char *nm = romdb_system_short(s_st_sys[i]);
            if (gfx_text_width(FONT_SMALL, nm) + 4 < w) gfx_text_align(c, FONT_SMALL, x, 152, w, nm, C8(255, 255, 255), ALIGN_CENTER);
            x += w;
        }
    }
    Hint h[1] = { { BTN_B, tr(S_BACK, L()), KEY_B } };
    s_hit_n = ui_hint_bar(c, h, 1, s_hits);
}

static void render_bottom(void) {
    if (s_ss_on) { render_bottom_ss(); return; }
    if (s_launching) {
        // keep the screen the game was picked from, dimmed, with a status pill
        switch (s_scr) {
            case SCR_ALL: render_all(); break;
            case SCR_KEYBOARD: render_keyboard(); break;
            default: render_home(); break;
        }
        flatten_bottom_rings();
        ui_scrim(&g_bot, 22);
        const char *msg = tr(S_LAUNCHING, L());
        int tw = gfx_text_width(FONT_BOLD, msg) + 28;
        gfx_round_rect(&g_bot, (256 - tw) / 2, 84, tw, 24, 12, T.light ? C8(45, 45, 45) : C8(235, 235, 235), 32);
        gfx_text_align(&g_bot, FONT_BOLD, 0, 89, 256, msg, T.light ? C8(255, 255, 255) : C8(25, 25, 25), ALIGN_CENTER);
        return;
    }
    switch (s_scr) {
        case SCR_HOME: render_home(); break;
        case SCR_ALL: render_all(); break;
        case SCR_KEYBOARD: render_keyboard(); break;
        case SCR_SETTINGS: render_settings(); break;
        case SCR_NOTES: render_notes(); break;
        case SCR_FATAL: render_fatal(); break;
        case SCR_LOGIN: render_login(); break;
        case SCR_STATS: render_stats(); break;
        case SCR_TOUCHCAL: render_touchcal(); break;
        case SCR_FOLDERS: render_folders(); break;
        case SCR_HOMEEDIT: render_homeedit(); break;
        case SCR_PHOTOS: render_photos(); break;
        case SCR_CHEATS: render_cheats(); break;
        case SCR_APPS: render_apps(); break;
        case SCR_STORE: render_store(); break;
        case SCR_NEARBY: render_nearby(); break;
        case SCR_PAD: pad_render_bottom(&g_bot); break;
        case SCR_WIFI: render_wifi(); break;
    }
    if (s_ovl != OVL_NONE) flatten_bottom_rings();
    if (s_ovl == OVL_OPTIONS) render_options();
    else if (s_ovl == OVL_DIALOG) render_dialog();
    else if (s_ovl == OVL_PIN) render_pin();
    else if (s_ovl == OVL_MENU) render_menu();
    if (s_toast_t > 0) {
        int tw = gfx_text_width(FONT_BODY, s_toast) + 24;
        int a = s_toast_t > 16 ? 32 : s_toast_t * 2;
        gfx_round_rect(&g_bot, (256 - tw) / 2, 140, tw, 22, 11, T.light ? C8(45, 45, 45) : C8(230, 230, 230), a);
        gfx_text_align(&g_bot, FONT_BODY, 0, 144, 256, s_toast, T.light ? C8(255, 255, 255) : C8(30, 30, 30), ALIGN_CENTER);
    }
}

// ---------------------------------------------------------------------------
// Settings > Wi-Fi: networks nearby, the password with the keyboard, a test connection.
// The chosen network goes to _hyper/wifi.txt (the controller, Store and Nearby try it first).
// ---------------------------------------------------------------------------
enum { WF_SCAN, WF_LIST, WF_TEST };
#define WIFI_MAX 16
static WifiNet s_wf_net[WIFI_MAX];
static int s_wf_n, s_wf_sel, s_wf_scroll, s_wf_state, s_wf_ver;
static bool s_wf_radio;

static void wifi_rescan(void) {
    s_wf_state = WF_SCAN;
    s_wf_n = 0;
    s_wf_ver++;
}

static void open_wifi(void) {
    s_wf_radio = wifi_scan_begin(s_drv);
    if (!s_wf_radio) { show_dialog(DLG_INFO, ICON_WARNING_L, tr(S_SET_WIFI, L()), tr(S_WIFI_NORADIO, L())); return; }
    s_wf_sel = s_wf_scroll = 0;
    wifi_rescan();
    s_scr = SCR_WIFI;
    sfx(SFX_DECIDE);
}

static void close_wifi(void) {
    if (s_wf_state == WF_TEST) { wifi_keep_old(true); s_wf_state = WF_LIST; }   // left before the answer
    wifi_off();
    s_scr = SCR_SETTINGS;
    sfx(SFX_BACK);
}

// the network saved before the test comes back when the new one does not connect
static void wifi_keep_old(bool restore) {
    char p[MAX_PATH_LEN], b[MAX_PATH_LEN];
    snprintf(p, sizeof(p), "%s/_hyper/wifi.txt", s_drv);
    snprintf(b, sizeof(b), "%s/_hyper/wifi.txt.old", s_drv);
    if (!restore) {
        remove(b);
        if (file_exists(p)) copy_file(p, b);
        return;
    }
    if (file_exists(b)) { copy_file(b, p); remove(b); }
    else wifi_forget(s_drv);
}

static void wifi_test(const char *ssid, const char *pass) {
    wifi_keep_old(false);
    if (!wifi_save(s_drv, ssid, pass)) { sfx(SFX_ERROR); return; }
    snprintf(s_wf_ssid, sizeof(s_wf_ssid), "%s", ssid);
    s_wf_state = WF_TEST;
    s_wf_ver++;
    wifi_test_begin();
}

static void wifi_screen_frame(void) {
    if (s_wf_state == WF_SCAN) {
        int n = wifi_scan_poll(s_wf_net, WIFI_MAX);
        if (n >= 0) { s_wf_n = n; s_wf_state = WF_LIST; if (s_wf_sel >= n) s_wf_sel = 0; s_wf_ver++; }
    } else if (s_wf_state == WF_TEST) {
        int r = wifi_test_poll();
        if (r != 0) {
            s_wf_state = WF_LIST;
            s_wf_ver++;
            char b[MAX_PATH_LEN];
            snprintf(b, sizeof(b), "%s/_hyper/wifi.txt.old", s_drv);
            if (r > 0) { remove(b); toast(tr(S_WIFI_OK, L())); sfx(SFX_DECIDE); }
            else { wifi_keep_old(true); show_dialog(DLG_INFO, ICON_WARNING_L, s_wf_ssid, tr(S_WIFI_BADPASS, L())); }
            wifi_off();
            wifi_scan_begin(s_drv);    // ready for another try
        }
    }
}

static void wifi_pick(int i) {
    if (i < 0 || i >= s_wf_n) return;
    const WifiNet *w = &s_wf_net[i];
    if (w->wep_only) { toast(tr(S_WIFI_WEP, L())); sfx(SFX_ERROR); return; }
    snprintf(s_wf_ssid, sizeof(s_wf_ssid), "%s", w->ssid);
    if (!w->secure) { wifi_test(w->ssid, ""); return; }
    s_kb_caps = false;
    open_keyboard(KB_WIFIPASS, "");
}

static void render_top_wifi(void) {
    Canvas *c = &g_top;
    gfx_vgradient(c, 0, 0, 256, 120, gfx_mix(T.bg_top, T.accent, 40), T.bg_top);
    gfx_rect(c, 0, 120, 256, 72, T.bg_top);
    ui_status_bar(c, &s_cfg);
    gfx_circle(c, 40, 56, 22, T.tile, 32);
    gfx_icon_centered(c, ICON_GLOBE, 40, 56, T.accent);
    char hd[64];
    snprintf(hd, sizeof(hd), "%s  ·  %s", tr(S_SETTINGS, L()), tr(S_ST_SYSTEM, L()));
    gfx_text(c, FONT_SMALL, 72, 38, hd, T.text2, 170);
    gfx_text(c, FONT_TITLE, 72, 52, tr(S_SET_WIFI, L()), T.text, 172);
    char saved[40];
    if (wifi_saved(s_drv, saved, sizeof(saved))) {
        gfx_text(c, FONT_SMALL, 20, 96, tr(S_WIFI_SAVED, L()), T.text2, 216);
        gfx_icon(c, ICON_CHECK, 20, 112, T.ok);
        gfx_text(c, FONT_BOLD, 40, 112, saved, T.text, 196);
    } else {
        gfx_text_wrap(c, FONT_BODY, 20, 96, 216, 3, 15, tr(S_WIFI_NOSAVED, L()), T.text2);
    }
    if (s_wf_state == WF_TEST) {
        char l[80];
        snprintf(l, sizeof(l), "%s  %s", tr(S_WIFI_CONNECTING, L()), s_wf_ssid);
        gfx_text(c, FONT_BODY, 20, 150, l, T.accent, 216);
    }
}

// signal: 1-4 bars from the RSSI the radio reports
static void wifi_bars(Canvas *c, int x, int y, int rssi, Color col) {
    int lv = rssi >= 170 ? 4 : rssi >= 130 ? 3 : rssi >= 90 ? 2 : 1;
    for (int b = 0; b < 4; b++) {
        int h = 3 + b * 3;
        gfx_rect_a(c, x + b * 4, y + 12 - h, 3, h, col, b < lv ? 32 : 9);
    }
}

static void render_wifi(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_GLOBE, 10, 4, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_SET_WIFI, L()), T.text, 200);
    if (s_wf_state == WF_SCAN || s_wf_state == WF_TEST) {
        ui_spinner(c, 128, 80, 12, s_frame, T.accent);
        char l[80];
        if (s_wf_state == WF_TEST) snprintf(l, sizeof(l), "%s  %s", tr(S_WIFI_CONNECTING, L()), s_wf_ssid);
        else snprintf(l, sizeof(l), "%s", tr(S_WIFI_SCANNING, L()));
        gfx_text_align(c, FONT_BODY, 0, 104, 256, l, T.text2, ALIGN_CENTER);
        Hint h[1] = { { BTN_B, tr(S_BACK, L()), KEY_B } };
        s_hit_n = ui_hint_bar(c, h, 1, s_hits);
        return;
    }
    if (s_wf_n == 0) {
        gfx_text_wrap(c, FONT_BODY, 24, 80, 208, 3, 15, tr(S_WIFI_NONE, L()), T.text2);
    }
    int rh = 26, y0 = 26, view = 5;
    if (s_wf_sel < s_wf_scroll) s_wf_scroll = s_wf_sel;
    if (s_wf_sel >= s_wf_scroll + view) s_wf_scroll = s_wf_sel - view + 1;
    char saved[40] = "";
    wifi_saved(s_drv, saved, sizeof(saved));
    gfx_set_clip(c, 0, y0 - 4, 256, HINT_BAR_Y - y0 + 2);
    for (int i = s_wf_scroll; i < s_wf_n && i < s_wf_scroll + view + 1; i++) {
        int y = y0 + (i - s_wf_scroll) * rh;
        bool sel = i == s_wf_sel;
        const WifiNet *w = &s_wf_net[i];
        gfx_round_rect(c, 8, y, 240, rh - 3, 4, sel ? T.panel_hi : T.panel, 32);
        if (pressed_at(8, y, 240, rh - 3)) gfx_round_rect(c, 8, y, 240, rh - 3, 4, C8(0, 0, 0), 7);
        wifi_bars(c, 16, y + 5, w->rssi, sel ? T.accent : T.text2);
        bool cur = saved[0] && !strcmp(saved, w->ssid);
        gfx_text(c, FONT_BODY, 40, y + 4, w->ssid, w->wep_only ? T.text3 : T.text, cur ? 150 : 170);
        if (cur) gfx_icon(c, ICON_CHECK, 196, y + 3, T.ok);
        if (w->secure) gfx_icon(c, ICON_LOCK_S, 222, y + 4, T.text2);
        if (sel) ui_sel_frame(c, 8 + 3, y + 3, 240 - 6, rh - 9, 2, s_frame);
    }
    gfx_reset_clip(c);
    Hint h[4] = {
        { BTN_B, tr(S_BACK, L()), KEY_B },
        { BTN_Y, tr(S_WIFI_RESCAN, L()), KEY_Y },
        { BTN_X, tr(S_WIFI_FORGET, L()), KEY_X },
        { BTN_A, tr(S_WIFI_CONNECT, L()), KEY_A },
    };
    if (!saved[0]) { h[2] = h[3]; s_hit_n = ui_hint_bar(c, h, s_wf_n ? 3 : 2, s_hits); }
    else { if (!s_wf_n) h[3] = h[2]; s_hit_n = ui_hint_bar(c, h, s_wf_n ? 4 : 3, s_hits); }
}

static void input_wifi(u32 k, u32 rep) {
    if (k & KEY_B) { close_wifi(); return; }
    if (s_wf_state != WF_LIST) return;
    if ((rep & KEY_DOWN) && s_wf_sel < s_wf_n - 1) { s_wf_sel++; sfx(SFX_TICK); }
    if ((rep & KEY_UP) && s_wf_sel > 0) { s_wf_sel--; sfx(SFX_TICK); }
    if (k & KEY_Y) { wifi_scan_begin(s_drv); wifi_rescan(); sfx(SFX_DECIDE); }
    if ((k & KEY_X) && wifi_saved(s_drv, NULL, 0)) { wifi_forget(s_drv); toast(tr(S_WIFI_FORGOTTEN, L())); sfx(SFX_BACK); s_wf_ver++; }
    if ((k & KEY_A) && s_wf_n) { sfx(SFX_DECIDE); wifi_pick(s_wf_sel); }
}

static void tap_wifi(int x, int y) {
    (void)x;
    if (s_wf_state != WF_LIST || y < 26 || y >= HINT_BAR_Y) return;
    int i = s_wf_scroll + (y - 26) / 26;
    if (i < 0 || i >= s_wf_n) return;
    if (i == s_wf_sel) { sfx(SFX_DECIDE); wifi_pick(i); }
    else { s_wf_sel = i; sfx(SFX_TICK); }
}

// ---------------------------------------------------------------------------
// Folders (collections)
// ---------------------------------------------------------------------------
static Color coll_rgb(int i) {
    uint16_t a;
    profile_color(coll_color(i), &a, NULL, NULL, NULL);
    return a;
}

// a folder drawn as a tile: colored body with a tab, the folder name at the bottom
static void folder_tile(Canvas *c, int x, int y, int size, Color col, const char *name, int count, bool big, const Color *pic) {
    int tab_w = size * 2 / 5, tab_h = size / 8 + 2;
    gfx_round_rect(c, x + 2, y, tab_w, tab_h + 4, 3, gfx_darken(col, 6), 32);
    if (pic) {
        // the folder's picture (Photos > Set as folder picture), framed in its colour
        gfx_round_rect(c, x, y + tab_h, size, size - tab_h, 5, gfx_darken(col, 2), 32);
        gfx_blit_scaled_round(c, x + 2, y + tab_h + 2, size - 4, size - tab_h - 4, pic, 64, 64, 4);
        gfx_rect_a(c, x + 2, y + size - (big ? 30 : 18), size - 4, big ? 28 : 16, (Color)0x8000, 14);
        if (big) gfx_rect_a(c, x + 2, y + tab_h + 2, size - 4, size / 2, (Color)0x8000, 6);
    } else {
        gfx_round_rect_vgrad(c, x, y + tab_h, size, size - tab_h, 5, gfx_lighten(col, 4), gfx_darken(col, 6));
    }
    gfx_rect_a(c, x + 3, y + tab_h + 2, size - 6, 1, C8(255, 255, 255), 10);
    char n[12];
    snprintf(n, sizeof(n), "%d", count);
    if (big) {
        gfx_text_align(c, FONT_HUGE, x, y + size / 2 - 10, size, n, C8(255, 255, 255), ALIGN_CENTER);
    } else {
        gfx_text_align(c, FONT_BOLD, x, y + tab_h + 6, size, n, C8(255, 255, 255), ALIGN_CENTER);
        gfx_text_align(c, FONT_SMALL, x + 2, y + size - 14, size - 4, name, C8(255, 255, 255), ALIGN_CENTER);
    }
}

static int fold_items(void) { return coll_count() + (coll_count() < MAX_COLLECTIONS ? 1 : 0); }

static void open_folders(void) {
    s_scr = SCR_FOLDERS;
    if (s_fold_sel >= fold_items()) s_fold_sel = 0;
    sfx(SFX_DECIDE);
}

static bool s_menu_dsw;   // MENU_GAMECFG is for a DSiWare title
static void open_menu(MenuKind kind, int target) {
    s_menu_kind = kind;
    s_menu_target = target;
    s_menu_sel = 0;
    s_menu_scroll = 0;
    switch (kind) {
        case MENU_FOLDER_OPTS: s_menu_n = 4; break;
        case MENU_COLOR: s_menu_n = PROFILE_COLORS; s_menu_sel = coll_color(target); break;
        case MENU_ADD_TO: s_menu_n = coll_count(); break;
        case MENU_GAMECFG: {
            const GameItem *it = library_get(target);
            s_menu_dsw = it && it->type == TYPE_NDS && launcher_is_dsiware(it->path, NULL, NULL);
            s_menu_n = s_menu_dsw ? 2 : 4;    // DSiWare: boot method + reset
            break;
        }
        case MENU_POWER: s_menu_n = power_off_supported() ? 3 : 2; break;
        case MENU_PHOTO: s_menu_n = s_phm_n; break;
        case MENU_PICK_GAME: s_menu_n = s_pick_n; break;
        case MENU_PICK_FOLDER: s_menu_n = coll_count(); break;
    }
    s_ovl = OVL_MENU;
    sfx(SFX_MODAL);
}

static void start_pick(int coll) {
    s_pick_coll = coll;
    build_tabs();
    s_tab = 0;
    build_view();
    s_grid_sel = 0;
    s_grid_scroll = s_grid_scroll_target = 0;
    s_scr = SCR_ALL;
}

// Power menu: 0 = turn off, 1 = restart, 2 = DSi Menu
static void power_action(int i) {
    sfx(SFX_DECIDE);
    s_ovl = OVL_NONE;
    if (s_scr == SCR_NEARBY) close_nearby();     // saves the people met
    if (s_scr == SCR_STORE) close_store();
    settings_save(&s_cfg);
    coll_save();
    fade(true);
    hand_over_display();
    if (i == 0) power_off_console();
    else if (i == 1) restart_console();
    else return_to_dsi_menu();
    fade(false);                          // (simulator)
}

static void menu_activate(void) {
    int t = s_menu_target, i = s_menu_sel;
    switch (s_menu_kind) {
        case MENU_POWER: power_action(power_off_supported() ? i : i + 1); break;
        case MENU_PHOTO: ph_menu_activate(i); break;
        case MENU_PICK_GAME: if (i < s_pick_n) ph_start_crop(4 /* CROP_COVER */, s_pick_items[i]); break;
        case MENU_PICK_FOLDER: if (i < coll_count()) ph_start_crop(5 /* CROP_FOLDER */, i); break;
        case MENU_FOLDER_OPTS:
            s_ovl = OVL_NONE;
            if (i == 0) start_pick(t);
            else if (i == 1) { s_menu_new = false; open_menu(MENU_COLOR, t); }
            else if (i == 2) open_keyboard(KB_FOLDER_RENAME, coll_name(t));
            else {
                char msg[160];
                snprintf(msg, sizeof(msg), tr(S_DELFOLDER_MSG, L()), coll_name(t));
                show_dialog(DLG_DELFOLDER, ICON_TRASH, tr(S_DELFOLDER_TITLE, L()), msg);
                s_dlg_sel = 1;
            }
            sfx(SFX_DECIDE);
            break;
        case MENU_COLOR:
            coll_set_color(t, i);
            coll_save();
            s_ovl = OVL_NONE;
            sfx(SFX_DECIDE);
            if (s_menu_new) { s_menu_new = false; start_pick(t); }
            break;
        case MENU_GAMECFG: {
            const GameItem *it = library_get(t);
            if (!it) break;
            GameCfg gc;
            game_cfg_get(it->filename, &gc);
            int row = s_menu_dsw ? (i == 0 ? 4 : 3) : i;
            if (row == 0) gc.cpu = (uint8_t)((gc.cpu + 1) % 3);
            else if (row == 1) gc.mode = (uint8_t)((gc.mode + 1) % 3);
            else if (row == 2) gc.vram ^= 1;
            else if (row == 4) gc.dsw ^= 1;
            else memset(&gc, 0, sizeof(gc));
            if (game_cfg_set(it->filename, &gc)) sfx(row == 3 ? SFX_BACK : SFX_TOGGLE);
            else sfx(SFX_ERROR);
            s_menu_ver++;
            break;
        }
        case MENU_ADD_TO: {
            const GameItem *it = library_get(t);
            if (!it) break;
            bool on = !coll_has(i, it->path);
            coll_set(i, it->path, on);
            coll_save();
            build_tabs();
            sfx(on ? SFX_TOGGLE : SFX_BACK);
            break;
        }
    }
}

static void input_menu(u32 k, u32 rep) {
    if ((rep & KEY_DOWN) && s_menu_sel < s_menu_n - 1) { s_menu_sel++; sfx(SFX_TICK); }
    if ((rep & KEY_UP) && s_menu_sel > 0) { s_menu_sel--; sfx(SFX_TICK); }
    if (k & KEY_A) menu_activate();
    if (k & KEY_B) {
        s_ovl = OVL_NONE;
        sfx(SFX_BACK);
        if (s_menu_kind == MENU_COLOR && s_menu_new) { s_menu_new = false; start_pick(s_menu_target); }
    }
}

static int menu_rh(void) { return s_menu_n > 7 ? 16 : 18; }

// at most 8 rows are visible (12 folders would not fit above the hint bar); the list scrolls
#define MENU_VIS 8
static int menu_vis(void) { return s_menu_n < MENU_VIS ? s_menu_n : MENU_VIS; }
static void menu_fix_scroll(void) {
    if (s_menu_sel < s_menu_scroll) s_menu_scroll = s_menu_sel;
    if (s_menu_sel >= s_menu_scroll + MENU_VIS) s_menu_scroll = s_menu_sel - MENU_VIS + 1;
    if (s_menu_scroll > s_menu_n - menu_vis()) s_menu_scroll = s_menu_n - menu_vis();
    if (s_menu_scroll < 0) s_menu_scroll = 0;
}

static int menu_rows_y0(int *h_out) {
    int rh = menu_rh(), h = 30 + menu_vis() * rh + 6;
    int y0 = (HINT_BAR_Y - h) / 2 + 2;
    if (y0 < 4) y0 = 4;
    if (h_out) *h_out = h;
    return y0;
}

static void render_menu(void) {
    Canvas *c = &g_bot;
    ui_scrim(c, 16);
    int h, y0 = menu_rows_y0(&h), rh = menu_rh();
    gfx_shadow(c, 16, y0, 224, h, 8, 4, 30);
    gfx_round_rect(c, 16, y0, 224, h, 8, T.panel, 32);
    const char *title = "";
    if (s_menu_kind == MENU_COLOR) title = tr(S_FOLDER_COLOR, L());
    else if (s_menu_kind == MENU_POWER) title = tr(S_POWER, L());
    else if (s_menu_kind == MENU_ADD_TO || s_menu_kind == MENU_GAMECFG) { const GameItem *it = library_get(s_menu_target); title = it ? it->title : ""; }
    else if (s_menu_kind == MENU_PHOTO) title = tr(S_PH_SET, L());
    else if (s_menu_kind == MENU_PICK_GAME) title = tr(S_PH_PICK_GAME, L());
    else if (s_menu_kind == MENU_PICK_FOLDER) title = tr(S_PH_PICK_FOLDER, L());
    else title = coll_name(s_menu_target);
    if (s_menu_kind == MENU_FOLDER_OPTS || s_menu_kind == MENU_COLOR) {
        gfx_circle(c, 32, y0 + 15, 7, coll_rgb(s_menu_target), 32);
        gfx_text(c, FONT_BOLD, 46, y0 + 8, title, T.text, 184);
    } else {
        gfx_icon(c, s_menu_kind == MENU_GAMECFG ? ICON_GEAR : s_menu_kind == MENU_POWER ? ICON_POWER :
                    (s_menu_kind == MENU_PHOTO || s_menu_kind == MENU_PICK_GAME) ? ICON_PHOTO : ICON_FOLDER, 24, y0 + 8, T.accent);
        gfx_text(c, FONT_BOLD, 46, y0 + 8, title, T.text, 184);
    }
    gfx_rect(c, 24, y0 + 28, 208, 1, T.divider);
    menu_fix_scroll();
    for (int i = s_menu_scroll; i < s_menu_scroll + menu_vis(); i++) {
        int y = y0 + 31 + (i - s_menu_scroll) * rh;
        bool sel = (i == s_menu_sel);
        if (sel) gfx_round_rect(c, 22, y, 212, rh - 1, 4, T.panel_hi, 32);
        if (s_menu_kind == MENU_POWER) {
            static const IconId ic[3] = { ICON_POWER, ICON_RESTORE, ICON_HOME };
            static const StrId lb[3] = { S_POWER_OFF, S_RESTART, S_DSI_MENU };
            int r = power_off_supported() ? i : i + 1;   // no "Turn off" with the stock ARM7
            gfx_icon(c, ic[r], 28, y + 1, sel ? T.accent : T.text2);
            gfx_text(c, FONT_BODY, 50, y + 2, tr(lb[r], L()), T.text, 170);
        } else if (s_menu_kind == MENU_PHOTO) {
            static const IconId pic[7] = { ICON_PHOTO, ICON_PHOTO, ICON_USER, ICON_PLAY, ICON_GRID, ICON_FOLDER, ICON_USERS };
            static const StrId plb[7] = { S_PHOTOS, S_PH_SET_WALL, S_PH_SET_AVATAR, S_PH_SET_BOOT, S_PH_SET_COVER, S_PH_SET_FOLDER, S_PH_SHARE };
            int kind = s_phm_rows[i];
            IconId ic = kind == 99 ? ICON_CLOCK : pic[kind % 7];
            gfx_icon(c, ic, 28, y + 1, sel ? T.accent : T.text2);
            gfx_text(c, FONT_BODY, 50, y + 2, tr(kind == 99 ? S_PH_SLIDESHOW : plb[kind % 7], L()), T.text, 170);
        } else if (s_menu_kind == MENU_PICK_GAME) {
            const GameItem *it = library_get(s_pick_items[i]);
            gfx_text(c, FONT_BODY, 30, y + 1, it ? it->title : "", T.text, 196);
        } else if (s_menu_kind == MENU_PICK_FOLDER) {
            gfx_circle(c, 36, y + 8, 6, coll_rgb(i), 32);
            gfx_text(c, FONT_BODY, 50, y + 2, coll_name(i), T.text, 150);
        } else if (s_menu_kind == MENU_FOLDER_OPTS) {
            static const IconId ic[4] = { ICON_CHECK, ICON_PALETTE, ICON_NOTE, ICON_TRASH };
            static const StrId lb[4] = { S_FOLDER_EDIT, S_FOLDER_COLOR, S_FOLDER_RENAME, S_FOLDER_DELETE };
            gfx_icon(c, ic[i], 28, y + 1, i == 3 ? T.danger : (sel ? T.accent : T.text2));
            gfx_text(c, FONT_BODY, 50, y + 2, tr(lb[i], L()), i == 3 ? T.danger : T.text, 170);
        } else if (s_menu_kind == MENU_GAMECFG) {
            const GameItem *it = library_get(s_menu_target);
            GameCfg gc;
            game_cfg_get(it ? it->filename : "", &gc);
            static const StrId lb[5] = { S_GC_CPU, S_GC_MODE, S_GC_VRAM, S_GC_RESET, S_GC_BOOT };
            static const StrId cpu[3] = { S_GC_AUTO, S_GC_CPU_NORMAL, S_GC_CPU_FAST };
            static const StrId mode[3] = { S_GC_AUTO, S_GC_MODE_DS, S_GC_MODE_DSI };
            int row = s_menu_dsw ? (i == 0 ? 4 : 3) : i;
            gfx_text(c, FONT_BODY, 30, y + 2, tr(lb[row], L()), row == 3 ? T.text2 : T.text, 110);
            const char *v = row == 0 ? tr(cpu[gc.cpu % 3], L()) : row == 1 ? tr(mode[gc.mode % 3], L()) : row == 2 ? tr(gc.vram ? S_ON : S_GC_AUTO, L())
                          : row == 4 ? (gc.dsw ? "nds-bootstrap" : "Unlaunch") : "";
            gfx_text_align(c, FONT_BODY, 140, y + 2, 88, v, sel ? T.sel_text : T.text2, ALIGN_RIGHT);
        } else if (s_menu_kind == MENU_COLOR) {
            uint16_t a;
            profile_color(i, &a, NULL, NULL, NULL);
            gfx_circle(c, 36, y + 8, 6, a, 32);
            gfx_text(c, FONT_BODY, 50, y + 2, tr((StrId)(S_COLOR_0 + i), L()), T.text, 150);
            if (i == coll_color(s_menu_target)) gfx_icon(c, ICON_CHECK, 212, y + 1, T.accent);
        } else {
            gfx_circle(c, 36, y + 8, 6, coll_rgb(i), 32);
            gfx_text(c, FONT_BODY, 50, y + 2, coll_name(i), T.text, 150);
            const GameItem *it = library_get(s_menu_target);
            if (it && coll_has(i, it->path)) gfx_icon(c, ICON_CHECK, 212, y + 1, T.accent);
        }
        if (sel) ui_sel_frame(c, 22 + 3, y + 3, 212 - 6, rh - 7, 2, s_frame);
    }
    Hint hh[2] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_A, tr(S_OK, L()), KEY_A } };
    s_hit_n = ui_hint_bar(c, hh, 2, s_hits);
}

#define FOLD_COLS 4
#define FOLD_TILE 56
#define FOLD_STRIDE 61
#define FOLD_X0 8
#define FOLD_Y0 28

static void render_folders(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    gfx_icon(c, ICON_FOLDER, 10, 5, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_FOLDERS, L()), T.text, 150);
    char cnt[32];
    snprintf(cnt, sizeof(cnt), tr(S_FOLDERS_COUNT, L()), coll_count());
    gfx_text_align(c, FONT_SMALL, 150, 6, 94, cnt, T.text2, ALIGN_RIGHT);
    int n = fold_items();
    int sel_row = s_fold_sel / FOLD_COLS;
    if (sel_row < s_fold_scroll) s_fold_scroll = sel_row;
    if (sel_row > s_fold_scroll + 1) s_fold_scroll = sel_row - 1;
    gfx_set_clip(c, 0, FOLD_Y0 - 4, 256, HINT_BAR_Y - FOLD_Y0);
    for (int i = 0; i < n; i++) {
        int row = i / FOLD_COLS - s_fold_scroll, col = i % FOLD_COLS;
        int x = FOLD_X0 + col * FOLD_STRIDE, y = FOLD_Y0 + row * (FOLD_STRIDE + 12);
        if (y > HINT_BAR_Y || y + FOLD_TILE < FOLD_Y0 - 4) continue;
        if (i < coll_count()) {
            folder_tile(c, x, y, FOLD_TILE, coll_rgb(i), coll_name(i), coll_size(i), false, folder_pic(i));
        } else {
            gfx_round_rect(c, x, y + 8, FOLD_TILE, FOLD_TILE - 8, 5, T.tile, 32);
            gfx_icon_centered(c, ICON_PLUS_L, x + FOLD_TILE / 2, y + FOLD_TILE / 2 + 4, T.text2);
        }
        const char *label = i < coll_count() ? coll_name(i) : tr(S_NEW_FOLDER, L());
        gfx_text_align(c, FONT_SMALL, x - 2, y + FOLD_TILE + 1, FOLD_TILE + 4, label, i == s_fold_sel ? T.sel_text : T.text2, ALIGN_CENTER);
        if (i == s_fold_sel) ui_sel_frame(c, x, y, FOLD_TILE, FOLD_TILE, 5, s_frame);
    }
    gfx_reset_clip(c);
    bool on_folder = s_fold_sel < coll_count();
    Hint h[3] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_Y, tr(S_OPTIONS, L()), KEY_Y }, { BTN_A, tr(S_OK, L()), KEY_A } };
    if (on_folder) s_hit_n = ui_hint_bar(c, h, 3, s_hits);
    else { h[1] = h[2]; s_hit_n = ui_hint_bar(c, h, 2, s_hits); }
}

static void render_top_folders(void) {
    Canvas *c = &g_top;
    if (s_fold_sel >= coll_count()) {
        hero_simple(c, ICON_FOLDER_L, C8(250, 190, 40), tr(S_NEW_FOLDER, L()), tr(S_NEW_FOLDER_HINT, L()), NULL);
        return;
    }
    int f = s_fold_sel;
    Color col = coll_rgb(f);
    if (ui_wallpaper_present()) ui_wallpaper_draw(c);
    else {
        gfx_vgradient(c, 0, 0, 256, 120, gfx_mix(T.bg_top, col, T.light ? 50 : 70), T.bg_top);
        gfx_rect(c, 0, 120, 256, 72, T.bg_top);
    }
    ui_status_bar(c, &s_cfg);
    gfx_shadow(c, 14, 32, 96, 96, 8, 4, 24);
    folder_tile(c, 14, 32, 96, col, coll_name(f), coll_size(f), true, folder_pic(f));
    gfx_text(c, FONT_SMALL, 122, 32, tr(S_FOLDERS, L()), T.text2, 124);
    int lines = gfx_text_wrap(c, FONT_TITLE, 122, 45, 124, 2, 18, coll_name(f), T.text);
    int y = 45 + lines * 18 + 4;
    char cnt[32];
    snprintf(cnt, sizeof(cnt), tr(S_SOFTWARE_COUNT, L()), coll_size(f));
    gfx_text(c, FONT_SMALL, 122, y, cnt, T.text2, 124);
    // the first games in the folder
    int shown = 0;
    for (int i = 0; i < library_count() && shown < 4; i++) {
        const GameItem *it = library_get(i);
        if (!coll_has(f, it->path)) continue;
        ui_thumb(c, i, it, 122 + shown * 30, 140, 26);
        shown++;
    }
}

static void input_folders(u32 k, u32 rep) {
    int n = fold_items(), sel = s_fold_sel;
    if ((rep & KEY_RIGHT) && sel + 1 < n) sel++;
    if ((rep & KEY_LEFT) && sel > 0) sel--;
    if ((rep & KEY_DOWN) && sel + FOLD_COLS < n) sel += FOLD_COLS;
    if ((rep & KEY_UP) && sel - FOLD_COLS >= 0) sel -= FOLD_COLS;
    if (sel != s_fold_sel) { s_fold_sel = sel; sfx(SFX_TICK); }
    if (k & KEY_A) {
        if (s_fold_sel < coll_count()) open_all(COLL_TAB(s_fold_sel));
        else if (coll_count() >= MAX_COLLECTIONS) { toast(tr(S_FOLDERS_FULL, L())); sfx(SFX_ERROR); }
        else open_keyboard(KB_FOLDER, "");
    }
    if ((k & KEY_Y) && s_fold_sel < coll_count()) open_menu(MENU_FOLDER_OPTS, s_fold_sel);
    if (k & KEY_B) { coll_save(); build_tabs(); build_view(); s_scr = SCR_ALL; sfx(SFX_BACK); }
}

// ---------------------------------------------------------------------------
// Profiles: login screen, switching, statistics
// ---------------------------------------------------------------------------
static int login_items(void) { return profiles_count() + (profiles_count() < MAX_PROFILES ? 1 : 0); }
static int login_slot_at(int i) { return i < profiles_count() ? profiles_slot(i) : -1; }   // -1 = "new profile"

static void profile_paths_for(int slot) {
    char dir[MAX_PATH_LEN];
    profiles_dir(slot, dir, sizeof(dir));
    launcher_set_profile_dir(slot == 0 ? "" : dir);   // the first profile keeps DS saves next to the games
}

// Everything that belongs to a profile is (re)loaded here
static void profile_login(int slot) {
    s_ph_scanned = false;               // favorite photos are kept per profile
    if (!profiles_exists(slot)) slot = profiles_slot(0);
    if (s_cur_profile >= 0) settings_save(&s_cfg);
    s_cur_profile = slot;
    profiles_activate(slot);
    profile_paths_for(slot);
    settings_load(&s_cfg);
    const ProfileInfo *pi = profiles_get(slot);
    if (pi) snprintf(s_cfg.profile_name, sizeof(s_cfg.profile_name), "%s", pi->name);
    char p[MAX_PATH_LEN];
    profiles_file(slot, "avatar.bin", p, sizeof(p));
    ui_avatar_load_slot(slot, p);
    profiles_file(slot, "wallpaper.bin", p, sizeof(p));
    ui_wallpaper_load(p);
    s_login_wp_slot = -1;
    apply_theme();
    sound_set_volume(s_cfg.volume);
    playtime_load(&s_pt);
    playtime_end_session(&s_pt);
    pathset_load();
    coll_load();
    library_refresh_flags();
    apply_covers();
    refresh_all();
    ProfilesGlobal *g = profiles_global();
    g->last = (uint8_t)slot;
    g->resume = 0;
    profiles_global_save();
    s_scr = SCR_HOME;
    s_ovl = OVL_NONE;
    s_home_sel = 0;
    s_home_focus = 0;
    s_home_scroll = s_home_scroll_target = 0;
    s_fade = 16;
    s_force_redraw = true;
}

// On the login screen the accent follows the highlighted profile
static void login_accent(void) {
    const ProfileInfo *pi = profiles_get(login_slot_at(s_login_sel));
    uint16_t a, h, sl, sd;
    profile_color(pi ? pi->color : 0, &a, &h, &sl, &sd);
    theme_set_accent(a, h, sl, sd);
}

static void open_login(int preselect, bool ask_pin) {
    s_scr = SCR_LOGIN;
    s_ovl = OVL_NONE;
    s_login_sel = 0;
    for (int i = 0; i < profiles_count(); i++) if (profiles_slot(i) == preselect) s_login_sel = i;
    s_login_wp_wait = 1;
    login_accent();
    s_force_redraw = true;
    if (ask_pin && profiles_has_pin(preselect)) open_pin(PIN_LOGIN, preselect);
}

static void login_activate(void) {
    int slot = login_slot_at(s_login_sel);
    if (slot < 0) {
        if (profiles_count() >= MAX_PROFILES) { toast(tr(S_PROFILES_FULL, L())); sfx(SFX_ERROR); return; }
        open_keyboard(KB_NEWPROFILE, "");
        return;
    }
    if (profiles_has_pin(slot)) { open_pin(PIN_LOGIN, slot); return; }
    sfx(SFX_DECIDE);
    profile_login(slot);
}

static void delete_profile(void) {
    int slot = s_cur_profile;
    if (!profiles_delete(slot)) { toast(tr(S_ONLY_PROFILE, L())); sfx(SFX_ERROR); return; }
    ui_avatar_load_slot(slot, NULL);
    ui_wallpaper_load(NULL);
    s_cur_profile = -1;
    sfx(SFX_BACK);
    open_login(profiles_slot(0), false);
}

static void fmt_minutes(uint32_t m, char *out, size_t len) {
    if (m >= 60) snprintf(out, len, tr(S_HM, L()), (unsigned)(m / 60), (unsigned)(m % 60));
    else snprintf(out, len, tr(S_M, L()), (unsigned)m);
}

static int library_find_filename(const char *fn) {
    for (int i = 0; i < library_count(); i++) if (!strcasecmp(library_get(i)->filename, fn)) return i;
    return -1;
}

static void open_stats(void) {
    s_st_total = s_st_games = s_st_launches = 0;
    s_st_top_n = 0;
    s_st_sys_n = 0;
    uint32_t sys_min[TYPE_UNKNOWN + 1];
    memset(sys_min, 0, sizeof(sys_min));
    for (int i = 0; i < s_pt.count; i++) {
        const PlaytimeEntry *e = &s_pt.entries[i];
        if (!e->total_minutes && !e->launch_count) continue;
        s_st_total += e->total_minutes;
        s_st_launches += e->launch_count;
        s_st_games++;
        // top 5 by minutes
        int pos = s_st_top_n;
        while (pos > 0 && s_pt.entries[s_st_top[pos - 1]].total_minutes < e->total_minutes) pos--;
        if (pos < 5) {
            int last = s_st_top_n < 5 ? s_st_top_n : 4;
            for (int j = last; j > pos; j--) s_st_top[j] = s_st_top[j - 1];
            s_st_top[pos] = i;
            if (s_st_top_n < 5) s_st_top_n++;
        }
        int li = library_find_filename(e->filename);
        if (li >= 0 && e->total_minutes) {
            int t = library_get(li)->type;
            if (t >= 0 && t <= TYPE_UNKNOWN) sys_min[t] += e->total_minutes;
        }
    }
    for (int i = 0; i < s_st_top_n; i++) s_st_top_lib[i] = library_find_filename(s_pt.entries[s_st_top[i]].filename);
    // systems, largest first (top 5)
    for (int n = 0; n < 5; n++) {
        int best = -1;
        for (int t = 0; t <= TYPE_UNKNOWN; t++) if (sys_min[t] && (best < 0 || sys_min[t] > sys_min[best])) best = t;
        if (best < 0) break;
        s_st_sys[s_st_sys_n] = (ItemType)best;
        s_st_sys_min[s_st_sys_n++] = sys_min[best];
        sys_min[best] = 0;
    }
    daylog_last_days((uint32_t)time(NULL), s_st_week);
    s_scr = SCR_STATS;
    sfx(SFX_DECIDE);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static void go_home(void) {
    s_scr = SCR_HOME;
    sfx(SFX_BACK);
}

// --- Photos (DSi Camera pictures, sd:/photos) -----------------------------
// The viewer, albums (all / favorites / one per month), "Use as…" (wallpaper, profile picture,
// boot screen, game cover, folder picture, Nearby), the screensaver and the photo of the day.
static int   s_ph_sel, s_ph_loaded = -1;   // s_ph_sel: position in the album, s_ph_loaded: photo index
static int   s_ph_bad = -1;   // picture that could not be read
static int   s_ph_stage;      // 1: "loading" is on screen, the picture is decoded on the next frame
static Color *s_ph_px;
static int  *s_ph_list;       // photo indices of the album shown
static int   s_ph_list_n, s_ph_album, s_ph_ver;
#define PH_MAX_MONTHS 48
static int   s_ph_months[PH_MAX_MONTHS], s_ph_month_n;
// crop ("Use as…")
enum { CROP_NONE, CROP_WALL, CROP_AVATAR, CROP_BOOT, CROP_COVER, CROP_FOLDER, CROP_SHARE };
static int   s_crop_kind, s_crop_target;
static PhotoCrop s_crop;
static int   s_crop_tx = -1, s_crop_ty;  // stylus drag on the lower screen

static int ph_cur(void) { return (s_ph_sel >= 0 && s_ph_sel < s_ph_list_n) ? s_ph_list[s_ph_sel] : -1; }

static void ph_favs_path(char *out, size_t len) {
    if (s_cur_profile >= 0) profiles_file(s_cur_profile, "photo_favs.txt", out, len);
    else snprintf(out, len, "%s/_hyper/photo_favs.txt", s_drv);
}

static bool ph_ensure_scan(void) {
    if (!s_ph_scanned) {
        photos_scan(s_drv);
        s_ph_scanned = true;
        char p[MAX_PATH_LEN];
        ph_favs_path(p, sizeof(p));
        photo_favs_load(p);
    }
    return photos_count() > 0;
}

static void ph_build_months(void) {
    s_ph_month_n = 0;
    for (int i = 0; i < photos_count() && s_ph_month_n < PH_MAX_MONTHS; i++) {
        int m = photo_month(photos_time(i));
        if (!m) continue;
        bool have = false;
        for (int k = 0; k < s_ph_month_n; k++) if (s_ph_months[k] == m) have = true;
        if (!have) s_ph_months[s_ph_month_n++] = m;
    }
    for (int i = 1; i < s_ph_month_n; i++)        // newest first
        for (int k = i; k > 0 && s_ph_months[k] > s_ph_months[k - 1]; k--) { int t = s_ph_months[k]; s_ph_months[k] = s_ph_months[k - 1]; s_ph_months[k - 1] = t; }
}

static int ph_album_count(void) { return 2 + s_ph_month_n; }   // all, favorites, months

static void ph_build_list(void) {
    if (!s_ph_list) s_ph_list = malloc(sizeof(int) * MAX_PHOTOS);
    s_ph_list_n = 0;
    if (!s_ph_list) return;
    int month = s_ph_album >= 2 ? s_ph_months[s_ph_album - 2] : 0;
    for (int i = 0; i < photos_count(); i++) {
        if (s_ph_album == 1 && !photo_is_fav(photos_path(i))) continue;
        if (month && photo_month(photos_time(i)) != month) continue;
        s_ph_list[s_ph_list_n++] = i;
    }
    if (s_ph_sel >= s_ph_list_n) s_ph_sel = s_ph_list_n ? s_ph_list_n - 1 : 0;
    s_ph_ver++;
}

static void ph_album_name(int a, char *out, size_t len) {
    if (a == 0) { snprintf(out, len, "%s", tr(S_PH_ALL, L())); return; }
    if (a == 1) { snprintf(out, len, "♥ %s", tr(S_PH_FAVS, L())); return; }
    int m = s_ph_months[a - 2];
    char names[96];
    snprintf(names, sizeof(names), "%s", tr(S_MONTHS, L()));
    const char *p = names;
    for (int k = 0; k < (m % 100) - 1 && p; k++) { p = strchr(p, ' '); if (p) p++; }
    char mon[16] = "?";
    if (p) { size_t n = strcspn(p, " "); if (n > sizeof(mon) - 1) n = sizeof(mon) - 1; memcpy(mon, p, n); mon[n] = '\0'; }
    snprintf(out, len, "%s %d", mon, m / 100);
}

static void photos_tile(Canvas *c, int x, int y, int size) {
    if (T.light) gfx_shadow(c, x, y, size, size, 6, 3, 14);
    gfx_round_rect_vgrad(c, x, y, size, size, 6, C8(60, 210, 160), C8(20, 150, 120));
    gfx_icon_centered(c, size >= 60 ? ICON_PHOTO_L : ICON_PHOTO, x + size / 2, y + size / 2 - 7, C8(255, 255, 255));
    gfx_text_align(c, FONT_SMALL, x + 2, y + size - 18, size - 4, tr(S_PHOTOS, L()), C8(255, 255, 255), ALIGN_CENTER);
}

static void open_photos(void) {
    s_ph_scanned = false;                         // pictures may have arrived (Studio, Nearby)
    if (!ph_ensure_scan()) { toast(tr(S_PHOTOS_NONE, L())); sfx(SFX_ERROR); return; }
    if (!s_ph_px) s_ph_px = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
    if (!s_ph_px) { toast(tr(S_PH_FAIL, L())); sfx(SFX_ERROR); return; }
    ph_build_months();
    s_ph_album = 0;
    s_ph_sel = 0;
    ph_build_list();
    s_ph_loaded = -1;
    s_ph_bad = -1;
    s_ph_stage = 0;
    s_crop_kind = CROP_NONE;
    s_scr = SCR_PHOTOS;
    sfx(SFX_DECIDE);
}

// the crop region on the upper screen: the rest is darkened
static void ph_draw_crop(Canvas *c) {
    int x, y, w, h;
    photo_crop_rect(&s_crop, &x, &y, &w, &h);
    gfx_rect_a(c, 0, 0, 256, y, (Color)0x8000, 20);
    gfx_rect_a(c, 0, y + h, 256, 192 - y - h, (Color)0x8000, 20);
    gfx_rect_a(c, 0, y, x, h, (Color)0x8000, 20);
    gfx_rect_a(c, x + w, y, 256 - x - w, h, (Color)0x8000, 20);
    if (s_crop_kind == CROP_AVATAR) gfx_ring(c, x + w / 2, y + h / 2, w / 2, 1, C8(255, 255, 255), 20);
    gfx_round_ring(c, x - 1, y - 1, w + 2, h + 2, 2, 2, T.accent, 32);
}

static void render_top_photos(void) {
    int cur = ph_cur();
    if (cur < 0) {
        gfx_clear(&g_top, (Color)0x8000);
        gfx_text_align(&g_top, FONT_BODY, 0, 90, 256, tr(S_PH_NONE_FAV, L()), C8(220, 220, 220), ALIGN_CENTER);
        return;
    }
    if (s_ph_loaded != cur) {
        if (s_ph_stage == 0) {
            // decoding takes a moment on the DSi: show that something happens first
            if (s_ph_loaded < 0) gfx_clear(&g_top, (Color)0x8000);
            else memcpy(g_top.px, s_ph_px, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
            gfx_rect_a(&g_top, 0, 0, 256, 192, (Color)0x8000, 18);
            ui_spinner(&g_top, 128, 96, 14, s_frame, C8(255, 255, 255));
            s_ph_stage = 1;
            return;
        }
        if (!photos_load(cur, s_ph_px)) {
            for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) s_ph_px[i] = 0x8000;
            s_ph_bad = cur;
        }
        s_ph_loaded = cur;
        s_ph_stage = 0;
    }
    memcpy(g_top.px, s_ph_px, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
    if (s_ph_bad == cur) gfx_text_align(&g_top, FONT_BODY, 0, 90, 256, tr(S_PHOTO_BAD, L()), C8(220, 220, 220), ALIGN_CENTER);
    if (s_crop_kind) { ph_draw_crop(&g_top); return; }
    if (photo_is_fav(photos_path(cur))) {
        gfx_icon_centered(&g_top, ICON_HEART, 240, 17, C8(40, 10, 20));
        gfx_icon_centered(&g_top, ICON_HEART, 240, 16, C8(255, 80, 110));
    }
}

static const StrId k_crop_title[] = { S_PHOTOS, S_PH_SET_WALL, S_PH_SET_AVATAR, S_PH_SET_BOOT, S_PH_SET_COVER, S_PH_SET_FOLDER, S_PH_SHARE };

static void render_photos(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, T.bg);
    if (s_crop_kind) {
        gfx_icon(c, ICON_PHOTO, 10, 4, T.text);
        gfx_text(c, FONT_BOLD, 30, 4, tr(k_crop_title[s_crop_kind], L()), T.text, 210);
        if (s_ph_loaded >= 0 && s_ph_loaded == ph_cur()) {
            int x, y, w, h;
            photo_crop_rect(&s_crop, &x, &y, &w, &h);
            static Color prev[112 * 84];
            int pw = (s_crop.aw == s_crop.ah) ? 84 : 112, ph = 84;
            photo_sample(s_ph_px, 256, 192, x, y, w, h, prev, pw, ph);
            int px = (256 - pw) / 2, py = 30;
            gfx_shadow(c, px, py, pw, ph, 6, 4, 24);
            if (s_crop_kind == CROP_AVATAR) {
                gfx_blit_round(c, px, py, prev, pw, ph, pw / 2);
            } else {
                gfx_blit_round(c, px, py, prev, pw, ph, 6);
            }
        }
        gfx_text_wrap(c, FONT_SMALL, 16, 124, 224, 3, 12, tr(S_PH_CROP_HINT, L()), T.text2);
        Hint h[2] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_A, tr(S_PH_SAVE, L()), KEY_A } };
        s_hit_n = ui_hint_bar(c, h, 2, s_hits);
        return;
    }
    gfx_icon(c, ICON_PHOTO, 10, 4, T.text);
    gfx_text(c, FONT_BOLD, 30, 4, tr(S_PHOTOS, L()), T.text, 100);
    // album: L / R
    char alb[40];
    ph_album_name(s_ph_album, alb, sizeof(alb));
    gfx_round_rect(c, 120, 3, 128, 16, 8, T.panel, 32);
    ui_button_glyph(c, BTN_L, 124, 5, T.text3);
    ui_button_glyph(c, BTN_R, 232, 5, T.text3);
    gfx_text_align(c, FONT_SMALL, 138, 5, 92, alb, T.text, ALIGN_CENTER);
    int cur = ph_cur();
    char line[64];
    if (s_ph_list_n) snprintf(line, sizeof(line), "%d / %d", s_ph_sel + 1, s_ph_list_n);
    else snprintf(line, sizeof(line), "0");
    gfx_text_align(c, FONT_HUGE, 0, 48, 256, line, T.text, ALIGN_CENTER);
    if (cur >= 0) {
        const char *p = photos_path(cur);
        const char *b = strrchr(p, '/');
        gfx_text_align(c, FONT_BODY, 10, 82, 236, b ? b + 1 : p, T.text2, ALIGN_CENTER);
        if (photos_time(cur)) {
            char d[24];
            ui_format_date(photos_time(cur), d, sizeof(d));
            gfx_text_align(c, FONT_SMALL, 10, 100, 236, d, T.text3, ALIGN_CENTER);
        }
        bool fav = photo_is_fav(p);
        gfx_circle(c, 128, 136, 15, fav ? C8(255, 80, 110) : T.panel, 32);
        gfx_icon_centered(c, ICON_HEART, 128, 136, fav ? C8(255, 255, 255) : T.text3);
        if (pressed_at(113, 121, 30, 30)) gfx_circle(c, 128, 136, 15, C8(0, 0, 0), 9);
    } else {
        gfx_text_wrap(c, FONT_BODY, 24, 86, 208, 2, 15, tr(S_PH_NONE_FAV, L()), T.text2);
    }
    // previous / next
    gfx_circle(c, 40, 136, 18, s_ph_sel > 0 ? T.panel_hi : T.panel, 32);
    gfx_icon_centered(c, ICON_CHEV_L, 40, 136, s_ph_sel > 0 ? T.text : T.text3);
    gfx_circle(c, 216, 136, 18, s_ph_sel < s_ph_list_n - 1 ? T.panel_hi : T.panel, 32);
    gfx_icon_centered(c, ICON_CHEV_R, 216, 136, s_ph_sel < s_ph_list_n - 1 ? T.text : T.text3);
    if (pressed_at(22, 118, 36, 36)) gfx_circle(c, 40, 136, 18, C8(0, 0, 0), 9);
    if (pressed_at(198, 118, 36, 36)) gfx_circle(c, 216, 136, 18, C8(0, 0, 0), 9);
    Hint h[3] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_X, tr(S_PH_FAV, L()), KEY_X }, { BTN_Y, tr(S_PH_SET, L()), KEY_Y } };
    s_hit_n = ui_hint_bar(c, h, cur >= 0 ? 3 : 1, s_hits);
}

static void photos_step(int d) {
    int n = s_ph_sel + d;
    if (n < 0 || n >= s_ph_list_n) { sfx(SFX_ERROR); return; }
    s_ph_sel = n;
    s_ph_ver++;
    sfx(SFX_TICK);
}

static void ph_album_step(int d) {
    int n = ph_album_count();
    s_ph_album = (s_ph_album + n + d) % n;
    s_ph_sel = 0;
    ph_build_list();
    sfx(SFX_PAGE);
}

static void ph_toggle_fav(void) {
    int cur = ph_cur();
    if (cur < 0) return;
    bool on = !photo_is_fav(photos_path(cur));
    photo_set_fav(photos_path(cur), on);
    photo_favs_save();
    sfx(on ? SFX_FAVORITE : SFX_BACK);
    if (s_ph_album == 1) ph_build_list();       // left the favorites album
    s_ph_ver++;
}

static void ph_open_set_menu(void) {
    if (ph_cur() < 0 || s_ph_loaded != ph_cur()) { sfx(SFX_ERROR); return; }
    s_phm_n = 0;
    s_phm_rows[s_phm_n++] = CROP_WALL;
    s_phm_rows[s_phm_n++] = CROP_AVATAR;
    s_phm_rows[s_phm_n++] = CROP_BOOT;
    s_phm_rows[s_phm_n++] = CROP_COVER;
    if (coll_count()) s_phm_rows[s_phm_n++] = CROP_FOLDER;
    if (s_cfg.beta) s_phm_rows[s_phm_n++] = CROP_SHARE;
    s_phm_rows[s_phm_n++] = 99;                 // start the screensaver
    open_menu(MENU_PHOTO, 0);
}

static void ph_start_crop(int kind, int target) {
    s_crop_kind = kind;
    s_crop_target = target;
    bool square = kind == CROP_AVATAR || kind == CROP_COVER || kind == CROP_FOLDER;
    photo_crop_init(&s_crop, square ? 1 : 4, square ? 1 : 3);
    s_crop_tx = -1;
    s_ovl = OVL_NONE;
    s_ph_ver++;
    sfx(SFX_DECIDE);
}

static uint32_t name_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static void folder_pic_path(const char *name, char *out, size_t len) {
    char f[32];
    snprintf(f, sizeof(f), "fold_%08x.bin", (unsigned)name_hash(name));
    profiles_file(s_cur_profile >= 0 ? s_cur_profile : 0, f, out, len);
}

// folder pictures (64x64), loaded when drawn
static Color *s_fold_pic[MAX_COLLECTIONS];
static uint32_t s_fold_pic_ver[MAX_COLLECTIONS], s_fold_pic_gen = 1;
static bool s_fold_pic_none[MAX_COLLECTIONS];
static const Color *folder_pic(int i) {
    if (i < 0 || i >= coll_count() || i >= MAX_COLLECTIONS) return NULL;
    uint32_t v = coll_version() * 131u + s_fold_pic_gen;
    if (s_fold_pic_ver[i] != v) {
        s_fold_pic_ver[i] = v;
        char p[MAX_PATH_LEN];
        folder_pic_path(coll_name(i), p, sizeof(p));
        if (!s_fold_pic[i]) s_fold_pic[i] = malloc(64 * 64 * sizeof(Color));
        s_fold_pic_none[i] = !s_fold_pic[i] || !photo_read_raw(p, "HFC1", s_fold_pic[i], 64, 64);
    }
    return s_fold_pic_none[i] ? NULL : s_fold_pic[i];
}

static void ph_crop_save(void) {
    int x, y, w, h;
    photo_crop_rect(&s_crop, &x, &y, &w, &h);
    Color *big = malloc(256 * 192 * sizeof(Color));
    char p[MAX_PATH_LEN];
    bool ok = false;
    StrId done = S_PH_FAIL;
    switch (big ? s_crop_kind : CROP_NONE) {
        case CROP_WALL:
            photo_sample(s_ph_px, 256, 192, x, y, w, h, big, 256, 192);
            profiles_file(s_cur_profile >= 0 ? s_cur_profile : 0, "wallpaper.bin", p, sizeof(p));
            ok = photo_write_raw(p, "HWP1", big, 256, 192);
            if (ok) { ui_wallpaper_load(p); s_login_wp_slot = -2; done = S_PH_DONE_WALL; }
            break;
        case CROP_AVATAR:
            photo_sample(s_ph_px, 256, 192, x, y, w, h, big, 64, 64);
            profiles_file(s_cur_profile >= 0 ? s_cur_profile : 0, "avatar.bin", p, sizeof(p));
            ok = photo_write_raw(p, "HAV1", big, 64, 64);
            if (ok) { ui_avatar_load_slot(s_cur_profile >= 0 ? s_cur_profile : 0, p); done = S_PH_DONE_AVATAR; }
            break;
        case CROP_BOOT:
            photo_sample(s_ph_px, 256, 192, x, y, w, h, big, 256, 192);
            snprintf(p, sizeof(p), "%s/_hyper/boot", s_drv);
            make_dirs(p);
            snprintf(p, sizeof(p), "%s/_hyper/boot/top.bin", s_drv);
            ok = photo_write_raw(p, "HWP1", big, 256, 192);
            if (ok) { s_cfg.splash = SPLASH_CUSTOM; settings_save(&s_cfg); done = S_PH_DONE_BOOT; }
            break;
        case CROP_COVER: {
            const GameItem *it = library_get(s_crop_target);
            if (!it) break;
            photo_sample(s_ph_px, 256, 192, x, y, w, h, big, COVER_SIZE, COVER_SIZE);
            snprintf(p, sizeof(p), "%s/_hyper/covers", s_drv);
            make_dirs(p);
            char name[MAX_TITLE_LEN + 8];
            snprintf(name, sizeof(name), "%s.png", it->filename);
            snprintf(p, sizeof(p), "%s/_hyper/covers/%s", s_drv, name);
            ok = photo_write_png(p, big, COVER_SIZE, COVER_SIZE);
            if (ok) {
                if (!s_cfg.covers) { s_cfg.covers = 1; settings_save(&s_cfg); apply_covers(); }
                covers_add_file(name);
                ui_tile_cache_reset();
                done = S_PH_DONE_COVER;
            }
            break;
        }
        case CROP_FOLDER:
            if (s_crop_target < 0 || s_crop_target >= coll_count()) break;
            photo_sample(s_ph_px, 256, 192, x, y, w, h, big, 64, 64);
            folder_pic_path(coll_name(s_crop_target), p, sizeof(p));
            ok = photo_write_raw(p, "HFC1", big, 64, 64);
            if (ok) { s_fold_pic_gen++; done = S_PH_DONE_FOLDER; }
            break;
        case CROP_SHARE:
            photo_sample(s_ph_px, 256, 192, x, y, w, h, big, NEARBY_PH_W, NEARBY_PH_H);
            profiles_file(s_cur_profile >= 0 ? s_cur_profile : 0, "nearby_photo.bin", p, sizeof(p));
            ok = photo_write_raw(p, "HFC1", big, NEARBY_PH_W, NEARBY_PH_H);
            if (ok) done = S_PH_SHARED;
            break;
    }
    free(big);
    toast(tr(done, L()));
    sfx(ok ? SFX_DECIDE : SFX_ERROR);
    s_crop_kind = CROP_NONE;
    s_ph_ver++;
}

static void ss_start(bool preview);

static void ph_menu_activate(int i) {
    if (i < 0 || i >= s_phm_n) return;
    int kind = s_phm_rows[i];
    if (kind == 99) { s_ovl = OVL_NONE; ss_start(true); return; }
    if (kind == CROP_COVER) {
        // which game? (alphabetical)
        if (!s_pick_items) s_pick_items = malloc(sizeof(int) * MAX_LIBRARY_ITEMS);
        if (!s_pick_items) return;
        s_pick_n = library_view(s_pick_items, MAX_LIBRARY_ITEMS, TYPE_UNKNOWN, SORT_ALPHA, true, &s_pt);
        int w = 0;
        for (int k = 0; k < s_pick_n; k++) { const GameItem *it = library_get(s_pick_items[k]); if (it && it->type != TYPE_APP) s_pick_items[w++] = s_pick_items[k]; }
        s_pick_n = w;
        if (!s_pick_n) { toast(tr(S_NO_GAMES, L())); sfx(SFX_ERROR); return; }
        open_menu(MENU_PICK_GAME, 0);
        return;
    }
    if (kind == CROP_FOLDER) { open_menu(MENU_PICK_FOLDER, 0); return; }
    ph_start_crop(kind, 0);
}

static void input_photos(u32 k, u32 rep) {
    if (s_crop_kind) {
        int step = 3;
        if (rep & KEY_LEFT) { photo_crop_move(&s_crop, -step, 0); s_ph_ver++; }
        if (rep & KEY_RIGHT) { photo_crop_move(&s_crop, step, 0); s_ph_ver++; }
        if (rep & KEY_UP) { photo_crop_move(&s_crop, 0, -step); s_ph_ver++; }
        if (rep & KEY_DOWN) { photo_crop_move(&s_crop, 0, step); s_ph_ver++; }
        if (rep & KEY_L) { photo_crop_zoom(&s_crop, -6); s_ph_ver++; }
        if (rep & KEY_R) { photo_crop_zoom(&s_crop, 6); s_ph_ver++; }
        if (k & KEY_A) ph_crop_save();
        else if (k & KEY_B) { s_crop_kind = CROP_NONE; s_ph_ver++; sfx(SFX_BACK); }
        return;
    }
    if (rep & KEY_RIGHT) photos_step(1);
    if (rep & KEY_LEFT) photos_step(-1);
    if (k & KEY_R) ph_album_step(1);
    if (k & KEY_L) ph_album_step(-1);
    if (k & KEY_X) ph_toggle_fav();
    if (k & KEY_Y) ph_open_set_menu();
    if (k & KEY_B) {
        s_scr = SCR_HOME;
        free(s_ph_px);                       // memory back (DS mode has little)
        s_ph_px = NULL;
        s_ph_loaded = -1;
        sfx(SFX_BACK);
    }
}

static void tap_photos(int x, int y) {
    if (s_crop_kind || y >= HINT_BAR_Y) return;
    if (y < 22 && x >= 120) { ph_album_step(x < 184 ? -1 : 1); return; }
    if (abs(x - 128) <= 16 && abs(y - 136) <= 16) { ph_toggle_fav(); return; }
    if (y >= 40) photos_step(x < 128 ? -1 : 1);
}

// stylus drag on the lower screen moves the crop region (called every frame)
static void ph_crop_drag(const AppInput *in) {
    if (s_scr != SCR_PHOTOS || !s_crop_kind || s_ovl != OVL_NONE) { s_crop_tx = -1; return; }
    if (!in->touching || in->ty >= HINT_BAR_Y) { s_crop_tx = -1; return; }
    if (s_crop_tx >= 0) {
        int dx = in->tx - s_crop_tx, dy = in->ty - s_crop_ty;
        if (dx || dy) { photo_crop_move(&s_crop, dx, dy); s_ph_ver++; }
    }
    s_crop_tx = in->tx;
    s_crop_ty = in->ty;
}

// ---------------------------------------------------------------------------
// Screensaver: photos glide by on the upper screen, the clock on the lower one.
// While charging (photo frame) the screens never dim; on battery they dim after 5 minutes.
// ---------------------------------------------------------------------------
#define SS_SHOW   (60 * 8)      // frames per photo
#define SS_FADE   48            // crossfade frames
static Color *s_ss_a, *s_ss_b, *s_ss_tmp;   // current, next, scratch
static int    s_ss_t, s_ss_var, s_ss_count;
static uint32_t s_ss_frames;    // frames since it started
static int   *s_ss_order;
static int    s_ss_pos;
static bool   s_ss_has_b;

static int ss_timeout_frames(void) {
    static const int mins[5] = { 3, 0, 1, 5, 10 };
    int m = mins[s_cfg.photo_opts & PHOTOOPT_SAVER_MASK];
    return m * 60 * 60;
}

static bool ss_charging(void) { return (ui_battery_state() & 0x80) != 0; }

static bool ss_next_photo(Color *dst) {
    for (int tries = 0; tries < 6 && s_ss_count > 0; tries++) {
        int idx = s_ss_order[s_ss_pos % s_ss_count];
        s_ss_pos++;
        if (photos_load(idx, dst)) return true;
    }
    return false;
}

static void ss_start(bool preview) {
    if (s_ss_on) return;
    free(s_pod_px);                 // the photo of the day is read again later (DS mode: memory)
    s_pod_px = NULL;
    s_pod_day = -1;
    // buffers only while it runs (DS mode has little memory): the next photo and the crossfade
    // are optional - without them the photos simply change
    if (!s_ss_order) s_ss_order = malloc(sizeof(int) * MAX_PHOTOS);
    if (!s_ss_a) s_ss_a = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
    if (!s_ss_b) s_ss_b = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
    if (!s_ss_tmp && s_ss_b) s_ss_tmp = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
    s_ss_count = 0;
    if (s_ss_a && s_ss_order && ph_ensure_scan()) {
        bool favs = (s_cfg.photo_opts & PHOTOOPT_FAVS_ONLY) && photo_favs_count() > 0;
        for (int i = 0; i < photos_count(); i++)
            if (!favs || photo_is_fav(photos_path(i))) s_ss_order[s_ss_count++] = i;
        uint32_t seed = (uint32_t)time(NULL) * 2654435761u;     // shuffled
        for (int i = s_ss_count - 1; i > 0; i--) {
            seed = seed * 1664525u + 1013904223u;
            int j = (int)((seed >> 8) % (uint32_t)(i + 1));
            int t = s_ss_order[i]; s_ss_order[i] = s_ss_order[j]; s_ss_order[j] = t;
        }
    }
    s_ss_pos = 0;
    if (s_ss_count && !ss_next_photo(s_ss_a)) s_ss_count = 0;
    s_ss_has_b = false;
    s_ss_t = 0;
    s_ss_var = (int)(time(NULL) & 7);
    s_ss_frames = 0;
    s_ss_on = true;
    s_ss_preview = preview;
    s_force_redraw = true;
}

static void ss_stop(void) {
    if (!s_ss_on) return;
    s_ss_on = false;
    free(s_ss_a); free(s_ss_b); free(s_ss_tmp);
    s_ss_a = s_ss_b = s_ss_tmp = NULL;
    s_idle = 0;
    s_force_redraw = true;
    s_fade = 8;                 // a short fade back in
}

static void ss_frame(void) {
    s_ss_frames++;
    if (!s_ss_count) return;
    s_ss_t++;
    if (!s_ss_b && s_ss_t >= SS_SHOW && s_ss_count > 1) {   // no second buffer: the next photo replaces it
        ss_next_photo(s_ss_a);
        s_ss_t = 0;
        s_ss_var = (s_ss_var + 3) & 7;
        return;
    }
    if (!s_ss_has_b && s_ss_b && s_ss_t > 30 && s_ss_count > 1) s_ss_has_b = ss_next_photo(s_ss_b);   // after the fade: the next one
    if (s_ss_t >= SS_SHOW + SS_FADE && s_ss_has_b) {
        Color *t = s_ss_a; s_ss_a = s_ss_b; s_ss_b = t;
        s_ss_has_b = false;
        s_ss_t = SS_FADE;
        s_ss_var = (s_ss_var + 3) & 7;
    } else if (s_ss_t >= SS_SHOW + SS_FADE) {
        s_ss_t = SS_SHOW;                         // a single photo: keep it
    }
}

static void render_top_ss(void) {
    Canvas *c = &g_top;
    if (!s_ss_count) {                            // no photos: just a slowly moving clock
        gfx_clear(c, (Color)0x8000);
        time_t now = time(NULL);
        struct tm *tmv = localtime(&now);
        if (tmv) {
            char t[8];
            snprintf(t, sizeof(t), "%02d:%02d", tmv->tm_hour, tmv->tm_min);
            int y = 40 + (int)((now / 60) % 90);
            gfx_text_align(c, FONT_HUGE, 0, y, 256, t, C8(200, 200, 200), ALIGN_CENTER);
        }
        return;
    }
    int span = SS_SHOW + SS_FADE;
    int t = s_ss_t * 4096 / span;
    photo_kenburns(c, s_ss_a, t, s_ss_var);
    if (s_ss_t >= SS_SHOW && s_ss_has_b && s_ss_tmp) {   // crossfade into the next photo
        Canvas tc;
        gfx_canvas_init(&tc, s_ss_tmp, SCREEN_WIDTH, SCREEN_HEIGHT);
        photo_kenburns(&tc, s_ss_b, (s_ss_t - SS_SHOW) * 4096 / span, (s_ss_var + 3) & 7);
        photo_blend(c->px, c->px, s_ss_tmp, (s_ss_t - SS_SHOW) * 32 / SS_FADE);
    } else if (s_ss_t < SS_FADE && s_ss_frames < SS_FADE) {   // the very first photo fades in
        gfx_rect_a(c, 0, 0, 256, 192, (Color)0x8000, 32 - s_ss_t * 32 / SS_FADE);
    }
}

static void render_bottom_ss(void) {
    Canvas *c = &g_bot;
    gfx_clear(c, (Color)0x8000);
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (!tmv) return;
    // drifts a little every minute (no burn-in)
    int dx = (int)((now / 60) % 21) - 10, dy = (int)((now / 60 / 7) % 15) - 7;
    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", tmv->tm_hour, tmv->tm_min);
    const int w = 30, h = 54, th = 7, gap = 8;
    int x0 = 128 - (4 * w + 3 * gap + 12) / 2 + dx, y0 = 50 + dy;
    Color col = C8(230, 230, 230);
    seg_digit(c, x0, y0, w, h, th, tmv->tm_hour / 10, col);
    seg_digit(c, x0 + w + gap, y0, w, h, th, tmv->tm_hour % 10, col);
    int cx = x0 + 2 * w + gap + 6 + gap / 2;
    gfx_circle(c, cx, y0 + 16, 3, T.accent, 32);
    gfx_circle(c, cx, y0 + h - 16, 3, T.accent, 32);
    seg_digit(c, x0 + 2 * w + 2 * gap + 12, y0, w, h, th, tmv->tm_min / 10, col);
    seg_digit(c, x0 + 3 * w + 3 * gap + 12, y0, w, h, th, tmv->tm_min % 10, col);
    char d[24];
    ui_format_date((uint32_t)now, d, sizeof(d));
    gfx_text_align(c, FONT_BODY, 0, y0 + h + 12, 256, d, C8(150, 150, 150), ALIGN_CENTER);
    ui_battery(c, 228 + dx / 2, 8);
    if (s_ss_frames < 60 * 4) gfx_text_align(c, FONT_SMALL, 0, 172, 256, tr(S_SS_TAP, L()), C8(110, 110, 110), ALIGN_CENTER);
}

// may the screensaver start now?
static bool ss_allowed(void) {
    if (s_launching || s_ovl != OVL_NONE || s_cur_profile < 0) return false;
    return s_scr == SCR_HOME || s_scr == SCR_ALL || s_scr == SCR_FOLDERS || s_scr == SCR_SETTINGS || s_scr == SCR_STATS;
}

static void ss_check_idle(void) {
    if (s_ss_on) return;
    int to = ss_timeout_frames();
    bool frame = !(s_cfg.photo_opts & PHOTOOPT_NO_FRAME) && ss_charging();
    if (frame && (!to || to > 60 * 60)) to = 60 * 60;   // photo frame: after a minute while charging
    if (!to || (int)s_idle < to || !ss_allowed()) return;
    ss_start(false);
}

// When the screens dim: after 2 minutes, or 5 minutes after the screensaver's time; never as a
// photo frame on the charger
static uint32_t idle_dim_frames(void) {
    int to = ss_timeout_frames();
    bool frame = !(s_cfg.photo_opts & PHOTOOPT_NO_FRAME) && ss_charging();
    if (frame) return 0xFFFFFFFFu;
    if (!to) return IDLE_DIM_FRAMES;
    return (uint32_t)to + 60u * 60u * 5u;
}

// ---------------------------------------------------------------------------
// Photo of the day behind the big clock (decoded once a day)
// ---------------------------------------------------------------------------
static bool   s_pod_ok;
static bool pod_ready(void) {
    if (s_cfg.photo_opts & PHOTOOPT_NO_POD) return false;
    int day = (int)(time(NULL) / 86400);
    if (day != s_pod_day) {
        s_pod_day = day;
        s_pod_ok = false;
        if (!s_pod_px) s_pod_px = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Color));
        if (s_pod_px && s_photos_exist && ph_ensure_scan()) {
            int i = photo_of_day(photos_count(), (uint32_t)time(NULL));
            s_pod_ok = i >= 0 && photos_load(i, s_pod_px);
        }
    }
    return s_pod_ok;
}

// --- Controller mode ("Kumanda", see pad.c) --------------------------------
static void pad_tile(Canvas *c, int x, int y, int size) {
    if (T.light) gfx_shadow(c, x, y, size, size, 6, 3, 14);
    gfx_round_rect_vgrad(c, x, y, size, size, 6, C8(150, 100, 240), C8(100, 60, 200));
    gfx_icon_centered(c, size >= 60 ? ICON_GAMEPAD_L : ICON_GAMEPAD, x + size / 2, y + size / 2 - 7, C8(255, 255, 255));
    gfx_text_align(c, FONT_SMALL, x + 2, y + size - 18, size - 4, tr(S_PAD, L()), C8(255, 255, 255), ALIGN_CENTER);
}

static void open_pad(void) {
    sfx(SFX_DECIDE);
    fade(true);
    s_scr = SCR_PAD;
    pad_enter(s_drv, s_cfg.profile_name, s_cfg.language);
    fade(false);
}

static void close_pad(void) {
    pad_leave();
    if (pad_take_rescan()) {                 // games arrived over Wi-Fi
        library_scan_begin(s_drv, false, NULL);
        covers_reset();
    }
    sfx(SFX_BACK);
    s_scr = SCR_HOME;
    s_force_redraw = true;
}

static void open_all(ItemType tab) {
    build_tabs();
    s_tab = 0;
    for (int i = 0; i < s_tab_n; i++) if (s_tabs[i] == tab) { s_tab = i; break; }
    build_view();
    s_grid_scroll = s_grid_scroll_target = 0;
    s_scr = SCR_ALL;
    sfx(SFX_DECIDE);
}

static void home_button(int b) {
    switch (b) {
        case HB_CARD:
            if (slot1_present()) do_launch(HOME_CARD);
            else { toast(tr(S_NO_CARD, L())); sfx(SFX_ERROR); }
            break;
        case HB_ALL: open_all(TYPE_UNKNOWN); break;
        case HB_SEARCH: open_keyboard(KB_SEARCH, ""); break;
        case HB_STORE: open_store(); break;
        case HB_SETTINGS: s_scr = SCR_SETTINGS; set_go(0, 0); s_set_scroll = 0; sfx(SFX_DECIDE); break;
        case HB_POWER: open_menu(MENU_POWER, 0); break;
        case HB_PAD: open_pad(); break;
        case HB_NEARBY: open_nearby(); break;
        case HB_PHOTOS: open_photos(); break;
    }
}

static void home_activate(void) {
    if (s_home_focus == 1) { home_button(s_home_btn); return; }
    if (s_home_n == 0) return;
    int id = s_home[s_home_sel];
    if (id == HOME_ALLSW) open_all(TYPE_UNKNOWN);
    else if (id == HOME_PAD) open_pad();
    else if (id == HOME_PHOTOS) open_photos();
    else if (id == HOME_STORE) open_store();
    else if (id == HOME_NEARBY) open_nearby();
    else do_launch(id);
}

static void input_home(u32 k, u32 rep) {
    if (rep & KEY_RIGHT) {
        if (s_home_focus == 0 && s_home_sel < s_home_n - 1) { s_home_sel++; sfx(SFX_TICK); }
        else if (s_home_focus == 1 && hb_step(s_home_btn, 1) != s_home_btn) { s_home_btn = hb_step(s_home_btn, 1); sfx(SFX_TICK); }
    }
    if (rep & KEY_LEFT) {
        if (s_home_focus == 0 && s_home_sel > 0) { s_home_sel--; sfx(SFX_TICK); }
        else if (s_home_focus == 1 && hb_step(s_home_btn, -1) != s_home_btn) { s_home_btn = hb_step(s_home_btn, -1); sfx(SFX_TICK); }
    }
    if ((k & KEY_DOWN) && s_home_focus == 0) {
        s_home_focus = 1;
        // pick the button under the selected tile
        int cx = home_tile_x(s_home_sel) + TILE / 2;
        int best = HB_SETTINGS;
        for (int b = 0; b < HB_COUNT; b++) if (hb_visible(b) && abs(btn_cx(b) - cx) < abs(btn_cx(best) - cx)) best = b;
        s_home_btn = best;
        sfx(SFX_TICK);
    }
    if ((k & KEY_UP) && s_home_focus == 1) { s_home_focus = 0; sfx(SFX_TICK); }
    if (k & KEY_A) home_activate();
    if ((k & KEY_Y) && s_home_focus == 0 && s_home_n && s_home[s_home_sel] >= -2 && s_home[s_home_sel] != HOME_ALLSW) open_options(s_home[s_home_sel]);
    if (k & KEY_X) {
        if (s_home_focus == 0 && s_home_n && s_home[s_home_sel] >= 0) {
            GameItem *it = library_get(s_home[s_home_sel]);
            pathset_set_favorite(it->path, !(it->flags & ITEM_FLAG_FAVORITE));
            library_refresh_flags();
            sfx(SFX_FAVORITE);
        }
    }
    if (k & KEY_START) { s_scr = SCR_SETTINGS; set_go(0, 0); s_set_scroll = 0; sfx(SFX_MODAL); }
    if (k & KEY_SELECT) open_keyboard(KB_SEARCH, "");
    if (k & (KEY_R | KEY_L)) open_all(TYPE_FOLDER);        // L or R: favorites
    if (k & KEY_B) {
        if (s_home_focus == 1) { s_home_focus = 0; sfx(SFX_BACK); }
        else if (s_home_sel != 0) { s_home_sel = 0; sfx(SFX_BACK); }
    }
}

static void input_all(u32 k, u32 rep) {
    int n = s_view_n;
    if (n > 0) {
        int sel = s_grid_sel;
        if ((rep & KEY_RIGHT) && sel + 1 < n) sel++;
        if ((rep & KEY_LEFT) && sel > 0) sel--;
        if ((rep & KEY_DOWN) && sel + GRID_COLS < n) sel += GRID_COLS;
        else if ((rep & KEY_DOWN) && sel / GRID_COLS < (n - 1) / GRID_COLS) sel = n - 1;
        if ((rep & KEY_UP) && sel - GRID_COLS >= 0) sel -= GRID_COLS;
        if (sel != s_grid_sel) { s_grid_sel = sel; sfx(SFX_TICK); }
    }
    if (k & (KEY_R | KEY_L)) {
        int d = (k & KEY_R) ? 1 : -1;
        s_tab = (s_tab + s_tab_n + d) % s_tab_n;
        s_view_n = 0;
        build_view();
        s_grid_sel = 0;
        s_grid_scroll = s_grid_scroll_target = 0;
        sfx(SFX_PAGE);
    }
    if (s_pick_coll >= 0) {
        // ticking the games of a folder
        if ((k & KEY_A) && n) {
            const char *path = library_get(s_view[s_grid_sel])->path;
            bool on = !coll_has(s_pick_coll, path);
            coll_set(s_pick_coll, path, on);
            sfx(on ? SFX_TOGGLE : SFX_BACK);
        }
        if (k & (KEY_B | KEY_START)) {
            coll_save();
            s_pick_coll = -1;
            build_tabs();
            build_view();
            s_scr = SCR_FOLDERS;
            sfx(SFX_DECIDE);
        }
        return;
    }
    if ((k & KEY_A) && n) do_launch(s_view[s_grid_sel]);
    if ((k & KEY_Y) && n) open_options(s_view[s_grid_sel]);
    if ((k & KEY_X) && n) {
        GameItem *it = library_get(s_view[s_grid_sel]);
        pathset_set_favorite(it->path, !(it->flags & ITEM_FLAG_FAVORITE));
        library_refresh_flags();
        sfx(SFX_FAVORITE);
    }
    if (k & KEY_SELECT) open_keyboard(KB_SEARCH, "");
    if ((k & KEY_START) && s_tab_n > 0 && s_tabs[s_tab] == TYPE_APP) open_apps();
    else if (k & KEY_START) open_folders();                // folders live in All Games: new / rename / games
    if (k & KEY_B) { build_home(); go_home(); }
}

static void kb_update_results(void) {
    if (s_kb_mode != KB_SEARCH) return;
    s_res_n = library_search(s_kb, s_results, 48, s_cfg.show_hidden != 0);
    if (s_res_sel >= s_res_n) s_res_sel = s_res_n ? s_res_n - 1 : 0;
}

static void kb_backspace(void) {
    size_t n = strlen(s_kb);
    if (!n) return;
    n--;
    while (n > 0 && ((unsigned char)s_kb[n] & 0xC0) == 0x80) n--;
    s_kb[n] = '\0';
    kb_update_results();
}

// Turkish-aware lower-casing of the key labels for names / notes
static void kb_type(const char *key) {
    char out[8];
    if (s_kb_mode == KB_WIFIPASS) {          // typed exactly as shown
        if (!strcmp(key, "↑")) { s_kb_caps = !s_kb_caps; sfx(SFX_TOGGLE); return; }
        if (!strcmp(key, "#+=") || !strcmp(key, "abc")) { s_kb_sym = !s_kb_sym; sfx(SFX_PAGE); return; }
        size_t n = strlen(s_kb), kl = strlen(key);
        if (n + kl > 63) { sfx(SFX_ERROR); return; }
        strcat(s_kb, key);
        sfx(SFX_KEY);
        return;
    }
    bool upper = true;
    size_t n = strlen(s_kb);
    if (s_kb_mode != KB_SEARCH && n > 0) {
        char prev = s_kb[n - 1];
        upper = (prev == ' ') || (s_kb_mode == KB_NOTE && n >= 2 && s_kb[n - 2] == '.' && prev == ' ');
        if (s_kb_mode == KB_NOTE || s_kb_mode == KB_GREETING) upper = (n >= 2 && (s_kb[n - 2] == '.' || s_kb[n - 2] == '!') && prev == ' ');
    }
    if (s_kb_mode == KB_SEARCH) upper = false;
    snprintf(out, sizeof(out), "%s", key);
    if (!upper) {
        if (out[1] == '\0' && out[0] >= 'A' && out[0] <= 'Z') {
            out[0] = (char)(out[0] + 32);
            if (out[0] == 'i' && (L() == LANG_TR || L() == LANG_AZ)) snprintf(out, sizeof(out), "ı");
        } else if (!strcmp(key, "Ş")) snprintf(out, sizeof(out), "ş");
        else if (!strcmp(key, "Ç")) snprintf(out, sizeof(out), "ç");
        else if (!strcmp(key, "Ö")) snprintf(out, sizeof(out), "ö");
        else if (!strcmp(key, "Ü")) snprintf(out, sizeof(out), "ü");
        else if (!strcmp(key, "Ğ")) snprintf(out, sizeof(out), "ğ");
        else if (!strcmp(key, "İ")) snprintf(out, sizeof(out), "i");
        else if (!strcmp(key, "Ə")) snprintf(out, sizeof(out), "ə");
    }
    size_t max = (s_kb_mode == KB_PROFILE || s_kb_mode == KB_NEWPROFILE) ? sizeof(s_cfg.profile_name) - 1 :
                 (s_kb_mode == KB_FOLDER || s_kb_mode == KB_FOLDER_RENAME) ? COLL_NAME_LEN - 1 :
                 s_kb_mode == KB_GREETING ? 47 : sizeof(s_kb) - 1;
    if (n + strlen(out) > max) { sfx(SFX_ERROR); return; }
    strcat(s_kb, out);
    kb_update_results();
    sfx(SFX_KEY);
}

static void kb_done(void) {
    if (s_kb_mode == KB_PROFILE) {
        if (s_kb[0]) snprintf(s_cfg.profile_name, sizeof(s_cfg.profile_name), "%s", s_kb);
        settings_save(&s_cfg);
        const ProfileInfo *pi = profiles_get(s_cur_profile);
        if (pi && s_kb[0]) {
            ProfileInfo t = *pi;
            snprintf(t.name, sizeof(t.name), "%s", s_kb);
            profiles_update(s_cur_profile, &t);
        }
        s_scr = SCR_SETTINGS;
        sfx(SFX_DECIDE);
    } else if (s_kb_mode == KB_FOLDER) {
        if (!s_kb[0]) { sfx(SFX_ERROR); return; }
        int idx = coll_create(s_kb, profiles_free_color() % PROFILE_COLORS);
        if (idx < 0) { s_scr = SCR_FOLDERS; toast(tr(S_FOLDERS_FULL, L())); sfx(SFX_ERROR); return; }
        coll_save();
        s_fold_sel = idx;
        s_scr = SCR_FOLDERS;
        s_menu_new = true;                 // color, then tick the games
        open_menu(MENU_COLOR, idx);
    } else if (s_kb_mode == KB_FOLDER_RENAME) {
        if (s_kb[0]) {
            char from[MAX_PATH_LEN], to[MAX_PATH_LEN];
            folder_pic_path(coll_name(s_fold_sel), from, sizeof(from));
            folder_pic_path(s_kb, to, sizeof(to));
            coll_rename(s_fold_sel, s_kb);
            coll_save();
            build_tabs();
            if (strcmp(from, to) != 0 && file_exists(from)) { remove(to); rename(from, to); }
            s_fold_pic_gen++;
        }
        s_scr = SCR_FOLDERS;
        sfx(SFX_DECIDE);
    } else if (s_kb_mode == KB_NEWPROFILE) {
        if (!s_kb[0]) { sfx(SFX_ERROR); return; }
        int slot = profiles_create(s_kb);
        if (slot < 0) { s_scr = SCR_LOGIN; toast(tr(S_PROFILES_FULL, L())); sfx(SFX_ERROR); return; }
        ui_avatar_load_slot(slot, NULL);
        // the new profile starts with the current look and language
        SystemSettings base = s_cfg;
        profiles_activate(slot);
        settings_defaults(&s_cfg);
        s_cfg.theme = base.theme;
        s_cfg.language = base.language;
        s_cfg.sfx_pack = base.sfx_pack;
        s_cfg.volume = base.volume;
        s_cfg.brightness = base.brightness;
        snprintf(s_cfg.profile_name, sizeof(s_cfg.profile_name), "%s", s_kb);
        settings_save(&s_cfg);
        s_cur_profile = -1;
        sfx(SFX_DECIDE);
        profile_login(slot);
    } else if (s_kb_mode == KB_NOTE) {
        append_note(s_kb);
        s_scr = SCR_NOTES;
        sfx(SFX_DECIDE);
    } else if (s_kb_mode == KB_WIFIPASS) {
        if (strlen(s_kb) < 8) { toast(tr(S_WIFI_SHORT, L())); sfx(SFX_ERROR); return; }   // WPA: 8-63 characters
        s_scr = SCR_WIFI;
        sfx(SFX_DECIDE);
        wifi_test(s_wf_ssid, s_kb);
        s_kb[0] = '\0';
    } else if (s_kb_mode == KB_GREETING) {
        nearby_save_greeting(s_kb);
        s_scr = SCR_NEARBY;
        sfx(SFX_DECIDE);
    } else {
        if (s_res_n > 0) do_launch(s_results[s_res_sel]);
    }
}

static void input_keyboard(u32 k, u32 rep) {
    if (s_kb_mode == KB_SEARCH) {
        if ((rep & KEY_DOWN) && s_res_sel + 1 < s_res_n) { s_res_sel++; sfx(SFX_TICK); }
        if ((rep & KEY_UP) && s_res_sel > 0) { s_res_sel--; sfx(SFX_TICK); }
        if ((k & KEY_A) && s_res_n) do_launch(s_results[s_res_sel]);
        if ((k & KEY_Y) && s_res_n) open_options(s_results[s_res_sel]);
    } else if (k & KEY_A) {
        kb_done();
        return;
    }
    if (k & KEY_START) { kb_done(); return; }
    if (k & KEY_X) { s_kb_sym = !s_kb_sym; sfx(SFX_PAGE); }
    if (k & KEY_B) {
        if (s_kb[0] && s_kb_mode == KB_SEARCH) { kb_backspace(); sfx(SFX_KEY); }
        else { s_scr = s_kb_return; sfx(SFX_BACK); }
    }
}

static void tap_keyboard(int x, int y) {
    for (int r = 0; r < 5; r++) {
        for (int col = 0; col < 10; col++) {
            int kx, ky, kw;
            kb_key_rect(r, col, &kx, &ky, &kw);
            if (x >= kx && x < kx + kw && y >= ky && y < ky + KB_KH) {
                if (r == 4 && col >= 3 && col <= 6) kb_type(" ");
                else if (r == 4 && (col == 7 || col == 8)) { kb_backspace(); sfx(SFX_KEY); }
                else if (r == 4 && col == 9) kb_done();
                else if (r == 4 && col == 2 && s_kb_mode != KB_WIFIPASS) { s_kb_sym = !s_kb_sym; sfx(SFX_PAGE); }
                else kb_type(kb_key(r, col));
                return;
            }
        }
    }
}

static void input_login(u32 k, u32 rep) {
    int n = login_items();
    if ((rep & KEY_RIGHT) && s_login_sel < n - 1) { s_login_sel++; s_login_wp_wait = 12; login_accent(); sfx(SFX_TICK); }
    if ((rep & KEY_LEFT) && s_login_sel > 0) { s_login_sel--; s_login_wp_wait = 12; login_accent(); sfx(SFX_TICK); }
    if (k & KEY_A) login_activate();
}

static void input_settings(u32 k, u32 rep) {
    if ((rep & KEY_DOWN) && s_set_pos < set_tab_n(s_set_tab) - 1) { set_go(s_set_tab, s_set_pos + 1); sfx(SFX_TICK); }
    if ((rep & KEY_UP) && s_set_pos > 0) { set_go(s_set_tab, s_set_pos - 1); sfx(SFX_TICK); }
    if (k & (KEY_R | KEY_L)) { set_go(s_set_tab + ((k & KEY_R) ? 1 : -1), 0); s_set_scroll = 0; sfx(SFX_TICK); return; }
    if (k & (KEY_A | KEY_RIGHT)) setting_change(s_set_sel, 1);
    else if (k & KEY_LEFT) setting_change(s_set_sel, -1);
    if ((k & KEY_Y) && s_set_sel == SET_SPLASH) {
        play_splash();
        return;
    }
    if (k & (KEY_B | KEY_START)) { build_home(); go_home(); }
}

static void input_notes(u32 k, u32 rep) {
    if (rep & KEY_DOWN) s_notes_scroll++;
    if ((rep & KEY_UP) && s_notes_scroll > 0) s_notes_scroll--;
    if (k & KEY_X) open_keyboard(KB_NOTE, "");
    if (k & KEY_B) { s_scr = s_kb_return == SCR_ALL ? SCR_ALL : SCR_HOME; sfx(SFX_BACK); }
}

static void input_options(u32 k, u32 rep) {
    if ((rep & KEY_DOWN) && s_opt_sel < s_opt_n - 1) { s_opt_sel++; sfx(SFX_TICK); }
    if ((rep & KEY_UP) && s_opt_sel > 0) { s_opt_sel--; sfx(SFX_TICK); }
    if (s_opt_rows[s_opt_sel] == OPT_SLOT && (k & (KEY_LEFT | KEY_RIGHT))) options_activate((k & KEY_LEFT) ? -1 : 1);
    if (k & KEY_A) options_activate(1);
    if (k & (KEY_B | KEY_Y)) { s_ovl = OVL_NONE; sfx(SFX_BACK); }
}

static void input_dialog(u32 k) {
    if (s_dlg_nbtn == 2 && (k & (KEY_LEFT | KEY_RIGHT))) { s_dlg_sel ^= 1; sfx(SFX_TICK); }
    if (k & KEY_A) {
        if (s_dlg_kind == DLG_DELETE && s_dlg_sel == 0) {
            s_ovl = OVL_NONE;
            delete_current();
            return;
        }
        if (s_dlg_kind == DLG_DELFOLDER) {
            s_ovl = OVL_NONE;
            if (s_dlg_sel == 0) {
                char fp[MAX_PATH_LEN];
                folder_pic_path(coll_name(s_fold_sel), fp, sizeof(fp));
                remove(fp);
                s_fold_pic_gen++;
                coll_delete(s_fold_sel);
                coll_save();
                if (s_fold_sel > 0 && s_fold_sel >= coll_count()) s_fold_sel--;
                build_tabs();
                sfx(SFX_BACK);
            }
            return;
        }
        if (s_dlg_kind == DLG_WPRESET) {
            s_ovl = OVL_NONE;
            if (s_dlg_sel == 0) {
                // kept as wallpaper.bin.old (nothing is lost), the theme's background comes back
                char p[MAX_PATH_LEN], old[MAX_PATH_LEN + 8];
                profiles_file(s_cur_profile >= 0 ? s_cur_profile : 0, "wallpaper.bin", p, sizeof(p));
                snprintf(old, sizeof(old), "%s.old", p);
                remove(old);
                if (rename(p, old) != 0) remove(p);
                ui_wallpaper_load(NULL);
                s_login_wp_slot = -2;
                s_force_redraw = true;
                toast(tr(S_WPRESET_DONE, L()));
                sfx(SFX_DECIDE);
            } else sfx(SFX_BACK);
            return;
        }
        if (s_dlg_kind == DLG_WIFI) {
            s_ovl = OVL_NONE;
            if (s_dlg_sel == 0) { pad_wifi_retry(); sfx(SFX_DECIDE); }
            else {
                // leave the screen that needs Wi-Fi, then Settings > Wi-Fi
                if (s_scr == SCR_PAD) close_pad();
                else if (s_scr == SCR_STORE) close_store();
                else if (s_scr == SCR_NEARBY) close_nearby();
                s_scr = SCR_SETTINGS;
                set_go(4, 1);
                open_wifi();
            }
            return;
        }
        if (s_dlg_kind == DLG_OLDMENU) {
            s_ovl = OVL_NONE;
            if (s_dlg_sel == 0) restore_old_menu();
            else sfx(SFX_BACK);
            return;
        }
        if (s_dlg_kind == DLG_DELPROF) {
            s_ovl = OVL_NONE;
            if (s_dlg_sel == 0) delete_profile();
            else sfx(SFX_BACK);
            return;
        }
        s_ovl = (s_dlg_kind == DLG_DELETE) ? OVL_OPTIONS : OVL_NONE;
        sfx(SFX_BACK);
    }
    if (k & KEY_B) { s_ovl = (s_dlg_kind == DLG_DELETE) ? OVL_OPTIONS : OVL_NONE; sfx(SFX_BACK); }
}

// ---------------------------------------------------------------------------
// PIN entry (overlay on the lower screen)
// ---------------------------------------------------------------------------
static void open_pin(PinMode mode, int slot) {
    s_pin_mode = mode;
    s_pin_slot = slot;
    s_pin_n = 0;
    s_pin_shake = 0;
    s_pin_return = s_scr;
    s_ovl = OVL_PIN;
    sfx(SFX_MODAL);
}

static void pin_wrong(StrId msg) {
    s_pin_n = 0;
    s_pin_shake = 18;
    toast(tr(msg, L()));
    sfx(SFX_ERROR);
}

static void profile_login(int slot);

static void pin_complete(void) {
    switch (s_pin_mode) {
        case PIN_LOGIN:
            if (!profiles_check_pin(s_pin_slot, s_pin, PIN_LEN)) { pin_wrong(S_WRONG_PIN); return; }
            s_ovl = OVL_NONE;
            sfx(SFX_DECIDE);
            profile_login(s_pin_slot);
            return;
        case PIN_SET_NEW:
            memcpy(s_pin_first, s_pin, PIN_LEN);
            s_pin_mode = PIN_SET_CONFIRM;
            s_pin_n = 0;
            sfx(SFX_DECIDE);
            return;
        case PIN_SET_CONFIRM:
            if (memcmp(s_pin_first, s_pin, PIN_LEN) != 0) { s_pin_mode = PIN_SET_NEW; pin_wrong(S_PIN_MISMATCH); return; }
            profiles_set_pin(s_pin_slot, s_pin, PIN_LEN);
            s_ovl = OVL_NONE;
            toast(tr(S_PIN_SET, L()));
            sfx(SFX_DECIDE);
            return;
        case PIN_UNLOCK_REMOVE:
            if (!profiles_check_pin(s_pin_slot, s_pin, PIN_LEN)) { pin_wrong(S_WRONG_PIN); return; }
            profiles_set_pin(s_pin_slot, NULL, 0);
            s_ovl = OVL_NONE;
            toast(tr(S_PIN_REMOVED, L()));
            sfx(SFX_TOGGLE);
            return;
        case PIN_UNLOCK_DELETE: {
            if (!profiles_check_pin(s_pin_slot, s_pin, PIN_LEN)) { pin_wrong(S_WRONG_PIN); return; }
            s_ovl = OVL_NONE;
            char msg[160];
            snprintf(msg, sizeof(msg), tr(S_DELPROF_MSG, L()), s_cfg.profile_name);
            show_dialog(DLG_DELPROF, ICON_WARNING_L, tr(S_DELPROF_TITLE, L()), msg);
            return;
        }
    }
}

static void input_pin(u32 k) {
    if (k & KEY_B) { s_ovl = OVL_NONE; sfx(SFX_BACK); return; }
    static const struct { u32 key; uint8_t code; } map[] = {
        { KEY_UP, PIN_KEY_UP }, { KEY_DOWN, PIN_KEY_DOWN }, { KEY_LEFT, PIN_KEY_LEFT }, { KEY_RIGHT, PIN_KEY_RIGHT },
        { KEY_A, PIN_KEY_A }, { KEY_X, PIN_KEY_X }, { KEY_Y, PIN_KEY_Y }, { KEY_L, PIN_KEY_L }, { KEY_R, PIN_KEY_R },
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (!(k & map[i].key) || s_pin_n >= PIN_LEN) continue;
        s_pin[s_pin_n++] = map[i].code;
        sfx(SFX_KEY);
        if (s_pin_n == PIN_LEN) { pin_complete(); return; }
    }
}

static void render_pin(void) {
    Canvas *c = &g_bot;
    ui_scrim(c, 18);
    int sh = 0;
    if (s_pin_shake > 0) { sh = ((s_pin_shake & 2) ? 5 : -5) * s_pin_shake / 18; s_pin_shake--; }
    int x = 28 + sh, y = 34, w = 200, h = 118;
    gfx_shadow(c, x, y, w, h, 10, 5, 34);
    gfx_round_rect(c, x, y, w, h, 10, T.panel, 32);
    gfx_icon_centered(c, ICON_LOCK, x + w / 2, y + 18, T.accent);
    StrId title = s_pin_mode == PIN_SET_NEW ? S_NEW_PIN : s_pin_mode == PIN_SET_CONFIRM ? S_CONFIRM_PIN : S_ENTER_PIN;
    gfx_text_align(c, FONT_BOLD, x + 6, y + 32, w - 12, tr(title, L()), T.text, ALIGN_CENTER);
    for (int i = 0; i < PIN_LEN; i++) {
        int cx = x + w / 2 - 36 + i * 24, cy = y + 70;
        if (i < s_pin_n) gfx_circle(c, cx, cy, 7, T.accent, 32);
        else gfx_ring(c, cx, cy, 7, 2, T.text3, 32);
    }
    gfx_text_align(c, FONT_SMALL, x + 6, y + 92, w - 12, tr(S_PIN_HINT, L()), T.text2, ALIGN_CENTER);
    Hint hh[1] = { { BTN_B, tr(S_CANCEL, L()), KEY_B } };
    s_hit_n = ui_hint_bar(c, hh, 1, s_hits);
}

// Touch: taps (no movement) and horizontal / vertical drags
static void handle_tap(int x, int y) {
    // hint bar buttons
    for (int i = 0; i < s_hit_n; i++) {
        HitRect *h = &s_hits[i];
        if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) {
            AppInput fake = { (u32)h->key, (u32)h->key, 0, 0, false, 0, 0, false, 0, 0 };
            app_frame(&fake);
            return;
        }
    }
    if (s_ovl == OVL_DIALOG) {
        int x0 = 18, w = 220, by = 30 + 132 - 28;
        if (y >= by && y < by + 28) {
            if (s_dlg_nbtn == 2) s_dlg_sel = (x >= x0 + w / 2) ? 1 : 0;
            input_dialog(KEY_A);
        }
        return;
    }
    if (s_ovl == OVL_MENU) {
        int h, y0 = menu_rows_y0(&h);
        if (x < 16 || x > 240 || y < y0 || y > y0 + h) { input_menu(KEY_B, 0); return; }
        int row = (y - (y0 + 31)) / menu_rh();
        if (row >= 0 && row < menu_vis()) { s_menu_sel = s_menu_scroll + row; menu_activate(); }
        return;
    }
    if (s_ovl == OVL_OPTIONS) {
        int rh = opt_rh(), h = 30 + opt_vis() * rh + 6;
        int y0 = (HINT_BAR_Y - h) / 2 + 2;
        if (y0 < 4) y0 = 4;
        if (x < 16 || x > 240 || y < y0 || y > y0 + h) { s_ovl = OVL_NONE; sfx(SFX_BACK); return; }
        int row = (y - (y0 + 31)) / rh;
        if (row >= 0 && row < opt_vis()) row += s_opt_scroll; else row = -1;
        if (row >= 0 && row < s_opt_n) {
            s_opt_sel = row;
            int r = s_opt_rows[row];
            if (r == OPT_SLOT) options_activate(x < 206 ? -1 : 1);
            else options_activate(1);
        }
        return;
    }
    switch (s_scr) {
        case SCR_HOME:
            if (y >= TILE_Y && y < TILE_Y + TILE) {
                for (int i = 0; i < s_home_n; i++) {
                    int tx = home_tile_x(i);
                    if (x >= tx && x < tx + TILE) {
                        if (s_home_focus == 0 && i == s_home_sel) home_activate();
                        else { s_home_focus = 0; s_home_sel = i; sfx(SFX_TICK); }
                        return;
                    }
                }
            } else if (y >= HB_Y - HB_R - 4 && y <= HB_Y + HB_R + 4) {
                for (int b = 0; b < HB_COUNT; b++) {
                    if (hb_visible(b) && abs(x - btn_cx(b)) <= hb_r() + 2) {
                        s_home_focus = 1;
                        s_home_btn = b;
                        home_button(b);
                        return;
                    }
                }
            }
            break;
        case SCR_ALL:
            if (y >= 22 && y < 42) {
                // tabs
                int tx = 23 - s_tab_scroll;
                for (int i = 0; i < s_tab_n; i++) {
                    int w = gfx_text_width(FONT_SMALL, tab_label(s_tabs[i])) + 14;
                    if (x >= 23 && x < 232 && x >= tx && x < tx + w) {
                        if (i != s_tab) { s_tab = i; s_view_n = 0; build_view(); s_grid_sel = 0; s_grid_scroll = s_grid_scroll_target = 0; sfx(SFX_PAGE); }
                        return;
                    }
                    tx += w + 3;
                }
                if (x < 22) { AppInput f = { KEY_L, KEY_L, 0, 0, false, 0, 0, false, 0, 0 }; app_frame(&f); }
                if (x > 232) { AppInput f = { KEY_R, KEY_R, 0, 0, false, 0, 0, false, 0, 0 }; app_frame(&f); }
            } else if (y >= GRID_Y0 && y < GRID_BOTTOM) {
                int col = (x - GRID_X0) / GRID_STRIDE;
                int row = (y - GRID_Y0 + s_grid_scroll) / GRID_STRIDE;
                if (col >= 0 && col < GRID_COLS) {
                    int i = row * GRID_COLS + col;
                    if (i >= 0 && i < s_view_n) {
                        if (s_pick_coll >= 0) { s_grid_sel = i; input_all(KEY_A, 0); }
                        else if (i == s_grid_sel) do_launch(s_view[i]);
                        else { s_grid_sel = i; sfx(SFX_TICK); }
                    }
                }
            }
            break;
        case SCR_KEYBOARD:
            tap_keyboard(x, y);
            break;
        case SCR_FOLDERS:
            for (int i = 0; i < fold_items(); i++) {
                int row = i / FOLD_COLS - s_fold_scroll, col = i % FOLD_COLS;
                int fx = FOLD_X0 + col * FOLD_STRIDE, fy = FOLD_Y0 + row * (FOLD_STRIDE + 12);
                if (x >= fx && x < fx + FOLD_TILE && y >= fy && y < fy + FOLD_TILE && fy < HINT_BAR_Y) {
                    if (i == s_fold_sel) input_folders(KEY_A, 0);
                    else { s_fold_sel = i; sfx(SFX_TICK); }
                    return;
                }
            }
            break;
        case SCR_LOGIN:
            for (int i = 0; i < login_items(); i++) {
                int cx, cy;
                login_tile_pos(i, &cx, &cy);
                if (abs(x - cx) <= 28 && y >= cy - 30 && y <= cy + 46) {
                    if (i == s_login_sel) login_activate();
                    else { s_login_sel = i; s_login_wp_wait = 6; login_accent(); sfx(SFX_TICK); }
                    return;
                }
            }
            break;
        case SCR_SETTINGS: {
            if (y >= 20 && y < 41) {                          // tabs
                for (int t = 0; t < SET_TABS; t++)
                    if (x >= s_set_tab_x[t] && x < s_set_tab_x[t + 1]) {
                        if (t != s_set_tab) { set_go(t, 0); s_set_scroll = 0; sfx(SFX_TICK); }
                        break;
                    }
                break;
            }
            int p = s_set_scroll + (y - 44) / 25;
            if (y >= 44 && y < HINT_BAR_Y && p >= 0 && p < set_tab_n(s_set_tab)) {
                if (p == s_set_pos) setting_change(s_set_sel, 1);
                else { set_go(s_set_tab, p); sfx(SFX_TICK); }
            }
            break;
        }
        case SCR_PHOTOS:
            tap_photos(x, y);
            break;
        case SCR_STORE: store_touch(x, y); break;
        case SCR_NEARBY: nearby_touch(x, y); break;
        case SCR_APPS: {
            int row = s_ap_scroll + (y - AP_Y0) / AP_RH;
            if (s_ap_stage == 2 && y >= AP_Y0 && y < AP_Y0 + AP_VIEW * AP_RH && row < s_ap_n) {
                if (row == s_ap_sel) { s_ap_on[row] ^= 1; s_ap_ver++; sfx(SFX_TOGGLE); }
                else { s_ap_sel = row; sfx(SFX_TICK); }
            }
            break;
        }
        case SCR_CHEATS: {
            int row = s_ch_scroll + (y - CH_Y0) / CH_RH;
            if (y >= CH_Y0 && y < CH_Y0 + CH_VIEW * CH_RH && row < cheats_count()) {
                const CheatRow *r = cheats_row(row);
                if (r && !r->folder) {
                    if (row == s_ch_sel) { cheats_toggle(row); s_ch_ver++; sfx(SFX_TOGGLE); }
                    else { s_ch_sel = row; sfx(SFX_TICK); }
                }
            }
            break;
        }
        case SCR_WIFI: tap_wifi(x, y); break;
        case SCR_HOMEEDIT: {
            int row = s_he_scroll + (y - 26) / 26;
            if (y >= 26 && y < HINT_BAR_Y && row >= 0 && row < he_count()) {
                if (row == s_he_sel) he_change(he_row(row), 1);
                else { s_he_sel = row; sfx(SFX_TICK); }
            }
            break;
        }
        default:
            break;
    }
}

static void handle_touch(const AppInput *in) {
    if (in->down & KEY_TOUCH) {
        s_fx_on = true;
        s_fx_x = in->tx;
        s_fx_y = in->ty;
        s_fx_t = 0;
        s_touch_active = true;
        s_touch_moved = false;
        s_touch_x0 = s_touch_x = in->tx;
        s_touch_y0 = s_touch_y = in->ty;
        s_drag_origin_scroll = (s_scr == SCR_HOME) ? s_home_scroll : s_grid_scroll;
        return;
    }
    if (s_touch_active && (in->held & KEY_TOUCH) && in->touching) {
        s_touch_x = in->tx;
        s_touch_y = in->ty;
        int dx = s_touch_x - s_touch_x0, dy = s_touch_y - s_touch_y0;
        if (!s_touch_moved && (abs(dx) > 6 || abs(dy) > 6)) { s_touch_moved = true; s_fx_on = false; }
        if (s_touch_moved && s_ovl == OVL_NONE) {
            if (s_scr == SCR_HOME && s_touch_y0 >= TILE_Y - 10 && s_touch_y0 < TILE_Y + TILE + 10) {
                s_home_dragging = true;
                s_home_scroll = s_drag_origin_scroll - dx;
                s_home_scroll_target = s_home_scroll;
            } else if (s_scr == SCR_ALL && s_touch_y0 >= GRID_Y0) {
                s_grid_dragging = true;
                s_grid_scroll = s_drag_origin_scroll - dy;
                if (s_grid_scroll < 0) s_grid_scroll = 0;
                s_grid_scroll_target = s_grid_scroll;
            }
        }
        return;
    }
    if (s_touch_active && (in->up & KEY_TOUCH)) {
        s_touch_active = false;
        if (s_fx_on && !s_touch_moved) s_fx_t = 1;   // the tap was taken: ring
        s_fx_on = false;
        if (s_home_dragging) {
            s_home_dragging = false;
            // select the tile closest to the left edge of the visible row
            int i = (s_home_scroll + (TILE + TILE_GAP) / 2) / (TILE + TILE_GAP);
            if (i < 0) i = 0;
            if (i >= s_home_n) i = s_home_n - 1;
            if (s_home_sel < i || s_home_sel > i + 2) s_home_sel = i;
            s_home_focus = 0;
            sfx(SFX_TICK);
        } else if (s_grid_dragging) {
            s_grid_dragging = false;
            int first_row = (s_grid_scroll + GRID_STRIDE / 2) / GRID_STRIDE;
            int sel_row = s_grid_sel / GRID_COLS;
            if (sel_row < first_row || sel_row > first_row + 1) {
                int ns = first_row * GRID_COLS + (s_grid_sel % GRID_COLS);
                if (ns >= s_view_n) ns = s_view_n - 1;
                if (ns >= 0) s_grid_sel = ns;
            }
        } else if (!s_touch_moved) {
            handle_tap(s_touch_x0, s_touch_y0);
        }
    }
}

static uint32_t fnv(uint32_t h, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

// anim = false leaves out the bottom-screen-only animations (scrolling, toast, cursor blink),
// so the top screen is redrawn only when something it shows changed
static uint32_t compute_sig_ex(bool anim) {
    int v[80];
    int n = 0;
    v[n++] = s_scr; v[n++] = s_ovl; v[n++] = s_home_sel; v[n++] = s_home_focus; v[n++] = s_home_btn;
    v[n++] = anim ? s_home_scroll : 0; v[n++] = s_home_n; v[n++] = s_grid_sel; v[n++] = anim ? s_grid_scroll : 0; v[n++] = s_tab;
    v[n++] = s_view_n; v[n++] = s_tab_n; v[n++] = s_set_sel; v[n++] = s_set_scroll + s_set_tab * 64; v[n++] = s_opt_sel;
    v[n++] = s_opt_n; v[n++] = s_dlg_sel; v[n++] = s_dlg_kind; v[n++] = s_res_sel; v[n++] = s_res_n;
    v[n++] = s_notes_scroll; v[n++] = anim ? (s_toast_t > 16 ? 17 : s_toast_t) : 0; v[n++] = library_count();
    v[n++] = slot1_present(); v[n++] = (int)covers_generation(); v[n++] = (int)library_generation();
    v[n++] = (anim && s_scr == SCR_KEYBOARD) ? ((s_frame / 20) & 1) : 0; v[n++] = s_launching;
    v[n++] = (int)ui_battery_state(); v[n++] = library_scanning();
    v[n++] = s_kb_sym; v[n++] = s_he_sel; v[n++] = s_he_scroll; v[n++] = s_menu_ver; v[n++] = s_menu_scroll; v[n++] = s_ph_sel; v[n++] = s_ph_stage; v[n++] = s_ph_loaded; v[n++] = home_clock_on(); v[n++] = s_fold_sel; v[n++] = s_fold_scroll; v[n++] = s_menu_sel; v[n++] = s_menu_n; v[n++] = s_pick_coll;
    v[n++] = anim ? (s_fx_on ? 1 + s_fx_x * 256 + s_fx_y : 0) : 0; v[n++] = anim ? s_fx_t : 0; v[n++] = touchcal_sig(); v[n++] = anim ? s_kp_t : 0;
    v[n++] = s_ch_sel; v[n++] = s_ch_scroll; v[n++] = s_ch_ver; v[n++] = s_opt_scroll; v[n++] = s_ap_sel; v[n++] = s_ap_scroll; v[n++] = s_ap_stage; v[n++] = s_ap_ver;
    v[n++] = s_nb_ver + (s_scr == SCR_NEARBY ? pad_status() * 7 : 0);
    v[n++] = anim_sig();
    v[n++] = s_store_ver + s_so_sel * 7 + s_so_scroll * 131 + s_so_tab * 1031; v[n++] = s_scr == SCR_STORE ? pad_status() * 1000 + xfer_percent() + (xfer_name()[0] ? 500 : 0) : 0;
    v[n++] = (int)coll_version(); v[n++] = s_scr == SCR_PAD ? (int)(anim ? pad_sig() : pad_sig_top()) : 0; v[n++] = s_login_sel; v[n++] = s_login_wp_slot; v[n++] = s_cur_profile; v[n++] = s_pin_n; v[n++] = s_pin_mode;
    v[n++] = s_scr == SCR_WIFI ? s_wf_ver * 131 + s_wf_sel * 7 + s_wf_scroll + s_wf_state * 1000 + (s_wf_state != WF_LIST ? (s_frame / 4) : 0) : 0;
    v[n++] = s_ss_on ? (int)(s_ss_frames / 2) + 1 : 0;
    v[n++] = s_ph_ver * 7 + s_crop_kind + (int)s_fold_pic_gen * 131;
    v[n++] = s_lv_t > 0 ? ((s_lv_kind * 64 + s_lv_val) * 40 + (s_lv_t >= 16 ? 32 : s_lv_t * 2)) : 0;
    v[n++] = anim ? s_pin_shake : 0; v[n++] = profiles_count(); v[n++] = profiles_global()->login;
    { const ProfileInfo *pi = profiles_get(s_cur_profile); v[n++] = pi ? pi->color * 16 + pi->pin_len : -1; }
    time_t now = time(NULL);
    v[n++] = (int)(now / 60);
    uint32_t h = fnv(2166136261u, v, sizeof(int) * (size_t)n);
    h = fnv(h, &s_cfg, sizeof(s_cfg));
    h = fnv(h, s_kb, strlen(s_kb));
    if (anim) h = fnv(h, s_toast, strlen(s_toast));
    return h;
}
static uint32_t compute_sig(void) { return compute_sig_ex(true); }


#if defined(HL_PROFILE) && defined(ARM9)
// Profiling build (make PROFILE=1): per-phase timings written to sd:/_hyper/prof.txt
static inline u16 prof_now(void) { return TIMER_DATA(2); }
static u32 s_prof[8], s_prof_max[8], s_prof_n[8];
static void prof_add(int k, u16 t0) {
    u16 d = (u16)(prof_now() - t0);
    s_prof[k] += d; s_prof_n[k]++; if (d > s_prof_max[k]) s_prof_max[k] = d;
    if (k == 5 && d > 8727) { s_prof_n[6]++; s_prof[6] += d; }   // frames longer than 16.7 ms
}
static void prof_dump(void) {
    char p[64]; snprintf(p, sizeof(p), "%s/_hyper/prof.txt", s_drv);
    FILE *f = fopen(p, "w"); if (!f) return;
    static const char *names[8] = { "render_top", "render_bot", "cache_copy", "present_full", "present_rows", "frame_total", "slow_frames", "-" };
    for (int i = 0; i < 7; i++) {
        unsigned long avg = s_prof_n[i] ? (unsigned long)((u64)s_prof[i] * 64000ull / 33514ull / s_prof_n[i]) : 0;
        unsigned long mx = (unsigned long)((u64)s_prof_max[i] * 64000ull / 33514ull);
        fprintf(f, "%-13s n=%5lu avg=%6luus max=%6luus\n", names[i], (unsigned long)s_prof_n[i], avg, mx);
    }
    fclose(f);
}
#define PROF_T0() u16 _pt = prof_now()
#define PROF(k) prof_add(k, _pt); _pt = prof_now()
#else
#define PROF_T0() do {} while (0)
#define PROF(k) do {} while (0)
#endif

static void save_rings(Canvas *dst, const Canvas *src, const UiRing *rings, int n) {
    for (int i = 0; i < n; i++) gfx_copy_rect(dst, src, rings[i].x, rings[i].y, rings[i].w, rings[i].h);
}

// Idle time: load the covers around what is on screen, nearest first, so scrolling finds them
// ready (no redraw is asked for; they show when their tile is drawn)
static void prefetch_covers(void) {
    if (!s_cfg.covers || s_idle < 6 || s_ovl != OVL_NONE || s_launching || !covers_budget_left()) return;
    if (s_scr == SCR_ALL && s_view_n > 0) {
        int first = (s_grid_scroll / GRID_STRIDE) * GRID_COLS;
        int last = first + 3 * GRID_COLS;             // three rows are visible
        for (int d = 0; d < 4 * GRID_COLS && covers_budget_left(); d++) {
            int a = last + d, b = first - 1 - d;
            if (a < s_view_n) covers_prefetch(s_view[a], library_get(s_view[a]));
            if (b >= 0 && covers_budget_left()) covers_prefetch(s_view[b], library_get(s_view[b]));
        }
    } else if (s_scr == SCR_HOME) {
        for (int i = 0; i < s_home_n && covers_budget_left(); i++)
            if (s_home[i] >= 0) covers_prefetch(s_home[i], library_get(s_home[i]));
    }
}

// ---------------------------------------------------------------------------
// Touch screen calibration (Settings > Touch screen, include/touchcal.h)
// ---------------------------------------------------------------------------
static int      s_tc_stage;                 // 0..4: targets, TOUCHCAL_POINTS: test
static int      s_tc_rx[TOUCHCAL_POINTS], s_tc_ry[TOUCHCAL_POINTS];
static int32_t  s_tc_sx, s_tc_sy, s_tc_n;   // samples of the current press
static bool     s_tc_down;
static TouchCal s_tc_new;
static int      s_tc_px = -1, s_tc_py = -1; // test stage: where the new calibration puts the stylus

static int touchcal_sig(void) {
    return s_scr == SCR_TOUCHCAL ? 1 + s_tc_stage * 7 + (s_tc_down ? 3 : 0) + (s_tc_px + 1) * 1000 + (s_tc_py + 1) * 300000 : 0;
}

static void open_touchcal(void) {
    s_scr = SCR_TOUCHCAL;
    s_tc_stage = 0;
    s_tc_down = false;
    s_tc_n = 0;
    s_tc_px = s_tc_py = -1;
    sfx(SFX_DECIDE);
}

static void touchcal_touch(const AppInput *in) {
    if (s_tc_stage < TOUCHCAL_POINTS) {
        if (in->touching && (in->rawx || in->rawy)) {
            if (!s_tc_down) { s_tc_down = true; s_tc_sx = s_tc_sy = 0; s_tc_n = 0; }
            if (++s_tc_n > 3) { s_tc_sx += in->rawx; s_tc_sy += in->rawy; }   // the first readings settle
        } else if (s_tc_down) {
            s_tc_down = false;
            int n = s_tc_n - 3;
            if (n < 1) { sfx(SFX_ERROR); return; }                        // too quick: same target again
            s_tc_rx[s_tc_stage] = s_tc_sx / n;
            s_tc_ry[s_tc_stage] = s_tc_sy / n;
            sfx(SFX_TICK);
            if (++s_tc_stage == TOUCHCAL_POINTS) {
                int err = 0;
                if (touchcal_fit(s_tc_rx, s_tc_ry, &s_tc_new, &err)) { sfx(SFX_DECIDE); }
                else { toast(tr(S_TC_FAIL, L())); sfx(SFX_ERROR); s_tc_stage = 0; }
            }
        }
        return;
    }
    // test: the dot follows the stylus with the new calibration; the hint bar still answers
    if (in->touching && (in->rawx || in->rawy)) {
        touchcal_apply(&s_tc_new, in->rawx, in->rawy, &s_tc_px, &s_tc_py);
        s_tc_down = true;
    } else if (s_tc_down) {
        s_tc_down = false;
        for (int i = 0; i < s_hit_n; i++)
            if (s_tc_px >= s_hits[i].x && s_tc_px < s_hits[i].x + s_hits[i].w && s_tc_py >= s_hits[i].y && s_tc_py < s_hits[i].y + s_hits[i].h) {
                AppInput k;
                memset(&k, 0, sizeof(k));
                k.down = k.repeat = (u32)s_hits[i].key;
                app_frame(&k);
                return;
            }
    }
}

static void input_touchcal(u32 k) {
    if (s_tc_stage < TOUCHCAL_POINTS) {
        if (k & KEY_B) { s_scr = SCR_SETTINGS; sfx(SFX_BACK); }
        if (k & KEY_Y) {                                    // back to the system calibration
            touchcal_remove(s_drv);
            toast(tr(S_TC_OFF, L()));
            s_scr = SCR_SETTINGS;
            sfx(SFX_BACK);
        }
        return;
    }
    if (k & KEY_A) {
        if (touchcal_save(s_drv, &s_tc_new)) { toast(tr(S_TC_SAVED, L())); sfx(SFX_DECIDE); }
        else sfx(SFX_ERROR);
        s_scr = SCR_SETTINGS;
    } else if (k & KEY_B) {
        open_touchcal();
    }
}

static void render_top_touchcal(void) {
    hero_simple(&g_top, ICON_GAMEPAD_L, T.accent, tr(S_SET_TOUCH, L()), tr(S_DESC_TOUCH, L()), NULL);
}

static void render_touchcal(void) {
    Canvas *c = &g_bot;
    gfx_rect(c, 0, 0, 256, 192, T.bg);
    if (s_tc_stage < TOUCHCAL_POINTS) {
        int x, y;
        touchcal_target(s_tc_stage, &x, &y);
        gfx_circle(c, x, y, 10, T.accent, s_tc_down ? 32 : 12);
        gfx_ring(c, x, y, 10, 1, T.accent, 32);
        gfx_rect(c, x - 14, y, 29, 1, T.text);
        gfx_rect(c, x, y - 14, 1, 29, T.text);
        char msg[96];
        snprintf(msg, sizeof(msg), tr(S_TC_TAP, L()), s_tc_stage + 1);
        int ty = (y < 96) ? 112 : 56;
        gfx_text_align(c, FONT_BODY, 0, ty, 256, msg, T.text, ALIGN_CENTER);
        for (int i = 0; i < TOUCHCAL_POINTS; i++)
            gfx_circle(c, 128 - 2 * 8 + i * 8, ty + 22, 2, i < s_tc_stage ? T.accent : T.text3, 32);
        Hint hh[2] = { { BTN_B, tr(S_BACK, L()), KEY_B }, { BTN_Y, tr(S_TC_RESET, L()), KEY_Y } };
        s_hit_n = ui_hint_bar(c, hh, 2, s_hits);
        return;
    }
    gfx_text_align(c, FONT_BODY, 0, 70, 256, tr(S_TC_TEST, L()), T.text, ALIGN_CENTER);
    for (int i = 0; i < TOUCHCAL_POINTS; i++) {           // the targets, for checking
        int x, y;
        touchcal_target(i, &x, &y);
        gfx_ring(c, x, y, 6, 1, T.text3, 32);
    }
    if (s_tc_down && s_tc_px >= 0) {
        gfx_circle(c, s_tc_px, s_tc_py, 4, T.accent, 32);
        gfx_ring(c, s_tc_px, s_tc_py, 8, 1, T.accent, 20);
    }
    Hint hh[2] = { { BTN_B, tr(S_TC_RETRY, L()), KEY_B }, { BTN_A, tr(S_OK, L()), KEY_A } };
    s_hit_n = ui_hint_bar(c, hh, 2, s_hits);
}

static void app_render(void) {
    if (!s_cache_top.px) {
        gfx_canvas_init(&s_cache_top, s_cache_top_px, SCREEN_WIDTH, SCREEN_HEIGHT);
        gfx_canvas_init(&s_cache_bot, s_cache_bot_px, SCREEN_WIDTH, SCREEN_HEIGHT);
    }
    uint32_t sig = compute_sig(), sig_top = compute_sig_ex(false);
    bool top = s_force_redraw || sig_top != s_last_sig_top;
    bool bot = s_force_redraw || sig != s_last_sig;
    PROF_T0();
    if (top) {
        ui_rings_reset(&g_top);
        render_top();
        save_rings(&s_cache_top, &g_top, ui_rings_top, ui_rings_top_n); // pixels under the selection frames
    } else {
        save_rings(&g_top, &s_cache_top, ui_rings_top, ui_rings_top_n);
    }
    PROF(0);
    if (bot) {
        ui_rings_reset(&g_bot);
        render_bottom();
        if (s_fx_on && s_scr != SCR_TOUCHCAL) {                   // under the stylus
            gfx_circle(&g_bot, s_fx_x, s_fx_y, 9, T.accent, 10);
            gfx_ring(&g_bot, s_fx_x, s_fx_y, 10, 1, T.accent, 26);
        } else if (s_fx_t > 0) {                                  // the tap was taken
            int a = 28 - s_fx_t * 3;
            if (a > 0) gfx_ring(&g_bot, s_fx_x, s_fx_y, 10 + s_fx_t * 2, 2, T.accent, a);
        }
        if (s_kp_t > 0 && s_kp_scr == (int)s_scr && s_kp_ovl == (int)s_ovl) {   // A on the selected element
            int t = s_kp_t;
            int dark = t <= 6 ? 12 - t * 2 : 0;
            if (dark > 0) gfx_round_rect(&g_bot, s_kp_x, s_kp_y, s_kp_w, s_kp_h, s_kp_r, C8(0, 0, 0), dark);
            int a = 30 - t * 2;
            if (a > 0) gfx_round_ring(&g_bot, s_kp_x - 4 - t, s_kp_y - 4 - t, s_kp_w + 8 + 2 * t, s_kp_h + 8 + 2 * t, s_kp_r + 4 + t, 2, T.accent, a);
            int rr = 5 + t * 2, mx = (s_kp_w < s_kp_h ? s_kp_w : s_kp_h) / 2;
            if (rr > mx) rr = mx;
            if (t < 12) gfx_circle(&g_bot, s_kp_x + s_kp_w / 2, s_kp_y + s_kp_h / 2, rr, T.accent, 14 - t);
        }
        save_rings(&s_cache_bot, &g_bot, ui_rings_bot, ui_rings_bot_n);
    } else {
        save_rings(&g_bot, &s_cache_bot, ui_rings_bot, ui_rings_bot_n);
    }
    PROF(1);
    // remember the state that was drawn; if drawing advanced an animation the next
    // frame's signature differs and that screen is drawn again
    if (top) s_last_sig_top = sig_top;
    if (bot) s_last_sig = sig;
    s_force_redraw = false;
    s_present_top = top;
    s_present_bot = bot;
    for (int i = 0; i < ui_rings_top_n; i++) ui_ring_draw(&ui_rings_top[i], s_frame);
    for (int i = 0; i < ui_rings_bot_n; i++) ui_ring_draw(&ui_rings_bot[i], s_frame);
}

void app_present(void) {
    PROF_T0();
    if (s_present_top || s_present_bot) gfx_present(s_present_top, s_present_bot);
    if (!s_present_top) for (int i = 0; i < ui_rings_top_n; i++) gfx_present_rows(true, ui_rings_top[i].y, ui_rings_top[i].y + ui_rings_top[i].h);
    if (!s_present_bot) for (int i = 0; i < ui_rings_bot_n; i++) gfx_present_rows(false, ui_rings_bot[i].y, ui_rings_bot[i].y + ui_rings_bot[i].h);
    PROF(3);
}

// Background library update: runs only on screens that do not hold library indices
// (HOME, All Software, Settings without an overlay), so indices never change under a dialog.
static void background_scan(void) {
    if (!library_scanning() || s_launching || s_ovl != OVL_NONE) return;
    if (s_scr != SCR_HOME && s_scr != SCR_ALL && s_scr != SCR_SETTINGS && s_scr != SCR_LOGIN && s_scr != SCR_FOLDERS) return;
    if (s_pick_coll >= 0) return;
    char home_path[MAX_PATH_LEN] = "", grid_path[MAX_PATH_LEN] = "";
    if (s_home_n > 0 && s_home_sel < s_home_n && s_home[s_home_sel] >= 0) {
        const GameItem *it = library_get(s_home[s_home_sel]);
        if (it) snprintf(home_path, sizeof(home_path), "%s", it->path);
    }
    if (s_view_n > 0 && s_grid_sel < s_view_n) {
        const GameItem *it = library_get(s_view[s_grid_sel]);
        if (it) snprintf(grid_path, sizeof(grid_path), "%s", it->path);
    }
    LibraryScanResult r = library_scan_step(24, 1);
    if (r == LIBRARY_SCAN_BUSY) return;
    if (r == LIBRARY_SCAN_CHANGED) {
        covers_reset();
        refresh_all();
        int hi = home_path[0] ? library_find_path(home_path) : -1;
        for (int i = 0; hi >= 0 && i < s_home_n; i++) if (s_home[i] == hi) { s_home_sel = i; break; }
        int gi = grid_path[0] ? library_find_path(grid_path) : -1;
        for (int i = 0; gi >= 0 && i < s_view_n; i++) if (s_view[i] == gi) { s_grid_sel = i; break; }
    }
    if (library_truncated()) {
        char msg[96];
        snprintf(msg, sizeof(msg), tr(S_LIB_FULL, L()), library_count());
        toast(msg);
    }
}

bool app_frame(const AppInput *in) {
    static int depth = 0;
    depth++;
    AppInput cal_in;                      // DSplay's own touch calibration, when the player made one
    const TouchCal *tc = touchcal_current();
    if (depth == 1 && tc && in->touching && (in->rawx || in->rawy) && s_scr != SCR_TOUCHCAL) {
        cal_in = *in;
        touchcal_apply(tc, in->rawx, in->rawy, &cal_in.tx, &cal_in.ty);
        in = &cal_in;
    }
    if (depth == 1) {
        s_frame++;
        covers_frame_begin();
        if (s_toast_t > 0) s_toast_t--;
        if (s_fx_t > 0 && ++s_fx_t > 9) s_fx_t = 0;
        if (s_kp_t > 0 && ++s_kp_t > 14) s_kp_t = 0;
        {   // the console's volume / brightness changed: a small indicator in the upper status bar
            static unsigned seq_seen;
            static int vol_was = -1, bl_was = -1;
            int v, b;
            unsigned seq = console_levels(&v, &b);
            if (seq != seq_seen) {
                seq_seen = seq;
                if (vol_was >= 0 && v != vol_was) { s_lv_kind = 0; s_lv_val = v; s_lv_max = 31; s_lv_t = 100; }
                else if (bl_was >= 0 && b != bl_was) { s_lv_kind = 1; s_lv_val = b; s_lv_max = 4; s_lv_t = 100; }
                vol_was = v; bl_was = b;
            }
            if (s_lv_t > 0) s_lv_t--;
            ui_set_level(s_lv_kind, s_lv_val, s_lv_max, s_lv_t >= 16 ? 32 : s_lv_t * 2);
        }
        if ((in->down & KEY_A) && ui_rings_bot_n > 0 && s_scr != SCR_TOUCHCAL && !s_ss_on) {
            const UiRing *rg = &ui_rings_bot[ui_rings_bot_n - 1];   // the frame drawn last (overlays on top)
            s_kp_x = rg->x + 4; s_kp_y = rg->y + 4; s_kp_w = rg->w - 8; s_kp_h = rg->h - 8; s_kp_r = rg->r > 4 ? rg->r - 4 : 0;
            s_kp_scr = s_scr; s_kp_ovl = s_ovl; s_kp_t = 1;
        }
        if (s_fade > 0) s_fade -= s_fade_slow ? ((s_frame & 1) ? 1 : 0) : 2;
        if (s_fade <= 0) { s_fade = 0; s_fade_slow = false; }
        background_scan();
        static bool lid_was = false;
        bool lid = (in->held & KEY_LID) != 0;
        if (lid && !lid_was && !s_launching) { lid_was = true; lid_closed(); depth--; return true; }
        lid_was = lid;
        if ((s_frame % 30) == 0) {
            // battery: warn once when it gets low, tell when charging starts / the battery is full
            static int prev_lvl = -1;
            static bool prev_chg = false, warned = false;
            unsigned st = ui_battery_state();
            int lvl = (int)(st & 0x7F);
            bool chg = (st & 0x80) != 0;
            if (prev_lvl >= 0) {
                if (chg && !prev_chg) toast(tr(S_CHARGING, L()));
                if (chg && lvl >= 15 && prev_lvl < 15) toast(tr(S_CHARGED, L()));
            }
            if (!chg && lvl <= 3 && !warned && s_ovl == OVL_NONE && !s_launching && s_frame > 120) {
                warned = true;
                show_dialog(DLG_INFO, ICON_WARNING_L, tr(S_BATT_LOW_T, L()), tr(S_BATT_LOW_M, L()));
            }
            if (chg || lvl > 3) warned = false;
            prev_lvl = lvl;
            prev_chg = chg;
        }
        if (s_scr == SCR_LOGIN && s_login_wp_wait > 0 && --s_login_wp_wait == 0) {
            // wallpaper of the highlighted profile, once the cursor rests on it
            int slot = login_slot_at(s_login_sel);
            if (slot >= 0) {
                char p[MAX_PATH_LEN];
                profiles_file(slot, "wallpaper.bin", p, sizeof(p));
                ui_wallpaper_load(p);
            } else {
                ui_wallpaper_load(NULL);
            }
            s_login_wp_slot = slot;
        }
        static int slot_was = -1;                 // -1: first reading after power on (no sound)
        if (s_frame > 20 && slot1_poll()) {
            bool now = slot1_present();
            if (slot_was < 0) {                    // the card was already there when the console started
                build_home();
            } else if (now != (slot_was == 1)) {   // inserted / removed
                if (s_frame > 180) sfx(now ? SFX_INSERT : SFX_EJECT);   // silent while the console starts
                build_home();
                if (now && s_scr == SCR_HOME) { s_home_sel = 0; s_home_focus = 0; }
            } else {                               // same card, its name has just been read
                build_home();
            }
            slot_was = now ? 1 : 0;
            {   // box art of the card, when Studio has downloaded it
                static Color card_px[COVER_SIZE * COVER_SIZE];
                const GameItem *ci = slot1_item();
                ui_set_card_cover(ci && covers_load_code(s_drv, ci->game_code, card_px) ? card_px : NULL);
            }
            ui_tile_cache_reset();
            s_force_redraw = true;
        }
    }
    if (depth == 1 && in->power && s_scr != SCR_FATAL && !s_launching) {
        // the POWER button: the power menu (pressed again: closed)
        ss_stop();
        s_idle = 0;
        if (s_ovl == OVL_MENU && s_menu_kind == MENU_POWER) { s_ovl = OVL_NONE; sfx(SFX_BACK); }
        else {
            if (s_scr == SCR_PAD) close_pad();
            s_ovl = OVL_NONE;
            open_menu(MENU_POWER, 0);
        }
        s_force_redraw = true;
    }
    if (depth == 1) {
        // Wi-Fi did not connect (controller, Store, Nearby): try again or choose a network
        static bool wifi_dlg_done;
        bool needs = s_scr == SCR_PAD || s_scr == SCR_STORE || s_scr == SCR_NEARBY;
        bool failed = needs && pad_status() == PAD_ST_WIFI_FAIL;
        if (!failed) wifi_dlg_done = false;
        else if (!wifi_dlg_done && s_ovl == OVL_NONE) {
            wifi_dlg_done = true;
            show_dialog(DLG_WIFI, ICON_WARNING_L, tr(S_WIFI_FAIL_T, L()), tr(S_WIFI_FAIL_M, L()));
            s_force_redraw = true;
        }
    }
    if (depth == 1 && s_scr == SCR_WIFI) wifi_screen_frame();
    if (depth == 1 && s_ss_on) {
        // screensaver: any button or touch only returns (the press is not used for anything else)
        ss_frame();
        if (in->down || in->touching) { ss_stop(); s_wake_hold = in->touching; }
        else if (s_idle < 0x7FFFFFFF) s_idle++;
        app_render();
        depth--;
        return true;
    }
    if (depth == 1) ph_crop_drag(in);
    if (depth == 1 && s_scr == SCR_PAD && s_ovl != OVL_DIALOG) {
        // controller mode takes every key and touch; the screen never dims meanwhile
        PadInput pi = { in->down, in->held, in->touching, in->tx, in->ty };
        s_idle = 0;
        if (!pad_frame(&pi)) close_pad();
        app_render();
        depth--;
        return true;
    }
    if (depth == 1 && s_scr == SCR_STORE) store_frame(in);
    if (depth == 1 && s_scr == SCR_NEARBY) nearby_app_frame();
    bool woke = false;
    if (depth == 1) {
        if (in->down || in->touching) {
            if (s_idle >= idle_dim_frames()) { woke = true; s_wake_hold = in->touching; }
            s_idle = 0;
        } else if (s_idle < 0x7FFFFFFF) {
            s_idle++;
        }
        if (!in->touching) s_wake_hold = false;
        ss_check_idle();
    }
    u32 k = woke ? 0 : (in->down & ~KEY_TOUCH), rep = woke ? 0 : (in->repeat & ~KEY_TOUCH);
    if (depth == 1 && s_scr == SCR_TOUCHCAL) touchcal_touch(in);
    else if (depth == 1 && !woke && !s_wake_hold) handle_touch(in);
    if (s_scr != SCR_FATAL && (k || rep)) {
        if (s_ovl == OVL_DIALOG) input_dialog(k);
        else if (s_ovl == OVL_PIN) input_pin(k);
        else if (s_ovl == OVL_MENU) input_menu(k, rep);
        else if (s_ovl == OVL_OPTIONS) input_options(k, rep);
        else {
            switch (s_scr) {
                case SCR_HOME: input_home(k, rep); break;
                case SCR_ALL: input_all(k, rep); break;
                case SCR_KEYBOARD: input_keyboard(k, rep); break;
                case SCR_SETTINGS: input_settings(k, rep); break;
                case SCR_NOTES: input_notes(k, rep); break;
                case SCR_LOGIN: input_login(k, rep); break;
                case SCR_FOLDERS: input_folders(k, rep); break;
                case SCR_HOMEEDIT: input_homeedit(k, rep); break;
                case SCR_PHOTOS: input_photos(k, rep); break;
                case SCR_CHEATS: input_cheats(k, rep); break;
                case SCR_APPS: input_apps(k, rep); break;
                case SCR_STORE: input_store(k, rep); break;
                case SCR_NEARBY: input_nearby(k, rep); break;
                case SCR_STATS: if (k & (KEY_B | KEY_A)) { s_scr = SCR_SETTINGS; sfx(SFX_BACK); } break;
                case SCR_TOUCHCAL: input_touchcal(k); break;
                case SCR_WIFI: input_wifi(k, rep); break;
                default: break;
            }
        }
    }
#if defined(HL_PROFILE) && defined(ARM9)
    if (depth == 1) { if (s_frame == 2) { TIMER_CR(2) = 0; TIMER_DATA(2) = 0; TIMER_CR(2) = TIMER_ENABLE | TIMER_DIV_64; }
        PROF_T0(); app_render(); PROF(5); if ((s_frame % 600) == 0) prof_dump(); }
#else
    if (depth == 1) {
        app_render();
        prefetch_covers();
    }
#endif
    depth--;
    return true;
}

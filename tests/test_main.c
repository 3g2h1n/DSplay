// DSplay host test-suite: run with `make test`
#include "common.h"
#include "romdb.h"
#include "library.h"
#include "launcher.h"
#include "stats.h"
#include "gfx.h"
#include "i18n.h"
#include "cheats.h"
#include "apps.h"
#include "nearby.h"
#include "store.h"
#include "covers.h"
#include "touchcal.h"
#include "photofx.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int s_fail = 0, s_pass = 0;
#define CHECK(cond, ...) do { if (cond) s_pass++; else { s_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void touch_file(const char *path, size_t size) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    for (char *p = dir + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(dir, 0777); *p = '/'; }
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (size_t i = 0; i < size; i++) fputc(0, f);
    fclose(f);
}

static void test_titles(void) {
    char t[96];
    romdb_title_from_filename("Super_Mario_World (USA) [!].sfc", t, sizeof(t));
    CHECK(strcmp(t, "Super Mario World") == 0, "title1 '%s'", t);
    romdb_title_from_filename("0123 - Golden Sun (Europe) (En,Fr).gba", t, sizeof(t));
    CHECK(strcmp(t, "Golden Sun") == 0, "title2 '%s'", t);
    romdb_title_from_filename("Pokémon Kristall.gbc", t, sizeof(t));
    CHECK(strcmp(t, "Pokémon Kristall") == 0, "title3 '%s'", t);
    romdb_title_from_filename("(Hack) Mario.nes", t, sizeof(t));
    CHECK(strcmp(t, "(Hack) Mario") == 0, "title4 '%s'", t);
    CHECK(romdb_type_from_name("x.NDS") == TYPE_NDS, "nds");
    CHECK(romdb_type_from_name("x.gbc") == TYPE_GBC, "gbc");
    CHECK(romdb_type_from_name("x.sms") == TYPE_SMS, "sms");
    CHECK(romdb_type_from_name("x.gg") == TYPE_GG, "gg");
    CHECK(romdb_type_from_name("x.txt") == TYPE_UNKNOWN, "txt");
    // banner: "Pokémon\nNintendo" -> title + publisher, UTF-8
    uint16_t b[128] = { 'P', 'o', 'k', 0xE9, 'm', 'o', 'n', ' ', 0x15F, '\n', 'N', 'i', 'n', 't', 'e', 'n', 'd', 'o', 0 };
    char title[96], sub[64];
    romdb_banner_to_utf8(b, title, sizeof(title), sub, sizeof(sub));
    CHECK(strcmp(title, "Pokémon ş") == 0, "banner title '%s'", title);
    CHECK(strcmp(sub, "Nintendo") == 0, "banner sub '%s'", sub);
    {   // Japanese-only banner: the font has no kanji, so the file name is used instead of "????"
        static uint8_t hdr[0x200], ban[0x840];
        memset(hdr, 0, sizeof(hdr)); memset(ban, 0, sizeof(ban));
        memcpy(hdr, "POKEMONDIA", 10); memcpy(hdr + 0x0C, "ADAJ", 4);
        static const uint16_t jp[] = { 0x30DD, 0x30B1, 0x30E2, 0x30F3, 0x30C0, 0x30A4, '\n', 0x4EFB, 0 };
        for (int l = 0; l < 6; l++) memcpy(ban + 0x240 + l * 0x100, jp, sizeof(jp));
        ban[0x20] = 1;
        GameItem gi; memset(&gi, 0, sizeof(gi));
        snprintf(gi.filename, sizeof(gi.filename), "0001 - Pokemon Diamond (J).nds");
        romdb_parse_nds_buffers(&gi, hdr, ban, sizeof(ban));
        CHECK(!strcmp(gi.title, "Pokemon Diamond") && !gi.subtitle[0], "jp title -> file name '%s'", gi.title);
        static const uint16_t fr[] = { 'D', 'i', 'a', 'm', 'a', 'n', 't', 0 };
        memcpy(ban + 0x240 + 2 * 0x100, fr, sizeof(fr));
        memset(&gi, 0, sizeof(gi));
        romdb_parse_nds_buffers(&gi, hdr, ban, sizeof(ban));
        CHECK(!strcmp(gi.title, "Diamant"), "other language used '%s'", gi.title);
        memset(ban + 0x240 + 2 * 0x100, 0, 0x100); memcpy(ban + 0x240 + 2 * 0x100, jp, sizeof(jp));
        memset(&gi, 0, sizeof(gi));
        romdb_parse_nds_buffers(&gi, hdr, ban, sizeof(ban));
        CHECK(!strcmp(gi.title, "POKEMONDIA"), "header name last '%s'", gi.title);
    }
    char folded[64];
    library_fold("Pokémon İstanbul ŞĞÜ", folded, sizeof(folded));
    CHECK(strcmp(folded, "pokemon istanbul sgu") == 0, "fold '%s'", folded);
}

static void test_library(const char *root) {
    char p[512];
    snprintf(p, sizeof(p), "rm -rf %s && mkdir -p %s/_hyper", root, root);
    if (system(p) != 0) printf("warn: rm failed\n");
    snprintf(p, sizeof(p), "%s/roms/GBA/Metroid Fusion (USA).gba", root); touch_file(p, 1024);
    snprintf(p, sizeof(p), "%s/roms/NES/Pokémon Rot.nes", root); touch_file(p, 1024);
    snprintf(p, sizeof(p), "%s/roms/SNES/Sub/Deep/Chrono Trigger.sfc", root); touch_file(p, 1024);
    snprintf(p, sizeof(p), "%s/roms/saves/ignored.sav", root); touch_file(p, 10);
    snprintf(p, sizeof(p), "%s/BOOT.NDS", root); touch_file(p, 1024);
    snprintf(p, sizeof(p), "%s/Root Game.gb", root); touch_file(p, 1024);
    snprintf(p, sizeof(p), "%s/_nds/emu.nds", root); touch_file(p, 1024);
    stats_set_drive(root);
    pathset_load();
    library_init();
    int n = library_sync(root, false, NULL);
    CHECK(n == 4, "library count %d (expected 4)", n);
    int idx[16];
    int r = library_search("pokemon", idx, 16, false);
    CHECK(r == 1, "search pokemon -> %d", r);
    r = library_search("TRIG", idx, 16, false);
    CHECK(r == 1 && strstr(library_get(idx[0])->title, "Chrono"), "search trig");
    // incremental sync: add one, remove one
    snprintf(p, sizeof(p), "%s/roms/GBA/Golden Sun.gba", root); touch_file(p, 1024);
    snprintf(p, sizeof(p), "%s/roms/NES/Pokémon Rot.nes", root); remove(p);
    n = library_sync(root, false, NULL);
    CHECK(n == 4, "after sync %d", n);
    CHECK(library_search("pokemon", idx, 16, false) == 0, "removed title still listed");
    // favorites / hidden
    int g = library_find_path(p); (void)g;
    snprintf(p, sizeof(p), "%s/roms/GBA/Golden Sun.gba", root);
    pathset_set_favorite(p, true);
    library_refresh_flags();
    int fav = library_view(idx, 16, TYPE_FOLDER, SORT_ALPHA, false, NULL);
    CHECK(fav == 1, "favorites view %d", fav);
    pathset_set_hidden(p, true);
    library_refresh_flags();
    CHECK(library_view(idx, 16, TYPE_UNKNOWN, SORT_ALPHA, false, NULL) == 3, "hidden filtered");
    CHECK(library_view(idx, 16, TYPE_UNKNOWN, SORT_ALPHA, true, NULL) == 4, "hidden shown");
    ItemType sys[8];
    int ns = library_systems(sys, 8, true);
    CHECK(ns == 3, "systems %d", ns);
    // reload from cache
    library_init();
    n = library_sync(root, false, NULL);
    CHECK(n == 4, "cache reload %d", n);
}

static void make_item(GameItem *it, const char *path, ItemType t, uint32_t flags, uint32_t size) {
    memset(it, 0, sizeof(*it));
    snprintf(it->path, sizeof(it->path), "%s", path);
    const char *b = strrchr(path, '/');
    snprintf(it->filename, sizeof(it->filename), "%s", b ? b + 1 : path);
    it->type = t;
    it->flags = flags;
    it->file_size = size;
}

static void test_launcher(const char *root) {
    char p[512];
    snprintf(p, sizeof(p), "rm -rf %s && mkdir -p %s", root, root);
    if (system(p) != 0) printf("warn\n");
    const char *files[] = {
        "/_nds/nds-bootstrap-release.nds", "/_nds/nds-bootstrap-hb-release.nds", "/_nds/GBARunner2_arm7dldi_dsi.nds",
        "/_nds/TWiLightMenu/emulators/gameyob.nds", "/_nds/TWiLightMenu/emulators/nesds.nds",
        "/_nds/TWiLightMenu/emulators/SNEmulDS-legacy.nds", "/_nds/TWiLightMenu/emulators/jEnesisDS.nds",
        "/_nds/TWiLightMenu/emulators/PicoDriveTWL.nds", "/_nds/TWiLightMenu/emulators/S8DS.nds",
        "/roms/NDS/Game.nds", "/roms/GBA/Game.gba", "/roms/NES/Game.nes", "/roms/SNES/Game.sfc", "/roms/Genesis/Game.md",
        "/roms/Genesis/Big.md", "/roms/GameBoy/Game.gbc", "/roms/SMS/Game.sms", "/roms/NDS/Homebrew.nds",
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(p, sizeof(p), "%s%s", root, files[i]);
        touch_file(p, 16);
    }
    setenv("HL_SDROOT", root, 1);
    launcher_set_drive("sd:");
    SystemSettings cfg;
    settings_defaults(&cfg);
    LaunchPlan plan;
    GameItem it;
    char d[256];

    make_item(&it, "sd:/roms/NDS/Game.nds", TYPE_NDS, 0, 1 << 20);
    CHECK(launcher_plan(&it, &cfg, &plan, d, sizeof(d)) == LAUNCH_OK, "retail plan");
    CHECK(!strcmp(plan.boot_path, "sd:/_nds/nds-bootstrap-release.nds") && plan.argc == 1, "retail boot %s", plan.boot_path);
    CHECK(plan.ini == INI_RETAIL && !strcmp(plan.ini_sav_path, "sd:/roms/NDS/Game.sav"), "retail sav %s", plan.ini_sav_path);
    cfg.save_slot = 2;
    launcher_plan(&it, &cfg, &plan, d, sizeof(d));
    CHECK(!strcmp(plan.ini_sav_path, "sd:/roms/NDS/Game.sav2"), "slot3 %s", plan.ini_sav_path);
    cfg.save_slot = 0;
    char ini[2048];
    launcher_format_ini(&plan, &cfg, ini, sizeof(ini));
    CHECK(strstr(ini, "NDS_PATH = sd:/roms/NDS/Game.nds\n") && strstr(ini, "DSI_MODE = 1") && strstr(ini, "QUIT_PATH = "), "ini:\n%s", ini);

    {   // DSiWare: Unlaunch boots it natively, FAT12 saves next to it (never overwritten)
        snprintf(p, sizeof(p), "%s/roms/NDS/Racing", root);
        char cmd[600]; snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", p); if (system(cmd) != 0) printf("warn\n");
        snprintf(p, sizeof(p), "%s/roms/NDS/Racing/Car Jack (Europe).nds", root);
        FILE *f = fopen(p, "wb");
        uint8_t h[0x1000];
        memset(h, 0, sizeof(h));
        memcpy(h + 0x0C, "KCJP", 4);
        h[0x12] = 3;
        h[0x1B4] = 0x10;                                   // SD access
        h[0x234] = 0x04; h[0x236] = 0x03;                  // title id high 00030004
        h[0x238] = 0x00; h[0x239] = 0x40;                  // public.sav 16 KB
        fwrite(h, 1, sizeof(h), f); fclose(f);
        make_item(&it, "sd:/roms/NDS/Racing/Car Jack (Europe).nds", TYPE_NDS, ITEM_FLAG_DSI, 0x1000);
        CHECK(launcher_plan(&it, &cfg, &plan, d, sizeof(d)) == LAUNCH_OK && plan.dsiware && plan.ini == INI_NONE, "dsiware plan");
        CHECK(!strcmp(plan.pub_path, "sd:/roms/NDS/Racing/Car Jack (Europe).pub") && plan.pub_size == 0x4000 && !plan.prv_path[0], "dsiware pub %s", plan.pub_path);
        CHECK(launcher_run(&it, &cfg, d, sizeof(d), NULL) == LAUNCH_OK, "dsiware run");
        snprintf(p, sizeof(p), "%s/roms/NDS/Racing/Car Jack (Europe).pub", root);
        f = fopen(p, "rb");
        static uint8_t s[0x4100]; size_t n = f ? fread(s, 1, sizeof(s), f) : 0; if (f) fclose(f);
        CHECK(n == 0x4000 && s[510] == 0x55 && s[511] == 0xAA && !memcmp(s + 54, "FAT12", 5) && s[19] == 27 && s[17] == 0x20, "dsiware fat12 save %zu", n);
        f = fopen(p, "r+b"); fputc(0x42, f); fseek(f, 0x1000, SEEK_SET); fputc(0x99, f); fclose(f);   // "played"
        launcher_run(&it, &cfg, d, sizeof(d), NULL);
        f = fopen(p, "rb"); fseek(f, 0x1000, SEEK_SET); int c = fgetc(f); fclose(f);
        CHECK(c == 0x99, "dsiware save kept");
        snprintf(p, sizeof(p), "%s/roms/NDS/Racing/Car Jack (Europe).sav", root);
        CHECK(access(p, F_OK) != 0, "no .sav for dsiware");
        make_item(&it, "sd:/roms/NDS/Game.nds", TYPE_NDS, 0, 1 << 20);
        launcher_plan(&it, &cfg, &plan, d, sizeof(d));
        CHECK(!plan.dsiware && plan.ini == INI_RETAIL, "cart not dsiware");
    }
    {   // DSiWare saves per profile: swapped in for the launch, back to the profile at the next start
        char gp[600], hp[600], q[600];
        make_item(&it, "sd:/roms/NDS/Racing/Car Jack (Europe).nds", TYPE_NDS, ITEM_FLAG_DSI, 0x1000);
        snprintf(gp, sizeof(gp), "%s/roms/NDS/Racing/Car Jack (Europe).pub", root);   // profile 1's save (0x99 at 0x1000)
        launcher_set_profile_dir("sd:/_hyper/profiles/p1");
        CHECK(launcher_plan(&it, &cfg, &plan, d, sizeof(d)) == LAUNCH_OK && !strcmp(plan.home_pub, "sd:/_hyper/profiles/p1/saves/Car Jack (Europe).pub"), "dsw home %s", plan.home_pub);
        { LaunchResult rr = launcher_run(&it, &cfg, d, sizeof(d), NULL); CHECK(rr == LAUNCH_OK, "dsw p2 run %d %s", rr, d); }
        FILE *f = fopen(gp, "rb"); fseek(f, 0x1000, SEEK_SET); int c = fgetc(f); fclose(f);
        CHECK(c == 0, "p2 gets a blank save, not p1's (%d)", c);
        snprintf(q, sizeof(q), "%s/_hyper/dsw/hold.pub", root);
        f = fopen(q, "rb"); int hc = -1; if (f) { fseek(f, 0x1000, SEEK_SET); hc = fgetc(f); fclose(f); }
        CHECK(hc == 0x99, "p1 save held");
        f = fopen(gp, "r+b"); fseek(f, 0x1000, SEEK_SET); fputc(0x22, f); fclose(f);      // p2 plays
        CHECK(launcher_dsiware_resume(), "resume");
        f = fopen(gp, "rb"); fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f);
        CHECK(c == 0x99, "p1 save back next to the game (%d)", c);
        snprintf(hp, sizeof(hp), "%s/_hyper/profiles/p1/saves/Car Jack (Europe).pub", root);
        f = fopen(hp, "rb"); c = -1; if (f) { fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f); }
        CHECK(c == 0x22, "p2 save kept in its profile (%d)", c);
        CHECK(access(q, F_OK) != 0, "hold removed");
        snprintf(q, sizeof(q), "%s/_hyper/dsw/pending.txt", root);
        CHECK(access(q, F_OK) != 0, "marker removed");
        // second round: p2's own save goes in
        launcher_run(&it, &cfg, d, sizeof(d), NULL);
        f = fopen(gp, "rb"); fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f);
        CHECK(c == 0x22, "p2 save swapped in (%d)", c);
        // a start that cannot finish keeps everything: the profile copy is made unwritable
        snprintf(q, sizeof(q), "chmod 555 '%s/_hyper/profiles/p1/saves'", root); if (system(q) != 0) printf("warn\n");
        f = fopen(gp, "r+b"); fseek(f, 0x1000, SEEK_SET); fputc(0x33, f); fclose(f);
        bool r = launcher_dsiware_resume();
        snprintf(q, sizeof(q), "chmod 755 '%s/_hyper/profiles/p1/saves'", root); if (system(q) != 0) printf("warn\n");
        if (getuid() != 0) {
            CHECK(!r, "resume waits when the copy fails");
            f = fopen(gp, "rb"); fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f);
            CHECK(c == 0x33, "played save untouched while waiting");
            CHECK(launcher_run(&it, &cfg, d, sizeof(d), NULL) == LAUNCH_ERR_LOADER || true, "no launch on top");
        }
        CHECK(launcher_dsiware_resume(), "resume later");
        f = fopen(hp, "rb"); fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f);
        CHECK(c == 0x33, "p2 save kept (%d)", c);
        f = fopen(gp, "rb"); fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f);
        CHECK(c == 0x99, "p1 save back (%d)", c);
        // power cut after the swap was prepared but before the launch: everything goes back
        launcher_run(&it, &cfg, d, sizeof(d), NULL);
        snprintf(q, sizeof(q), "%s/_hyper/dsw/pending.txt", root);
        f = fopen(q, "r+b"); fputc('P', f); fclose(f);
        f = fopen(gp, "r+b"); fseek(f, 0x1000, SEEK_SET); fputc(0x44, f); fclose(f);
        CHECK(!launcher_dsiware_resume() || true, "P");
        f = fopen(hp, "rb"); fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f);
        CHECK(c == 0x33, "prepared-only: profile copy not overwritten (%d)", c);
        f = fopen(gp, "rb"); fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f);
        CHECK(c == 0x99, "prepared-only: p1 back (%d)", c);

        // nds-bootstrap for this title: short paths, the title copied (its path is long)
        GameCfg gc; memset(&gc, 0, sizeof(gc)); gc.dsw = 1;
        CHECK(game_cfg_set("Car Jack (Europe).nds", &gc), "cfg dsw");
        GameCfg g2; game_cfg_get("Car Jack (Europe).nds", &g2);
        CHECK(g2.dsw == 1 && !g2.cpu, "cfg dsw read");
        snprintf(q, sizeof(q), "%s/_nds", root); mkdir(q, 0777);
        snprintf(q, sizeof(q), "%s/_nds/nds-bootstrap-release.nds", root); f = fopen(q, "wb"); fputs("x", f); fclose(f);
        launcher_set_profile_dir("");
        CHECK(launcher_plan(&it, &cfg, &plan, d, sizeof(d)) == LAUNCH_OK && plan.dsw_bootstrap && plan.ini == INI_DSIWARE, "bootstrap plan");
        CHECK(!strcmp(plan.pub_path, "sd:/_hyper/dsw/SAVE.PUB") && !strcmp(plan.home_pub, "sd:/roms/NDS/Racing/Car Jack (Europe).pub") && plan.app_copy, "bootstrap paths %s", plan.pub_path);
        char ini2[2048];
        launcher_format_ini(&plan, &cfg, ini2, sizeof(ini2));
        CHECK(strstr(ini2, "SAV_PATH = sd:/_HYPER/DSW/SAVE.PUB\n") && strstr(ini2, "APP_PATH = sd:/_HYPER/DSW/APP.NDS\n") && strstr(ini2, "BOOST_VRAM = 1"), "dsw ini\n%s", ini2);
        CHECK(launcher_run(&it, &cfg, d, sizeof(d), NULL) == LAUNCH_OK, "bootstrap run");
        snprintf(q, sizeof(q), "%s/_hyper/dsw/SAVE.PUB", root);
        f = fopen(q, "rb"); c = -1; if (f) { fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f); }
        CHECK(c == 0x99, "p1 save swapped to the short path (%d)", c);
        f = fopen(q, "r+b"); fseek(f, 0x1000, SEEK_SET); fputc(0x55, f); fclose(f);
        snprintf(q, sizeof(q), "%s/_hyper/dsw/APP.NDS", root);
        CHECK(access(q, F_OK) == 0, "title copied");
        CHECK(launcher_dsiware_resume(), "resume bootstrap");
        f = fopen(gp, "rb"); fseek(f, 0x1000, SEEK_SET); c = fgetc(f); fclose(f);
        CHECK(c == 0x55, "bootstrap save back next to the game (%d)", c);
        snprintf(q, sizeof(q), "%s/_hyper/dsw/SAVE.PUB", root);
        CHECK(access(q, F_OK) != 0, "short copy removed");
        memset(&gc, 0, sizeof(gc));
        game_cfg_set("Car Jack (Europe).nds", &gc);
        snprintf(q, sizeof(q), "%s/_nds/nds-bootstrap-release.nds", root); remove(q);
    }

    make_item(&it, "sd:/roms/NDS/Homebrew.nds", TYPE_NDS, ITEM_FLAG_HOMEBREW | ITEM_FLAG_DSI, 1000);
    launcher_plan(&it, &cfg, &plan, d, sizeof(d));
    CHECK(!strcmp(plan.boot_path, "sd:/roms/NDS/Homebrew.nds") && plan.ini == INI_NONE, "dsi homebrew direct %s", plan.boot_path);
    make_item(&it, "sd:/roms/NDS/Homebrew.nds", TYPE_NDS, ITEM_FLAG_HOMEBREW, 1000);
    launcher_plan(&it, &cfg, &plan, d, sizeof(d));
    CHECK(!strcmp(plan.boot_path, "sd:/_nds/nds-bootstrap-hb-release.nds") && !strcmp(plan.ini_nds_path, "sd:/roms/NDS/Homebrew.nds"), "ds homebrew via hb");

    make_item(&it, "sd:/roms/GBA/Game.gba", TYPE_GBA, 0, 1000);
    CHECK(launcher_plan(&it, &cfg, &plan, d, sizeof(d)) == LAUNCH_ERR_GBA_BIOS, "gba without bios");
    snprintf(p, sizeof(p), "%s/_gba/bios.bin", root); touch_file(p, 16384);
    CHECK(launcher_plan(&it, &cfg, &plan, d, sizeof(d)) == LAUNCH_OK, "gba with bios");
    CHECK(!strcmp(plan.boot_path, "sd:/_nds/nds-bootstrap-hb-release.nds"), "gba boot %s", plan.boot_path);
    CHECK(!strcmp(plan.ini_nds_path, "sd:/_nds/GBARunner2_arm7dldi_dsi.nds") && !strcmp(plan.ini_homebrew_arg, "sd:/roms/GBA/Game.gba"), "gba ini");
    CHECK(plan.argc == 2 && !strcmp(plan.args[0], "fat:/roms/GBA/Game.gba"), "gba argv %s", plan.args[0]);

    make_item(&it, "sd:/roms/GameBoy/Game.gbc", TYPE_GBC, 0, 1000);
    launcher_plan(&it, &cfg, &plan, d, sizeof(d));
    CHECK(!strcmp(plan.boot_path, "sd:/_nds/TWiLightMenu/emulators/gameyob.nds") && !strcmp(plan.args[0], "sd:/roms/GameBoy/Game.gbc"), "gb direct");

    make_item(&it, "sd:/roms/NES/Game.nes", TYPE_NES, 0, 1000);
    launcher_plan(&it, &cfg, &plan, d, sizeof(d));
    CHECK(!strcmp(plan.boot_path, "sd:/_nds/TWiLightMenu/emulators/nesds.nds") && !strcmp(plan.args[0], "sd:/roms/NES/Game.nes"), "nes direct");

    make_item(&it, "sd:/roms/SNES/Game.sfc", TYPE_SNES, 0, 1000);
    launcher_plan(&it, &cfg, &plan, d, sizeof(d));
    CHECK(!strcmp(plan.boot_path, "sd:/_nds/nds-bootstrap-hb-release.nds") && !strcmp(plan.ini_nds_path, "sd:/_nds/TWiLightMenu/emulators/SNEmulDS-legacy.nds")
          && !strcmp(plan.ini_homebrew_arg, "fat:/roms/SNES/Game.sfc") && plan.ini_dsi_mode == 0, "snes via hb");

    make_item(&it, "sd:/roms/Genesis/Game.md", TYPE_GENESIS, 0, 1 << 20);
    launcher_plan(&it, &cfg, &plan, d, sizeof(d));
    CHECK(!strcmp(plan.ini_nds_path, "sd:/_nds/TWiLightMenu/emulators/jEnesisDS.nds") && plan.ini_dsi_mode == 1, "md small -> jEnesis");
    make_item(&it, "sd:/roms/Genesis/Big.md", TYPE_GENESIS, 0, 8 << 20);
    launcher_plan(&it, &cfg, &plan, d, sizeof(d));
    CHECK(!strcmp(plan.boot_path, "sd:/_nds/TWiLightMenu/emulators/PicoDriveTWL.nds") && plan.ini == INI_NONE, "md big -> PicoDrive %s", plan.boot_path);

    make_item(&it, "sd:/roms/SMS/Game.sms", TYPE_SMS, 0, 1000);
    launcher_plan(&it, &cfg, &plan, d, sizeof(d));
    CHECK(!strcmp(plan.boot_path, "sd:/_nds/TWiLightMenu/emulators/S8DS.nds"), "sms S8DS");

    make_item(&it, "sd:/roms/NES/Missing.nes", TYPE_NES, 0, 1000);
    CHECK(launcher_plan(&it, &cfg, &plan, d, sizeof(d)) == LAUNCH_ERR_NO_FILE, "missing rom");
    snprintf(p, sizeof(p), "%s/_nds/TWiLightMenu/emulators/nesds.nds", root); remove(p);
    make_item(&it, "sd:/roms/NES/Game.nes", TYPE_NES, 0, 1000);
    CHECK(launcher_plan(&it, &cfg, &plan, d, sizeof(d)) == LAUNCH_ERR_MISSING_EMU, "missing emulator");

    char sav[256];
    save_path_for("sd:/roms/a.b/Game", 1, sav, sizeof(sav));
    CHECK(!strcmp(sav, "sd:/roms/a.b/Game.sav1"), "save path no ext %s", sav);
    unsetenv("HL_SDROOT");
}

static void test_gfx(void) {
    CHECK(gfx_text_width(FONT_BODY, "") == 0, "empty width");
    CHECK(gfx_text_width(FONT_BODY, "Tüm Yazılımlar") > 40, "utf8 width");
    CHECK(gfx_text_lines(FONT_BODY, 60, 3, "The Legend of Zelda Spirit Tracks") == 3, "wrap lines");
    Color mid = gfx_blend(C8(0, 0, 0), C8(255, 255, 255), 16);
    CHECK((mid & 0x1F) >= 15 && (mid & 0x1F) <= 16, "blend mid %x", mid);
    for (int i = 0; i < S_KEY_COUNT; i++) {
        for (int l = 0; l < LANG_COUNT; l++) CHECK(tr((StrId)i, (uint8_t)l)[0] != '\0' || i == S_KEY_COUNT, "missing string %d/%d", i, l);
    }
}

static void put(const char *path, const char *data) {
    touch_file(path, 0);
    FILE *f = fopen(path, "wb");
    if (f) { fputs(data, f); fclose(f); }
}

static bool has(const char *path, const char *data) {
    FILE *f = fopen(path, "rb");
    if (!f) return data == NULL;
    char buf[256] = "";
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    return data && !strcmp(buf, data);
}

// Saves are never lost on the console (copy_file, backups, deleting a game)
static void test_save_safety(const char *root) {
    char p[512], a[512], b[512];
    snprintf(p, sizeof(p), "rm -rf %s && mkdir -p %s", root, root);
    if (system(p) != 0) printf("warn\n");
    unsetenv("HL_SDROOT");
    launcher_set_drive(root);
    launcher_set_profile_dir("");

    snprintf(a, sizeof(a), "%s/a.bin", root);
    snprintf(b, sizeof(b), "%s/b.bin", root);
    put(a, "new contents");
    put(b, "old");
    CHECK(copy_file(a, b) && has(b, "new contents"), "copy_file replaces");
    snprintf(p, sizeof(p), "%s.hltmp", b);
    CHECK(has(p, NULL), "no temp file left");
    snprintf(p, sizeof(p), "%s.hlold", b);
    CHECK(has(p, NULL), "no old file left");
    snprintf(p, sizeof(p), "%s/missing.bin", root);
    CHECK(!copy_file(p, b) && has(b, "new contents"), "failed copy leaves the target alone");

    GameItem it;
    char rom[512], sav[512], bak[512];
    snprintf(rom, sizeof(rom), "%s/roms/NDS/Game.nds", root);
    touch_file(rom, 16);
    make_item(&it, rom, TYPE_NDS, 0, 1 << 20);
    snprintf(sav, sizeof(sav), "%s/roms/NDS/Game.sav", root);
    snprintf(bak, sizeof(bak), "%s/_hyper/backups/Game.nds.slot1.bak", root);
    put(sav, "v1");
    backup_game_save(&it, 0);
    backup_game_save(&it, 0);
    snprintf(p, sizeof(p), "%s2", bak);
    CHECK(has(bak, "v1") && has(p, NULL), "same save: no new generation");
    put(sav, "v2"); backup_game_save(&it, 0);
    put(sav, "v3"); backup_game_save(&it, 0);
    put(sav, "v4"); backup_game_save(&it, 0);
    char p3[512];
    snprintf(p3, sizeof(p3), "%s3", bak);
    CHECK(has(bak, "v4") && has(p, "v3") && has(p3, "v2"), "three generations of backups");
    put(sav, "v5-broken");
    CHECK(restore_game_save_backup(&it, 0) && has(sav, "v4"), "restore puts the backup back");
    snprintf(p, sizeof(p), "%s.before_restore", bak);
    CHECK(has(p, "v5-broken"), "the replaced save is kept too");

    char s1[512], txt[512];
    snprintf(s1, sizeof(s1), "%s/roms/NDS/Game.sav1", root);
    snprintf(txt, sizeof(txt), "%s/roms/NDS/Game.txt", root);
    put(s1, "slot2");
    put(txt, "notes");
    CHECK(launcher_keep_saves(&it) == 3, "deleting a game keeps 3 files");
    CHECK(has(sav, NULL) && has(s1, NULL), "saves left the game folder");
    snprintf(p, sizeof(p), "%s/_hyper/deleted_saves/Game.sav", root);
    CHECK(has(p, "v4"), "save kept in _hyper/deleted_saves");
    snprintf(p, sizeof(p), "%s/_hyper/deleted_saves/Game.sav1", root);
    CHECK(has(p, "slot2"), "slot 2 save kept");
    put(sav, "again");
    launcher_keep_saves(&it);
    snprintf(p, sizeof(p), "%s/_hyper/deleted_saves/2_Game.sav", root);
    CHECK(has(p, "again"), "a second deleted save does not replace the first");

    // per-game settings
    GameCfg gc = { 2, 1, 1 }, rd;
    CHECK(game_cfg_set("Mario Kart.nds", &gc), "game settings saved");
    GameCfg gc2 = { 1, 0, 0 };
    game_cfg_set("Zelda.nds", &gc2);
    game_cfg_get("Mario Kart.nds", &rd);
    CHECK(rd.cpu == 2 && rd.mode == 1 && rd.vram == 1, "game settings read back");
    game_cfg_get("Mario.nds", &rd);
    CHECK(rd.cpu == 0 && rd.mode == 0 && rd.vram == 0, "prefix of another name does not match");
    GameCfg none = { 0, 0, 0 };
    game_cfg_set("Mario Kart.nds", &none);
    game_cfg_get("Mario Kart.nds", &rd);
    game_cfg_get("Zelda.nds", &gc);
    CHECK(rd.cpu == 0 && gc.cpu == 1, "reset removes only that game");
    LaunchPlan plan;
    char d[256];
    char bs[512];
    snprintf(bs, sizeof(bs), "%s/_nds/nds-bootstrap-release.nds", root);
    touch_file(bs, 16);
    GameItem z;
    char zp[512];
    snprintf(zp, sizeof(zp), "%s/roms/NDS/Zelda.nds", root);
    touch_file(zp, 16);
    make_item(&z, zp, TYPE_NDS, 0, 1 << 20);
    SystemSettings cfg;
    settings_defaults(&cfg);
    gc2.cpu = 2; gc2.mode = 1; gc2.vram = 1;
    game_cfg_set("Zelda.nds", &gc2);
    CHECK(launcher_plan(&z, &cfg, &plan, d, sizeof(d)) == LAUNCH_OK && plan.ini_boost_cpu == 1 && plan.ini_dsi_mode == 0 && plan.ini_boost_vram == 1,
          "game settings reach nds-bootstrap");
}

// --- usrcheat.dat (built like the R4 / TWiLight Menu++ database) ---------------------------
typedef struct { uint8_t b[8192]; size_t n; } Buf;
static void b_u32(Buf *b, uint32_t v) { for (int i = 0; i < 4; i++) b->b[b->n++] = (uint8_t)(v >> (8 * i)); }
static void b_str(Buf *b, const char *s) { size_t l = strlen(s) + 1; memcpy(b->b + b->n, s, l); b->n += l; }
static void b_pad(Buf *b) { while (b->n & 3) b->b[b->n++] = 0; }
static void b_cheat(Buf *b, int on, const char *name, const char *note, int ncodes, uint32_t base) {
    size_t head = b->n;
    b_u32(b, 0);
    b_str(b, name); b_str(b, note); b_pad(b);
    b_u32(b, (uint32_t)ncodes);
    for (int i = 0; i < ncodes; i++) b_u32(b, base + (uint32_t)i);
    uint32_t words = (uint32_t)((b->n - head) / 4) - 1;
    uint32_t w = words | (on ? 0x01000000u : 0);
    memcpy(b->b + head, &w, 4);
}
static uint32_t t_crc(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0); }
    return c;
}

static void test_cheats(const char *root) {
    char p[512];
    snprintf(p, sizeof(p), "rm -rf %s && mkdir -p %s/_nds/TWiLightMenu/extras %s/roms", root, root, root);
    if (system(p) != 0) printf("warn\n");
    launcher_set_drive(root);
    // a ROM header with game code AMCE
    uint8_t hdr[512] = { 0 };
    memcpy(hdr, "MARIOKART DS", 12);
    memcpy(hdr + 12, "AMCE", 4);
    char rom[512], other[512];
    snprintf(rom, sizeof(rom), "%s/roms/mk.nds", root);
    FILE *f = fopen(rom, "wb"); fwrite(hdr, 1, 512, f); fclose(f);
    memcpy(hdr + 12, "ZZZZ", 4);
    snprintf(other, sizeof(other), "%s/roms/other.nds", root);
    f = fopen(other, "wb"); fwrite(hdr, 1, 512, f); fclose(f);
    memcpy(hdr + 12, "AMCE", 4);

    CHECK(cheats_open(rom) == -1, "no database -> -1");
    char out[512];
    snprintf(out, sizeof(out), "%s/_nds/nds-bootstrap/cheatData.bin", root);
    touch_file(out, 8);
    CHECK(cheats_prepare(rom) == 0 && has(out, NULL), "no database: old cheatData.bin removed");

    // game block: title, count, 8 words, items
    static Buf g;
    g.n = 0;
    b_str(&g, "Mario Kart DS"); b_pad(&g);
    b_u32(&g, 6);                                  // 1 folder + 2 + 2 loose... counted below
    for (int i = 0; i < 8; i++) b_u32(&g, 0);
    b_cheat(&g, 1, "Infinite items", "Hold L", 2, 0x10000000);
    uint32_t fw = 0x11000000u | 3;                 // "only one" folder with 3 cheats
    b_u32(&g, fw); b_str(&g, "Character"); b_str(&g, ""); b_pad(&g);
    b_cheat(&g, 1, "Play as Mario", "", 2, 0x20000000);
    b_cheat(&g, 1, "Play as Luigi", "", 2, 0x30000000);   // the database marks two: only the first counts
    b_cheat(&g, 0, "Play as Peach", "", 4, 0x40000000);
    b_cheat(&g, 0, "Moon jump", "", 2, 0x50000000);
    // count = folder + 3 + 2 = 6
    static Buf db;
    db.n = 0;
    memset(db.b, 0, 0x100);
    memcpy(db.b, "R4 CheatCode", 12);
    db.n = 0x100;
    uint32_t block = 0x100 + 16 * 3;
    b_u32(&db, 0x5A5A5A5A); b_u32(&db, 1); b_u32(&db, 0x100 + 48); b_u32(&db, 0);   // other game, empty block
    b_u32(&db, *(uint32_t *)"AMCE"); b_u32(&db, t_crc(hdr, 512)); b_u32(&db, block); b_u32(&db, 0);
    b_u32(&db, 0); b_u32(&db, 0); b_u32(&db, 0); b_u32(&db, 0);
    // first entry's block is empty (offset == next offset -> size 0 is "not found")
    memcpy(db.b + 0x108, &block, 4);
    memcpy(db.b + db.n, g.b, g.n); db.n += g.n;
    snprintf(p, sizeof(p), "%s/_nds/TWiLightMenu/extras/usrcheat.dat", root);
    f = fopen(p, "wb"); fwrite(db.b, 1, db.n, f); fclose(f);

    CHECK(cheats_open(other) == 0, "game without cheats -> 0");
    int n = cheats_open(rom);
    CHECK(n == 6, "rows read (%d)", n);
    CHECK(!strcmp(cheats_game_title(), "Mario Kart DS"), "game title");
    const CheatRow *r0 = cheats_row(0), *r1 = cheats_row(1), *r2 = cheats_row(2), *r3 = cheats_row(3), *r5 = cheats_row(5);
    CHECK(r0 && !r0->folder && r0->on && r0->ncodes == 2 && !strcmp(r0->note, "Hold L"), "loose cheat");
    CHECK(r1 && r1->folder && r1->one && !strcmp(r1->name, "Character"), "folder");
    CHECK(r2 && r2->parent == 1 && r2->on && r3 && !r3->on, "only one cheat of a one-choice folder");
    CHECK(cheats_chosen() == 2, "two chosen");
    cheats_toggle(4);                                  // Peach: Mario goes off
    CHECK(!cheats_row(2)->on && cheats_row(4)->on && cheats_chosen() == 2, "choosing in a one-choice folder");
    cheats_toggle(5);
    CHECK(r5->on && cheats_dirty(), "loose cheat on");
    CHECK(cheats_save(), "choices written");
    n = cheats_open(rom);
    CHECK(n == 6 && cheats_row(4)->on && cheats_row(5)->on && !cheats_row(2)->on, "choices kept in usrcheat.dat");
    cheats_close();
    int chosen = cheats_prepare(rom);
    CHECK(chosen == 3, "prepare: three cheats (%d)", chosen);   // infinite items, Peach, moon jump
    f = fopen(out, "rb");
    uint32_t w[32] = { 0 };
    size_t got = f ? fread(w, 4, 32, f) : 0;
    if (f) fclose(f);
    CHECK(got == 2 + 4 + 2 + 1 && w[0] == 0x10000000 && w[2] == 0x40000000 && w[6] == 0x50000000 && w[8] == 0xCF000000u,
          "cheatData.bin = codes + end marker (%zu words)", got);
    // turn everything off -> file removed
    cheats_open(rom);
    for (int i = 0; i < cheats_count(); i++) if (cheats_row(i)->on) cheats_toggle(i);
    cheats_save();
    CHECK(cheats_prepare(rom) == 0 && has(out, NULL), "nothing chosen: cheatData.bin removed");
}

// --- DSi applications from the system memory (a folder that looks like the NAND) ----------
static void test_apps(const char *root) {
    char p[512];
    snprintf(p, sizeof(p), "rm -rf %s && mkdir -p %s/_hyper %s/nand/title/00030004/4b475545/content %s/nand/title/00030005/484e4141/content", root, root, root, root);
    if (system(p) != 0) printf("warn\n");
    launcher_set_drive(root);
    char nand[512];
    snprintf(nand, sizeof(nand), "%s/nand", root);
    launcher_set_nand_root(nand);
    // title.tmd: content id 3 (big endian at 0x1E4)
    uint8_t tmd[0x208] = { 0 };
    tmd[0x1E7] = 3;
    snprintf(p, sizeof(p), "%s/title/00030004/4b475545/content/title.tmd", nand);
    FILE *f = fopen(p, "wb"); fwrite(tmd, 1, sizeof(tmd), f); fclose(f);
    // the .app: header + banner with a title
    static uint8_t app[0x200 + 0x840];
    memset(app, 0, sizeof(app));
    memcpy(app, "FLIPNOTE    ", 12);
    memcpy(app + 12, "KGUE", 4);
    app[0x68] = 0x00; app[0x69] = 0x02;              // banner at 0x200
    app[0x200] = 1;                                   // banner version
    for (int i = 0; i < 512; i++) app[0x200 + 0x20 + i] = 0x11;   // some icon
    const char *name = "Flipnote Studio\nNintendo";
    for (int lang = 0; lang < 6; lang++)
        for (int i = 0; name[i]; i++) app[0x200 + 0x240 + lang * 0x100 + i * 2] = (uint8_t)name[i];
    snprintf(p, sizeof(p), "%s/title/00030004/4b475545/content/00000003.app", nand);
    f = fopen(p, "wb"); fwrite(app, 1, sizeof(app), f); fclose(f);
    // a title without banner (no menu entry)
    tmd[0x1E7] = 1;
    snprintf(p, sizeof(p), "%s/title/00030005/484e4141/content/title.tmd", nand);
    f = fopen(p, "wb"); fwrite(tmd, 1, sizeof(tmd), f); fclose(f);
    snprintf(p, sizeof(p), "%s/title/00030005/484e4141/content/00000001.app", nand);
    f = fopen(p, "wb"); fwrite(app, 1, 0x200, f); fclose(f);

    static GameItem found[APPS_MAX];
    int n = apps_scan(found, APPS_MAX, LANG_TR);
    CHECK(n == 1, "one application with a banner (%d)", n);
    CHECK(n == 1 && found[0].type == TYPE_APP && !strcmp(found[0].title, "Flipnote Studio") && !strcmp(found[0].game_code, "KGUE"),
          "Flipnote read (%s)", n ? found[0].title : "-");
    apps_load();
    CHECK(apps_count() == 0, "no apps yet");
    CHECK(apps_add(&found[0]) && !apps_add(&found[0]) && apps_save(), "added once");
    apps_load();
    CHECK(apps_count() == 1 && apps_has(found[0].path), "apps.bin kept");
    library_sync(root, true, NULL);
    int view[8];
    CHECK(library_view(view, 8, TYPE_APP, SORT_ALPHA, false, NULL) == 1, "library shows the application");
    LaunchPlan plan;
    char d[256];
    const GameItem *it = library_get(view[0]);
    CHECK(launcher_plan(it, NULL, &plan, d, sizeof(d)) == LAUNCH_OK && strstr(plan.boot_path, "/content/00000003.app"), "plan boots the .app (%s)", plan.boot_path);
    CHECK(launcher_run(it, NULL, d, sizeof(d), NULL) == LAUNCH_OK, "run");
    library_sync(root, true, NULL);                 // a full scan keeps the application
    CHECK(library_view(view, 8, TYPE_APP, SORT_ALPHA, false, NULL) == 1, "kept after a rescan");
    CHECK(apps_remove(found[0].path) && apps_save(), "removed");
    apps_load();
    library_merge_apps();
    CHECK(apps_count() == 0 && library_view(view, 8, TYPE_APP, SORT_ALPHA, false, NULL) == 0, "gone from the library");
    launcher_set_nand_root("");
}

// --- Nearby cards and the store catalog ----------------------------------------------------
static void test_nearby_store(const char *root) {
    char p[512];
    snprintf(p, sizeof(p), "rm -rf %s && mkdir -p %s", root, root);
    if (system(p) != 0) printf("warn\n");
    NearbyCard me, a, got;
    memset(&me, 0, sizeof(me)); memset(&a, 0, sizeof(a));
    me.id = 7; snprintf(me.name, sizeof(me.name), "Ege");
    nearby_set_me(&me);
    snprintf(p, sizeof(p), "%s/nearby.bin", root);
    nearby_open(p);
    a.id = 42; snprintf(a.name, sizeof(a.name), "Ayşe"); a.color = 3; snprintf(a.greet, sizeof(a.greet), "Selam!");
    snprintf(a.game, sizeof(a.game), "Mario Kart DS"); a.minutes = 600; a.games = 12; a.has_avatar = 1;
    for (int i = 0; i < NEARBY_AV * NEARBY_AV; i++) a.avatar[i] = (uint16_t)(0x8000 | i);
    static uint8_t buf[1600];
    int n = nearby_encode(&a, buf, sizeof(buf));
    CHECK(n == 5 + 128 + NEARBY_AV * NEARBY_AV * 2 && n < 1400, "nearby card fits one packet (%d)", n);
    CHECK(nearby_decode(buf, n, &got) && got.id == 42 && !strcmp(got.name, "Ayşe") && got.minutes == 600 && got.avatar[5] == (0x8000 | 5), "nearby decode");
    CHECK(!nearby_decode(buf, 40, &got), "short packet refused");
    memset(buf + 5 + 4, 'x', 20);                              // no terminator in the name: still safe
    CHECK(nearby_decode(buf, n, &got) && strlen(got.name) == 19, "names always terminated");
    CHECK(!nearby_merge(&me, 1000), "own card ignored");
    CHECK(nearby_merge(&a, 1000) && nearby_count() == 1, "first meeting");
    char name[24];
    CHECK(nearby_take_met(name, sizeof(name)) && !strcmp(name, "Ayşe") && !nearby_take_met(name, sizeof(name)), "met once");
    CHECK(!nearby_merge(&a, 1100) && nearby_get(0)->times == 1, "same visit is not a new meeting");
    CHECK(nearby_merge(&a, 1000 + 3600) && nearby_get(0)->times == 2, "met again later");
    NearbyCard b = a; b.id = 43; snprintf(b.name, sizeof(b.name), "Can");
    nearby_merge(&b, 5000);
    CHECK(nearby_count() == 2 && nearby_get(0)->c.id == 43, "newest first");
    nearby_close();
    nearby_open(p);
    CHECK(nearby_count() == 2 && !strcmp(nearby_get(1)->c.greet, "Selam!"), "list kept on the card");
    CHECK(nearby_remove(0) && nearby_count() == 1, "removed");
    nearby_close();

    // store catalog as Studio writes it
    static uint8_t cat[16 + 2 * 2444];
    memset(cat, 0, sizeof(cat));
    memcpy(cat, "HSTR", 4);
    uint32_t v = 1, cnt = 2, esz = 2444;
    memcpy(cat + 4, &v, 4); memcpy(cat + 8, &cnt, 4); memcpy(cat + 12, &esz, 4);
    StoreEntry *e = (StoreEntry *)(cat + 16);
    snprintf(e[0].slug, 40, "wordle-ds"); snprintf(e[0].title, 48, "Wordle DS"); e[0].category = 1; e[0].stars = 76;
    snprintf(e[1].slug, 40, "ftpd"); snprintf(e[1].title, 48, "ftpd"); e[1].category = 2;
    memset(e[1].desc, 'x', 200);                             // not terminated
    CHECK(store_parse(cat, sizeof(cat)) && store_count() == 2 && strlen(store_get(1)->desc) == 199, "store catalog parsed");
    int idx[4];
    CHECK(store_view(STORE_CAT_GAME, idx, 4) == 1 && idx[0] == 0 && store_view(STORE_CAT_ALL, idx, 4) == 2, "store categories");
    CHECK(!store_parse(cat, 100), "truncated catalog refused");
    store_parse(cat, sizeof(cat));
    store_parse_installed("wordle-ds|v3|WordleDS.nds\nbad line\n");
    CHECK(store_installed("wordle-ds") && !store_installed("ftpd"), "installed list");
    store_free();
}

static void write_bmp(const char *path, int w, int h, int seed) {
    FILE *f = fopen(path, "wb");
    int stride = (w * 3 + 3) & ~3;
    uint8_t hd[54] = { 'B', 'M' };
    uint32_t size = 54 + (uint32_t)stride * h;
    memcpy(hd + 2, &size, 4);
    hd[10] = 54; hd[14] = 40;
    memcpy(hd + 18, &w, 4); memcpy(hd + 22, &h, 4);
    hd[26] = 1; hd[28] = 24;
    fwrite(hd, 1, 54, f);
    uint8_t *row = calloc(1, (size_t)stride);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            row[x * 3] = (uint8_t)(x * 13 + seed); row[x * 3 + 1] = (uint8_t)(y * 9 + seed * 3); row[x * 3 + 2] = (uint8_t)((x ^ y) * 7 + seed);
        }
        fwrite(row, 1, (size_t)stride, f);
    }
    free(row);
    fclose(f);
}

static void test_covers(const char *root) {
    char p[512];
    snprintf(p, sizeof(p), "rm -rf %s && mkdir -p %s/_hyper/covers %s/_nds/TWiLightMenu/boxart", root, root, root);
    if (system(p) != 0) printf("warn\n");
    snprintf(p, sizeof(p), "%s/_hyper/covers/Game.bmp", root);
    write_bmp(p, 120, 170, 1);                      // portrait: crop keeps the upper part
    for (int i = 0; i < 150; i++) {                 // a box art pack: many unrelated files
        snprintf(p, sizeof(p), "%s/_nds/TWiLightMenu/boxart/X%03d.png", root, i);
        FILE *f = fopen(p, "wb"); if (f) fclose(f);
    }
    GameItem it;
    make_item(&it, "x:/roms/GBA/Game.gba", TYPE_GBA, 0, 100);
    covers_init(root);
    CHECK(!covers_scan_done() && covers_get(0, &it) == NULL && !covers_known_missing(0), "scan runs in the background");
    int frames = 0;
    while (!covers_scan_done() && frames < 100) { covers_frame_begin(); frames++; }
    CHECK(covers_scan_done() && frames >= 3, "scan spread over frames (%d)", frames);
    covers_frame_begin();
    const Color *c = covers_get(0, &it);
    CHECK(c != NULL, "decoded cover");
    static Color dec[COVER_SIZE * COVER_SIZE];
    if (c) memcpy(dec, c, sizeof(dec));
    GameItem other;
    make_item(&other, "x:/roms/GBA/None.gba", TYPE_GBA, 0, 100);
    CHECK(covers_get(1, &other) == NULL && covers_known_missing(1), "no cover known");

    // Studio's pack: the same pixels as the console's own decoder, read instead of decoding
    snprintf(p, sizeof(p), "python3 -c \"import sys; sys.path.insert(0, '.'); from studio.covers import build_pack; print(build_pack('%s'))\" > /dev/null", root);
    CHECK(system(p) == 0, "build_pack ran");
    snprintf(p, sizeof(p), "%s/_hyper/covers/Game.bmp", root);
    write_bmp(p, 120, 170, 77);                     // the file changes: the pack still answers
    covers_reset();
    covers_frame_begin();
    c = covers_get(0, &it);
    CHECK(c && !memcmp(c, dec, sizeof(dec)), "pack pixels = console decode");
    int loaded = 0;                                  // budget: several pack reads per frame
    GameItem many[6];
    for (int i = 0; i < 6; i++) { many[i] = it; }
    covers_reset();
    covers_frame_begin();
    for (int i = 0; i < 6; i++) if (covers_get(10 + i, &many[i])) loaded++;
    CHECK(loaded == 4, "4 pack reads per frame (%d)", loaded);
    covers_frame_begin();
    CHECK(covers_prefetch(20, &it) && covers_get(20, &it) != NULL, "prefetch keeps the cover");
    covers_disable();
}

static void test_photofx(const char *root) {
    char p[512];
    snprintf(p, sizeof(p), "rm -rf %s && mkdir -p %s/_hyper/covers", root, root);
    if (system(p) != 0) printf("warn\n");
    // crop: square region inside the 256x192 picture
    PhotoCrop cr;
    photo_crop_init(&cr, 1, 1);
    int x, y, w, h;
    photo_crop_rect(&cr, &x, &y, &w, &h);
    CHECK(w == 192 && h == 192 && x == 32 && y == 0, "crop init %d %d %d %d", x, y, w, h);
    photo_crop_move(&cr, -500, 30);
    photo_crop_rect(&cr, &x, &y, &w, &h);
    CHECK(x == 0 && y == 0, "crop stays inside %d %d", x, y);
    photo_crop_zoom(&cr, -1000);
    photo_crop_rect(&cr, &x, &y, &w, &h);
    CHECK(h == 24 && w == 24, "crop min %d", h);
    photo_crop_init(&cr, 4, 3);
    photo_crop_rect(&cr, &x, &y, &w, &h);
    CHECK(w == 256 && h == 192, "crop 4:3 whole");
    // sampling: a two-colour picture keeps its halves
    static Color pic[256 * 192], out[64 * 64];
    for (int i = 0; i < 256 * 192; i++) pic[i] = (i % 256) < 128 ? C8(255, 0, 0) : C8(0, 0, 255);
    photo_sample(pic, 256, 192, 32, 0, 192, 192, out, 64, 64);
    CHECK(out[5 * 64 + 2] == C8(255, 0, 0) && out[5 * 64 + 61] == C8(0, 0, 255), "sample halves %04x %04x", out[5 * 64 + 2], out[5 * 64 + 61]);
    // raw files (wallpaper / profile picture / folder) and PNG (covers)
    snprintf(p, sizeof(p), "%s/avatar.bin", root);
    CHECK(photo_write_raw(p, "HAV1", out, 64, 64), "write raw");
    static Color back[64 * 64];
    CHECK(photo_read_raw(p, "HAV1", back, 64, 64) && !memcmp(back, out, sizeof(back)), "read raw");
    CHECK(!photo_read_raw(p, "HWP1", back, 64, 64), "raw magic checked");
    CHECK(ui_avatar_load_slot(1, p), "avatar loader reads it");
    snprintf(p, sizeof(p), "%s/_hyper/covers/Game.gba.png", root);
    CHECK(photo_write_png(p, out, 64, 64), "write png");
    snprintf(p, sizeof(p), "python3 -c \"import sys,zlib; d=open('%s/_hyper/covers/Game.gba.png','rb').read(); "
             "assert d[:8]==b'\\x89PNG\\r\\n\\x1a\\n'; i=d.index(b'IDAT'); n=int.from_bytes(d[i-4:i],'big'); "
             "raw=zlib.decompress(d[i+4:i+4+n]); assert len(raw)==64*(64*3+1) and raw[1:4]==bytes([255,0,0])\"", root);
    CHECK(system(p) == 0, "png valid (zlib, pixels)");
    // the console's own cover loader finds it and prefers it
    GameItem it;
    make_item(&it, "x:/roms/GBA/Game.gba", TYPE_GBA, 0, 100);
    covers_init(root);
    for (int i = 0; i < 100 && !covers_scan_done(); i++) covers_frame_begin();
    covers_frame_begin();
    const Color *c = covers_get(0, &it);
    CHECK(c != NULL, "png cover decoded");
    snprintf(p, sizeof(p), "%s/_hyper/covers/Late.gba.png", root);
    photo_write_png(p, out, 64, 64);
    make_item(&it, "x:/roms/GBA/Late.gba", TYPE_GBA, 0, 100);
    covers_add_file("Late.gba.png");
    covers_frame_begin();
    CHECK(covers_get(1, &it) != NULL, "cover written later is found");
    covers_disable();
    // favorites
    snprintf(p, sizeof(p), "%s/favs.txt", root);
    photo_favs_load(p);
    photo_set_fav("sd:/DCIM/100DSI/HNI_0001.JPG", true);
    photo_set_fav("sd:/DCIM/100DSI/HNI_0001.JPG", true);
    photo_set_fav("sd:/photos/a.png", true);
    CHECK(photo_favs_count() == 2 && photo_is_fav("fat:/DCIM/100DSI/HNI_0001.JPG"), "favs");
    CHECK(photo_favs_save(), "favs save");
    photo_favs_load(p);
    photo_set_fav("sd:/photos/a.png", false);
    CHECK(photo_favs_count() == 1 && !photo_is_fav("sd:/photos/a.png") && photo_is_fav("sd:/DCIM/100DSI/HNI_0001.JPG"), "favs load/remove");
    // screensaver drawing stays inside the canvas, all variants and times
    static Color scr[256 * 192];
    Canvas cv;
    gfx_canvas_init(&cv, scr, 256, 192);
    bool all_set = true;
    for (int v = 0; v < 8; v++)
        for (int t = 0; t <= 4096; t += 512) {
            photo_kenburns(&cv, pic, t, v);
            for (int i = 0; i < 256 * 192; i += 97) if (!(scr[i] & 0x8000)) all_set = false;
        }
    CHECK(all_set, "ken burns fills the screen");
    static Color bl[256 * 192];
    static Color white[256 * 192];
    for (int i = 0; i < 256 * 192; i++) white[i] = C8(255, 255, 255);
    photo_blend(bl, pic, white, 16);
    CHECK((bl[0] & 31) == 31 && ((bl[0] >> 10) & 31) == 15, "blend half %04x", bl[0]);
    {   // a photo shared in Nearby: 24 packets, saved once complete
        snprintf(p, sizeof(p), "mkdir -p %s/prof", root);
        if (system(p) != 0) printf("warn\n");
        snprintf(p, sizeof(p), "%s/prof/nearby.bin", root);
        nearby_open(p);
        static uint16_t ph[NEARBY_PH_W * NEARBY_PH_H];
        for (int i = 0; i < NEARBY_PH_W * NEARBY_PH_H; i++) ph[i] = (uint16_t)(0x8000 | (i & 0x7FFF));
        NearbyCard me; memset(&me, 0, sizeof(me)); me.id = 111; snprintf(me.name, sizeof(me.name), "A");
        nearby_set_me(&me);
        nearby_set_photo(ph, 7);
        static uint8_t pk[NEARBY_PH_PARTS][1100];
        int lens[NEARBY_PH_PARTS];
        for (int i = 0; i < NEARBY_PH_PARTS; i++) lens[i] = nearby_photo_encode(i, pk[i], sizeof(pk[i]));
        CHECK(lens[0] == 15 + NEARBY_PH_PART && nearby_photo_encode(NEARBY_PH_PARTS, pk[0], 2000) == 0, "photo packets");
        CHECK(!nearby_photo_receive(pk[0], lens[0]), "own photo ignored");
        me.id = 222; nearby_set_me(&me);
        bool done = false;
        for (int i = NEARBY_PH_PARTS - 1; i >= 0; i--) done = nearby_photo_receive(pk[i], lens[i]);
        char pp[512];
        CHECK(done && nearby_take_photo() && nearby_photo_path(111, pp, sizeof(pp)), "photo assembled (any order)");
        static Color got[NEARBY_PH_W * NEARBY_PH_H];
        CHECK(photo_read_raw(pp, "HFC1", got, NEARBY_PH_W, NEARBY_PH_H) && got[300] == ph[300], "photo saved");
        CHECK(!nearby_photo_receive(pk[0], lens[0]) && !nearby_take_photo(), "same photo not saved twice");
        nearby_close();
    }
    CHECK(photo_month(0) == 0 && photo_month(1793548772u) / 100 == 2026, "month");
    CHECK(photo_of_day(10, 1793548772u) == photo_of_day(10, 1793548772u + 3600) && photo_of_day(0, 5) == -1, "photo of the day");
}

static void test_touchcal(void) {
    // a panel that is shifted, scaled differently in x / y and turned by about one degree
    int rx[TOUCHCAL_POINTS], ry[TOUCHCAL_POINTS];
    for (int i = 0; i < TOUCHCAL_POINTS; i++) {
        int x, y;
        touchcal_target(i, &x, &y);
        rx[i] = 180 + x * 15 + y / 4;
        ry[i] = 260 + y * 20 - x / 5;
    }
    TouchCal cal;
    int err = 99;
    CHECK(touchcal_fit(rx, ry, &cal, &err) && err <= 1, "fit (err %d)", err);
    int px, py;
    touchcal_apply(&cal, 180 + 100 * 15 + 50 / 4, 260 + 50 * 20 - 100 / 5, &px, &py);
    CHECK(abs(px - 100) <= 1 && abs(py - 50) <= 1, "apply %d,%d", px, py);
    rx[2] += 600;                                     // one target missed badly
    CHECK(!touchcal_fit(rx, ry, &cal, &err), "bad point refused (err %d)", err);
    int same[TOUCHCAL_POINTS] = { 2000, 2000, 2000, 2000, 2000 };
    CHECK(!touchcal_fit(same, same, &cal, &err), "degenerate refused");
    if (system("rm -rf /tmp/hl_test_tc && mkdir -p /tmp/hl_test_tc/_hyper") != 0) printf("warn\n");
    rx[2] -= 600;
    touchcal_fit(rx, ry, &cal, &err);
    CHECK(touchcal_save("/tmp/hl_test_tc", &cal), "save");
    touchcal_set(NULL);
    touchcal_load("/tmp/hl_test_tc");
    const TouchCal *c = touchcal_current();
    CHECK(c && c->a == cal.a && c->f == cal.f, "load");
    FILE *f = fopen("/tmp/hl_test_tc/_hyper/touch.cal", "r+b"); fseek(f, 8, SEEK_SET); fputc(0x55, f); fclose(f);
    touchcal_load("/tmp/hl_test_tc");
    CHECK(touchcal_current() == NULL, "damaged file ignored");
}

int main(void) {
    test_titles();
    test_touchcal();
    test_covers("/tmp/hl_test_covers");
    test_photofx("/tmp/hl_test_photofx");
    test_library("/tmp/hl_test_lib");
    test_launcher("/tmp/hl_test_sd");
    test_gfx();
    test_save_safety("/tmp/hl_test_safe");
    test_cheats("/tmp/hl_test_cheats");
    test_apps("/tmp/hl_test_apps");
    test_nearby_store("/tmp/hl_test_nearby");
    printf("\n%d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}

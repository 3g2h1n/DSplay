// Host stand-in for the console in controller mode (tests/test_pad.py drives it).
// Usage: HL_PAD_HOST=127.0.0.1 build/pad_host <sd dir> <seconds>
#include "pad.h"
#include "store.h"
#include "nearby.h"
#include "xfer.h"
#include "gfx.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *s_out;

static void dump(const char *name) {
    if (!s_out) return;
    pad_render_top(&g_top);
    pad_render_bottom(&g_bot);
    char path[512];
    snprintf(path, sizeof(path), "%s_%s.ppm", s_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n256 384\n255\n");
    for (int sc = 0; sc < 2; sc++) {
        Canvas *c = sc ? &g_bot : &g_top;
        for (int i = 0; i < 256 * 192; i++) {
            u16 p = c->px[i];
            unsigned char rgb[3] = { (unsigned char)((p & 31) << 3), (unsigned char)(((p >> 5) & 31) << 3), (unsigned char)(((p >> 10) & 31) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

static void sleep_frame(void) {
    struct timespec ts = { 0, 16666667 };
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    const char *sd = argc > 1 ? argv[1] : ".";
    int secs = argc > 2 ? atoi(argv[2]) : 10;
    s_out = argc > 3 ? argv[3] : NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    theme_apply(0);
    if (getenv("HL_PAD_NEARBY")) {
        // "Nearby": send our card, print the people we meet
        pad_enter_nearby(sd, "Ege", 1);
        NearbyCard me;
        memset(&me, 0, sizeof(me));
        me.id = 1234;
        snprintf(me.name, sizeof(me.name), "Ege");
        snprintf(me.greet, sizeof(me.greet), "Merhaba!");
        nearby_set_me(&me);
        char lp[512];
        snprintf(lp, sizeof(lp), "%s/nearby.bin", sd);
        nearby_open(lp);
        printf("READY\n");
        for (int f = 0; f < secs * 60; f++) {
            PadInput in;
            memset(&in, 0, sizeof(in));
            pad_frame(&in);
            uint32_t ip, mask;
            if (pad_net_info(&ip, &mask)) nearby_frame(ip, mask);
            char name[24];
            if (nearby_take_met(name, sizeof(name))) printf("MET %s %s\n", name, nearby_get(0)->c.greet);
            sleep_frame();
        }
        nearby_close();
        pad_leave();
        return 0;
    }
    bool store = getenv("HL_PAD_STORE") != NULL;
    if (store) pad_enter_store(sd, "Ege DSi", 1);
    else pad_enter(sd, "Ege DSi", 1);
    printf("CODE %u\n", (unsigned)pad_pair_code());
    if (store) {
        // store screen: ask for the catalog, then install its first program
        bool asked = false, asked2 = false;
        for (int f = 0; f < secs * 60; f++) {
            PadInput in;
            memset(&in, 0, sizeof(in));
            pad_frame(&in);
            if (pad_connected() && !asked) { asked = pad_store_request(1, ""); printf("ASKED\n"); }
            int op, st;
            char slug[40];
            if (pad_store_result(&op, &st, slug, sizeof(slug))) printf("RESULT %d %d %s\n", op, st, slug);
            if (pad_take_rescan()) {
                bool ok = store_load(sd);
                printf("CATALOG %d %d\n", ok ? store_count() : -1, store_count() ? store_installed(store_get(0)->slug) : 0);
                if (ok && store_count() && !asked2) { asked2 = pad_store_request(2, store_get(0)->slug); printf("INSTALL %s\n", store_get(0)->slug); }
            }
            sleep_frame();
        }
        pad_leave();
        return 0;
    }
    int connected_at = -1;
    for (int f = 0; f < secs * 60; f++) {
        PadInput in;
        memset(&in, 0, sizeof(in));
        if (pad_connected()) {
            if (connected_at < 0) { connected_at = f; printf("CONNECTED %d pages\n", pad_layout_pages()); }
            int t = f - connected_at;
            // script: A held 30..60, stick (widget 0) pushed right 90..120, page 2 trackpad drag 150..180
            if (t >= 30 && t < 60) in.held |= KEY_A;
            if (t == 30) in.down |= KEY_A;
            if (t >= 90 && t < 120) { in.touching = true; in.tx = 64 + 54; in.ty = 82; }
            if (t == 140) { in.touching = true; in.tx = 200; in.ty = 182; }
            if (t >= 150 && t < 180) { in.touching = true; in.tx = 40 + (t - 150) * 2; in.ty = 80; }
            if (t == 200) printf("DONE\n");
        }
        if (!pad_frame(&in)) break;
        if (f == 20) dump("search");
        if (connected_at >= 0 && f - connected_at == 100) dump("stick");
        if (connected_at >= 0 && f - connected_at == 165) dump("mouse");
        sleep_frame();
    }
    pad_leave();
    return 0;
}

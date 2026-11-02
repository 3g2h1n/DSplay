// Host simulator: runs DSplay against a folder that mimics the SD card and
// dumps screenshots. Usage: sim <sd-folder> <script> <out-prefix>
// Script commands: wait N | press KEY | touch X Y | drag X0 Y0 X1 Y1 | shot NAME
#ifndef ARM9
#include "common.h"
#include "app.h"
#include "gfx.h"
#include "launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n256 384\n255\n");
    for (int s = 0; s < 2; s++) {
        Canvas *c = s ? &g_bot : &g_top;
        for (int i = 0; i < 256 * 192; i++) {
            u16 p = c->px[i];
            unsigned char rgb[3] = { (unsigned char)((p & 31) << 3 | (p & 31) >> 2), (unsigned char)(((p >> 5) & 31) << 3 | ((p >> 5) & 31) >> 2),
                                     (unsigned char)(((p >> 10) & 31) << 3 | ((p >> 10) & 31) >> 2) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

static u32 keybit(const char *k) {
    static const char *names[] = { "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y" };
    for (int i = 0; i < 12; i++) if (!strcmp(k, names[i])) return 1u << i;
    return 0;
}

// the simulated panel: raw = a slightly shifted / scaled version of the pixels (calibration tests)
static void step(AppInput *in) {
    if (in->touching) { in->rawx = 200 + in->tx * 15 + (in->ty >> 3); in->rawy = 300 + in->ty * 19; }
    app_frame(in);
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: sim <sd> <script> <out>\n"); return 1; }
    launcher_set_drive(argv[1]);
    {   // a folder that looks like the DSi system memory, for the DSi applications
        char nand[512];
        snprintf(nand, sizeof(nand), "%s/_simnand", argv[1]);
        launcher_set_nand_root(nand);
    }
    app_init(true, false);
    FILE *sf = fopen(argv[2], "r");
    if (!sf) return 1;
    char line[256];
    AppInput idle;
    memset(&idle, 0, sizeof(idle));
    while (fgets(line, sizeof(line), sf)) {
        char cmd[32], a[64];
        int x0, y0, x1, y1, n;
        if (sscanf(line, "%31s", cmd) != 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "wait") && sscanf(line, "%*s %d", &n) == 1) { for (int i = 0; i < n; i++) step(&idle); }
        else if (!strcmp(cmd, "press") && sscanf(line, "%*s %63s", a) == 1) {
            AppInput in = idle;
            if (!strcmp(a, "POWER")) in.power = true;   // the DSi POWER button (tap)
            else in.down = in.repeat = in.held = keybit(a);
            step(&in);
            AppInput up = idle; up.up = keybit(a); step(&up);
            for (int i = 0; i < 8; i++) step(&idle);
        } else if (!strcmp(cmd, "levels") && sscanf(line, "%*s %d %d", &x0, &y0) == 2) {   // volume, brightness
            console_levels_sim(x0, y0);
        } else if (!strcmp(cmd, "tap") && sscanf(line, "%*s %63s", a) == 1) {   // one frame down, one up
            AppInput in = idle; in.down = in.repeat = in.held = keybit(a); step(&in);
            AppInput up = idle; up.up = keybit(a); step(&up);
        } else if (!strcmp(cmd, "touch") && sscanf(line, "%*s %d %d", &x0, &y0) == 2) {
            AppInput in = idle; in.down = in.held = KEY_TOUCH; in.touching = true; in.tx = x0; in.ty = y0; step(&in);
            AppInput h = in; h.down = 0; step(&h);
            AppInput up = idle; up.up = KEY_TOUCH; step(&up);
            for (int i = 0; i < 8; i++) step(&idle);
        } else if (!strcmp(cmd, "pen") && sscanf(line, "%*s %d %d", &x0, &y0) == 2) {   // stylus down, kept
            AppInput in = idle; in.down = in.held = KEY_TOUCH; in.touching = true; in.tx = x0; in.ty = y0; step(&in);
            AppInput h = in; h.down = 0; step(&h); step(&h);
        } else if (!strcmp(cmd, "penup")) {
            AppInput up = idle; up.up = KEY_TOUCH; step(&up);
        } else if (!strcmp(cmd, "drag") && sscanf(line, "%*s %d %d %d %d", &x0, &y0, &x1, &y1) == 4) {
            AppInput in = idle; in.down = in.held = KEY_TOUCH; in.touching = true; in.tx = x0; in.ty = y0; step(&in);
            for (int i = 1; i <= 10; i++) { AppInput h = idle; h.held = KEY_TOUCH; h.touching = true; h.tx = x0 + (x1 - x0) * i / 10; h.ty = y0 + (y1 - y0) * i / 10; step(&h); }
            AppInput up = idle; up.up = KEY_TOUCH; step(&up);
            for (int i = 0; i < 8; i++) step(&idle);
        } else if (!strcmp(cmd, "shot") && sscanf(line, "%*s %63s", a) == 1) {
            char p[512]; snprintf(p, sizeof(p), "%s_%s.ppm", argv[3], a); dump(p);
        }
    }
    fclose(sf);
    return 0;
}
#endif

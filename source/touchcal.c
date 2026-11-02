// Touch screen calibration (see include/touchcal.h)
#include "touchcal.h"
#include <stdio.h>
#include <string.h>

#define CAL_MAGIC 0x31435448u   // "HTC1"

static TouchCal s_cal;

void touchcal_target(int i, int *x, int *y) {
    static const int t[TOUCHCAL_POINTS][2] = { { 24, 24 }, { 232, 24 }, { 232, 156 }, { 24, 156 }, { 128, 90 } };
    if (i < 0 || i >= TOUCHCAL_POINTS) i = 0;
    *x = t[i][0];
    *y = t[i][1];
}

// 3x3 linear system by Gaussian elimination with partial pivoting
static bool solve3(double m[3][4], double out[3]) {
    for (int c = 0; c < 3; c++) {
        int p = c;
        for (int r = c + 1; r < 3; r++) if ((m[r][c] < 0 ? -m[r][c] : m[r][c]) > (m[p][c] < 0 ? -m[p][c] : m[p][c])) p = r;
        if ((m[p][c] < 0 ? -m[p][c] : m[p][c]) < 1e-9) return false;
        if (p != c) for (int k = 0; k < 4; k++) { double t = m[c][k]; m[c][k] = m[p][k]; m[p][k] = t; }
        for (int r = 0; r < 3; r++) {
            if (r == c) continue;
            double f = m[r][c] / m[c][c];
            for (int k = c; k < 4; k++) m[r][k] -= f * m[c][k];
        }
    }
    for (int i = 0; i < 3; i++) out[i] = m[i][3] / m[i][i];
    return true;
}

bool touchcal_fit(const int rawx[TOUCHCAL_POINTS], const int rawy[TOUCHCAL_POINTS], TouchCal *cal, int *max_err) {
    // least squares: [rx ry 1] * [a b c]^T = tx  (and the same for ty)
    double ata[3][3] = { { 0 } }, atx[3] = { 0 }, aty[3] = { 0 };
    for (int i = 0; i < TOUCHCAL_POINTS; i++) {
        int tx, ty;
        touchcal_target(i, &tx, &ty);
        double v[3] = { rawx[i], rawy[i], 1.0 };
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) ata[r][c] += v[r] * v[c];
            atx[r] += v[r] * tx;
            aty[r] += v[r] * ty;
        }
    }
    double mx[3][4], my[3][4], sx[3], sy[3];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) mx[r][c] = my[r][c] = ata[r][c];
        mx[r][3] = atx[r];
        my[r][3] = aty[r];
    }
    if (!solve3(mx, sx) || !solve3(my, sy)) return false;
    // the panel spans roughly 4096 raw steps over 256 x 192 pixels: the main scale must be close
    if (sx[0] < 0.02 || sx[0] > 0.2 || sy[1] < 0.015 || sy[1] > 0.2) return false;
    if ((sx[1] < 0 ? -sx[1] : sx[1]) > 0.02 || (sy[0] < 0 ? -sy[0] : sy[0]) > 0.02) return false;   // a few degrees at most
    TouchCal t;
    t.a = (int32_t)(sx[0] * 65536.0 + (sx[0] < 0 ? -0.5 : 0.5));
    t.b = (int32_t)(sx[1] * 65536.0 + (sx[1] < 0 ? -0.5 : 0.5));
    t.c = (int32_t)(sx[2] * 65536.0 + (sx[2] < 0 ? -0.5 : 0.5));
    t.d = (int32_t)(sy[0] * 65536.0 + (sy[0] < 0 ? -0.5 : 0.5));
    t.e = (int32_t)(sy[1] * 65536.0 + (sy[1] < 0 ? -0.5 : 0.5));
    t.f = (int32_t)(sy[2] * 65536.0 + (sy[2] < 0 ? -0.5 : 0.5));
    t.valid = true;
    int worst = 0;
    for (int i = 0; i < TOUCHCAL_POINTS; i++) {
        int px, py, tx, ty;
        touchcal_apply(&t, rawx[i], rawy[i], &px, &py);
        touchcal_target(i, &tx, &ty);
        int e = (px > tx ? px - tx : tx - px) + (py > ty ? py - ty : ty - py);
        if (e > worst) worst = e;
    }
    if (max_err) *max_err = worst;
    if (worst > 8) return false;               // one target was missed badly: do it again
    *cal = t;
    return true;
}

void touchcal_apply(const TouchCal *cal, int rawx, int rawy, int *px, int *py) {
    int64_t x = ((int64_t)cal->a * rawx + (int64_t)cal->b * rawy + cal->c + 32768) >> 16;
    int64_t y = ((int64_t)cal->d * rawx + (int64_t)cal->e * rawy + cal->f + 32768) >> 16;
    if (x < 0) x = 0;
    if (x > 255) x = 255;
    if (y < 0) y = 0;
    if (y > 191) y = 191;
    *px = (int)x;
    *py = (int)y;
}

static void cal_path(const char *drv, char *out, size_t len) { snprintf(out, len, "%s/_hyper/touch.cal", drv ? drv : ""); }

void touchcal_load(const char *drv) {
    memset(&s_cal, 0, sizeof(s_cal));
    char p[MAX_PATH_LEN];
    cal_path(drv, p, sizeof(p));
    FILE *f = fopen(p, "rb");
    if (!f) return;
    int32_t v[8];
    if (fread(v, 4, 8, f) == 8 && (uint32_t)v[0] == CAL_MAGIC) {
        int32_t sum = 0;
        for (int i = 1; i < 7; i++) sum ^= v[i] * (i + 1);
        if (sum == v[7]) {
            s_cal.a = v[1]; s_cal.b = v[2]; s_cal.c = v[3];
            s_cal.d = v[4]; s_cal.e = v[5]; s_cal.f = v[6];
            s_cal.valid = true;
        }
    }
    fclose(f);
}

bool touchcal_save(const char *drv, const TouchCal *cal) {
    char p[MAX_PATH_LEN], tmp[MAX_PATH_LEN + 4];
    cal_path(drv, p, sizeof(p));
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    int32_t v[8] = { (int32_t)CAL_MAGIC, cal->a, cal->b, cal->c, cal->d, cal->e, cal->f, 0 };
    for (int i = 1; i < 7; i++) v[7] ^= v[i] * (i + 1);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(v, 4, 8, f) == 8;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    remove(p);
    if (rename(tmp, p) != 0) return false;
    s_cal = *cal;
    s_cal.valid = true;
    return true;
}

bool touchcal_remove(const char *drv) {
    char p[MAX_PATH_LEN];
    cal_path(drv, p, sizeof(p));
    remove(p);
    memset(&s_cal, 0, sizeof(s_cal));
    return true;
}

const TouchCal *touchcal_current(void) { return s_cal.valid ? &s_cal : NULL; }
void touchcal_set(const TouchCal *cal) { if (cal) s_cal = *cal; else memset(&s_cal, 0, sizeof(s_cal)); }

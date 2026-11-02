#ifndef TOUCHCAL_H
#define TOUCHCAL_H
// DSplay's own touch screen calibration (Settings > Touch screen): five targets, a least-squares
// affine fit raw ADC -> pixels, so offset, scale and a slightly rotated / skewed panel are all
// corrected. Kept for the console in <drv>/_hyper/touch.cal; games keep the system calibration.
#include "common.h"

#define TOUCHCAL_POINTS 5

typedef struct {
    int32_t a, b, c;   // px = (a * rawx + b * rawy + c) >> 16
    int32_t d, e, f;   // py = (d * rawx + e * rawy + f) >> 16
    bool    valid;
} TouchCal;

// target i of TOUCHCAL_POINTS on the 256x192 screen
void touchcal_target(int i, int *x, int *y);
// fits raw[i] -> target i; false (cal untouched) when the points do not make sense
bool touchcal_fit(const int rawx[TOUCHCAL_POINTS], const int rawy[TOUCHCAL_POINTS], TouchCal *cal, int *max_err);
void touchcal_apply(const TouchCal *cal, int rawx, int rawy, int *px, int *py);

void touchcal_load(const char *drv);           // reads <drv>/_hyper/touch.cal (no file: not valid)
bool touchcal_save(const char *drv, const TouchCal *cal);
bool touchcal_remove(const char *drv);         // back to the system calibration
const TouchCal *touchcal_current(void);
void touchcal_set(const TouchCal *cal);        // NULL: none

#endif // TOUCHCAL_H

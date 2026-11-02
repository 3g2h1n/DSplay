#ifndef CART_ICONS_H
#define CART_ICONS_H
#include "common.h"
#include "gfx.h"
// 32x32 RGB555 cartridge art (bit 15 = opaque) for a non-DS system, or NULL
const Color *cart_icon(ItemType type);
// Empty label of that cartridge (icon pixels, inclusive) and the short system name written on it
bool cart_label(ItemType type, int *x0, int *y0, int *x1, int *y1, const char **name);
#endif

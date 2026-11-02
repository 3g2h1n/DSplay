#ifndef SLOT1_H
#define SLOT1_H

// Physical DS game card (Slot-1) detection on the DSi.
#include "common.h"

void            slot1_init(bool enable);      // enable only when running from the DSi SD card
bool            slot1_poll(void);             // true when the card was inserted or removed
bool            slot1_present(void);
const GameItem *slot1_item(void);             // NULL when no card
bool            slot1_header_valid(const uint8_t *hdr);
void            slot1_fill_from_header(GameItem *it, const uint8_t *hdr);
// Reads the save chip of the inserted card (read only). Returns its size, 0 when it cannot be read.
uint32_t        slot1_read_save(uint8_t **out, uint32_t max_size);

#endif // SLOT1_H

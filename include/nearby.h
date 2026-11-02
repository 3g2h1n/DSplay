#ifndef NEARBY_H
#define NEARBY_H
// "Nearby" (StreetPass-like): two consoles running DSplay on the same Wi-Fi network show each
// other a small profile card and keep a list of the people they met.
//
// UDP port 39513, broadcast every 2 s while the screen is open:
//   "HNBR", u8 version (1), u32 id, name[20], u8 color, u8 has_avatar, greeting[48],
//   favorite game[48], u32 minutes played, u16 games, [24 x 24 RGB555 avatar]
#include "common.h"

#define NEARBY_PORT   39513
#define NEARBY_AV     24
#define NEARBY_MAX    60
// A shared photo (Photos > Share with Nearby): 128x96 RGB555 in 24 packets of 1 KB after each card
//   "HNPH", u8 version (1), u32 id, u32 stamp (changes with the photo), u8 part, u8 parts, 1024 bytes
// Received photos are kept as <list folder>/nearby_photos/<id>.bin ("HFC1" 128x96).
#define NEARBY_PH_W   128
#define NEARBY_PH_H   96
#define NEARBY_PH_PART 1024
#define NEARBY_PH_PARTS (NEARBY_PH_W * NEARBY_PH_H * 2 / NEARBY_PH_PART)

typedef struct {
    uint32_t id;
    char     name[20];
    uint8_t  color;
    uint8_t  has_avatar;
    char     greet[48];
    char     game[48];
    uint32_t minutes;
    uint16_t games;
    uint16_t avatar[NEARBY_AV * NEARBY_AV];
} NearbyCard;

typedef struct {
    NearbyCard c;
    uint32_t   first_met, last_met;   // unix time
    uint16_t   times;                 // meetings (at least 30 minutes apart)
    uint16_t   _pad;
} NearbyPerson;

void nearby_set_me(const NearbyCard *me);
void nearby_open(const char *list_path);      // loads the people met before
void nearby_close(void);                      // saves, closes the socket
// one frame while the screen is open and Wi-Fi is up (ip / mask in network order, 0 on the PC)
void nearby_frame(uint32_t ip, uint32_t mask);
int  nearby_count(void);
const NearbyPerson *nearby_get(int i);       // newest meeting first
bool nearby_here(int i);                      // heard in the last 10 seconds
bool nearby_take_met(char *name, size_t len); // someone new (or again) since the last call
bool nearby_remove(int i);
void nearby_set_photo(const uint16_t *px, uint32_t stamp);   // NULL: nothing shared
bool nearby_photo_path(uint32_t id, char *out, size_t len);   // false: no photo from that person
bool nearby_take_photo(void);                                  // a photo arrived since the last call

// (host tests)
int  nearby_encode(const NearbyCard *c, uint8_t *out, int max);
bool nearby_decode(const uint8_t *d, int len, NearbyCard *c);
bool nearby_merge(const NearbyCard *c, uint32_t now);   // true when it counts as a meeting
int  nearby_photo_encode(int part, uint8_t *out, int max);  // one packet of the shared photo (0: none)
bool nearby_photo_receive(const uint8_t *d, int len);       // true when a whole photo was saved

#endif // NEARBY_H

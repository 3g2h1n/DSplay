#ifndef CHEATS_H
#define CHEATS_H
// Action Replay cheats for Nintendo DS games, from the usual R4 / TWiLight Menu++ database
// (usrcheat.dat). The choices are stored in the database itself, exactly like TWiLight Menu++
// does, so both menus agree. Before a game starts, the chosen codes go to
// _nds/nds-bootstrap/cheatData.bin, where nds-bootstrap reads them.
#include "common.h"

#define CHEAT_MAX_ROWS 600

typedef struct {
    const char     *name;      // UTF-8, points into the loaded block
    const char     *note;
    const uint32_t *codes;     // ncodes words (pairs of Action Replay words)
    uint32_t        flag_pos;  // file offset of the "chosen" byte (0 for folders)
    uint16_t        ncodes;
    uint8_t         folder;    // 1 = folder heading
    uint8_t         one;       // folder where only one cheat can be chosen
    uint8_t         on;        // chosen
    int16_t         parent;    // row of the folder, -1 = none
} CheatRow;

// Database file on the card ("" when there is none).
bool cheats_db_path(char *out, size_t len);
// Loads the cheats of a ROM: >0 rows, 0 = no cheats for this game, -1 = no database, -2 = error
int  cheats_open(const char *rom_path);
int  cheats_count(void);
const CheatRow *cheats_row(int i);
void cheats_toggle(int i);
int  cheats_chosen(void);
bool cheats_dirty(void);
bool cheats_save(void);            // writes the changed choices back into usrcheat.dat
void cheats_close(void);
const char *cheats_game_title(void);

// Writes the chosen codes of the ROM to _nds/nds-bootstrap/cheatData.bin, or removes that
// file when there are none (returns the number of chosen cheats).
int  cheats_prepare(const char *rom_path);

#endif // CHEATS_H

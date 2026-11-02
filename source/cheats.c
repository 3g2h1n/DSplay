// Action Replay cheats from usrcheat.dat (see include/cheats.h).
//
// usrcheat.dat ("R4 CheatCode"):
//   0x000  "R4 CheatCode" ...
//   0x100  index: { u32 game code, u32 crc32 of the 512-byte ROM header, u64 offset } ...,
//          ended by an entry whose offset is 0
//   block  game title\0, padded to 4; u32 count (low 28 bits); 8 more words; then items:
//          folder : u32 (bit 28 set, bits 24-27 = 1 "only one", low 24 = number of cheats),
//                   name\0 note\0 padded to 4, followed by its cheats
//          cheat  : u32 (top byte = chosen, low 24 = size in words after this one),
//                   name\0 note\0 padded to 4, u32 n, n code words
// The same layout and rules as TWiLight Menu++ (romsel_dsimenutheme/arm9/source/cheat.cpp).
#include "cheats.h"
#include "launcher.h"
#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BLOCK (512 * 1024)
#define MAX_CHEAT_FILE 0x4000      // nds-bootstrap's limit (TWiLight Menu++ checks the same)

static char     s_db[MAX_PATH_LEN];
static uint8_t *s_buf;
static uint32_t s_buf_len, s_block_pos;
static CheatRow s_rows[CHEAT_MAX_ROWS];
static uint8_t  s_orig[CHEAT_MAX_ROWS];
static int      s_n;

static uint32_t crc32_buf(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
    }
    return c;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool cheats_db_path(char *out, size_t len) {
    static const char *const cands[] = { "/_nds/TWiLightMenu/extras/usrcheat.dat", "/_hyper/usrcheat.dat", "/usrcheat.dat" };
    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
        snprintf(out, len, "%s%s", get_drive_prefix(), cands[i]);
        FILE *f = fopen(out, "rb");
        if (f) { fclose(f); return true; }
    }
    if (len) out[0] = '\0';
    return false;
}

static bool rom_key(const char *rom, uint32_t *code, uint32_t *crc) {
    FILE *f = fopen(rom, "rb");
    if (!f) return false;
    uint8_t h[512];
    bool ok = fread(h, 1, sizeof(h), f) == sizeof(h);
    fclose(f);
    if (!ok) return false;
    *code = rd32(h + 12);
    *crc = crc32_buf(h, sizeof(h));
    return true;
}

// Finds the block of a game: file offset and size (0 = not there)
static bool find_block(FILE *f, uint32_t code, uint32_t crc, uint32_t *pos, uint32_t *size) {
    char head[12];
    if (fread(head, 1, 12, f) != 12 || memcmp(head, "R4 CheatCode", 12) != 0) return false;
    if (fseek(f, 0, SEEK_END) != 0) return false;
    long fsize = ftell(f);
    if (fseek(f, 0x100, SEEK_SET) != 0) return false;
    static uint8_t chunk[16 * 256];
    uint8_t prev[16];
    bool have_prev = false;
    for (;;) {
        size_t got = fread(chunk, 16, 256, f);
        if (!got) break;
        for (size_t i = 0; i < got; i++) {
            const uint8_t *e = chunk + i * 16;
            uint32_t off = rd32(e + 8);
            if (have_prev && rd32(prev) == code && rd32(prev + 4) == crc) {
                uint32_t start = rd32(prev + 8);
                uint32_t end = off ? off : (uint32_t)fsize;
                if (end <= start) return false;
                *pos = start; *size = end - start;
                return true;
            }
            if (!off) return false;               // end of the index
            memcpy(prev, e, 16);
            have_prev = true;
        }
    }
    return false;
}

void cheats_close(void) {
    free(s_buf);
    s_buf = NULL;
    s_buf_len = 0;
    s_n = 0;
}

static const char *str_at(uint32_t off, uint32_t *next) {
    // a NUL-terminated string inside the block (NULL when it runs past the end)
    if (off >= s_buf_len) return NULL;
    const char *p = (const char *)s_buf + off;
    const void *z = memchr(p, 0, s_buf_len - off);
    if (!z) return NULL;
    *next = off + (uint32_t)((const char *)z - p) + 1;
    return p;
}

static bool parse_block(void) {
    uint32_t o = 0, n;
    if (!str_at(0, &n)) return false;                        // game title
    o = (n + 3) & ~3u;
    if (o + 4 > s_buf_len) return false;
    uint32_t count = rd32(s_buf + o) & 0x0FFFFFFF;
    o += 9 * 4;
    uint32_t done = 0;
    while (done < count && o + 4 <= s_buf_len && s_n < CHEAT_MAX_ROWS) {
        uint32_t w = rd32(s_buf + o);
        uint32_t in_folder = 1;
        int parent = -1;
        bool one = false;
        if ((w >> 28) & 1) {                                 // folder
            one = (w >> 24) == 0x11;
            in_folder = w & 0x00FFFFFF;
            uint32_t a, b;
            const char *name = str_at(o + 4, &a);
            const char *note = name ? str_at(a, &b) : NULL;
            if (!note) return s_n > 0;
            CheatRow *r = &s_rows[s_n];
            memset(r, 0, sizeof(*r));
            r->name = name; r->note = note; r->folder = 1; r->one = one; r->parent = -1;
            parent = s_n++;
            done++;
            o = (b + 3) & ~3u;
        }
        for (uint32_t k = 0; k < in_folder && o + 4 <= s_buf_len; k++) {
            uint32_t w2 = rd32(s_buf + o);
            uint32_t a, b;
            const char *name = str_at(o + 4, &a);
            const char *note = name ? str_at(a, &b) : NULL;
            if (!note) return s_n > 0;
            uint32_t d = (b + 3) & ~3u;
            if (d + 4 > s_buf_len) return s_n > 0;
            uint32_t ncodes = rd32(s_buf + d);
            if (ncodes && d + 4 + ncodes * 4 <= s_buf_len && s_n < CHEAT_MAX_ROWS) {
                CheatRow *r = &s_rows[s_n];
                memset(r, 0, sizeof(*r));
                r->name = name; r->note = note;
                r->codes = (const uint32_t *)(s_buf + d + 4);
                r->ncodes = (uint16_t)(ncodes > 0xFFFF ? 0xFFFF : ncodes);
                r->flag_pos = s_block_pos + o + 3;
                r->on = (w2 & 0xFF000000u) != 0;
                r->parent = (int16_t)parent;
                r->one = one;
                s_orig[s_n] = r->on;
                s_n++;
            }
            done++;
            uint32_t step = ((w2 & 0x00FFFFFF) + 1) * 4;
            if (!step || o + step <= o) return s_n > 0;
            o += step;
        }
    }
    // "only one" folders: keep the first chosen cheat (the database may say otherwise)
    for (int i = 0; i < s_n; i++) {
        if (!s_rows[i].folder || !s_rows[i].one) continue;
        bool seen = false;
        for (int j = i + 1; j < s_n && s_rows[j].parent == i; j++) {
            if (s_rows[j].on && seen) s_rows[j].on = 0;
            if (s_rows[j].on) seen = true;
        }
    }
    return true;
}

int cheats_open(const char *rom_path) {
    cheats_close();
    if (!cheats_db_path(s_db, sizeof(s_db))) return -1;
    uint32_t code, crc;
    if (!rom_key(rom_path, &code, &crc)) return -2;
    FILE *f = fopen(s_db, "rb");
    if (!f) return -1;
    uint32_t pos = 0, size = 0;
    if (!find_block(f, code, crc, &pos, &size)) { fclose(f); return 0; }
    if (size > MAX_BLOCK) size = MAX_BLOCK;
    s_buf = (uint8_t *)malloc(size + 1);
    if (!s_buf) { fclose(f); return -2; }
    bool ok = fseek(f, (long)pos, SEEK_SET) == 0 && fread(s_buf, 1, size, f) == size;
    fclose(f);
    if (!ok) { cheats_close(); return -2; }
    s_buf[size] = 0;
    s_buf_len = size;
    s_block_pos = pos;
    if (!parse_block()) { cheats_close(); return -2; }
    return s_n;
}

int cheats_count(void) { return s_n; }
const CheatRow *cheats_row(int i) { return (i >= 0 && i < s_n) ? &s_rows[i] : NULL; }
const char *cheats_game_title(void) { return s_buf ? (const char *)s_buf : ""; }

void cheats_toggle(int i) {
    if (i < 0 || i >= s_n || s_rows[i].folder) return;
    CheatRow *r = &s_rows[i];
    bool turn_on = !r->on;
    if (turn_on && r->one && r->parent >= 0) {
        for (int j = r->parent + 1; j < s_n && s_rows[j].parent == r->parent; j++) s_rows[j].on = 0;
    }
    r->on = turn_on;
}

int cheats_chosen(void) {
    int n = 0;
    for (int i = 0; i < s_n; i++) n += !s_rows[i].folder && s_rows[i].on;
    return n;
}

bool cheats_dirty(void) {
    for (int i = 0; i < s_n; i++) if (!s_rows[i].folder && s_rows[i].on != s_orig[i]) return true;
    return false;
}

bool cheats_save(void) {
    if (!s_n || !cheats_dirty()) return true;
    FILE *f = fopen(s_db, "r+b");
    if (!f) return false;
    bool ok = true;
    for (int i = 0; i < s_n; i++) {
        CheatRow *r = &s_rows[i];
        if (r->folder || r->on == s_orig[i] || !r->flag_pos) continue;
        uint8_t v = r->on ? 1 : 0;
        if (fseek(f, (long)r->flag_pos, SEEK_SET) != 0 || fwrite(&v, 1, 1, f) != 1) { ok = false; break; }
        s_orig[i] = r->on;
    }
    fclose(f);
    return ok;
}

int cheats_prepare(const char *rom_path) {
    char out[MAX_PATH_LEN];
    snprintf(out, sizeof(out), "%s/_nds/nds-bootstrap/cheatData.bin", get_drive_prefix());
    int n = cheats_open(rom_path);
    int chosen = 0;
    uint32_t words = 0;
    if (n > 0) {
        for (int i = 0; i < s_n; i++) if (!s_rows[i].folder && s_rows[i].on) { chosen++; words += s_rows[i].ncodes; }
    }
    if (!chosen || (words + 1) * 4 > MAX_CHEAT_FILE) {
        remove(out);
        cheats_close();
        return 0;
    }
    char dir[MAX_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/_nds/nds-bootstrap", get_drive_prefix());
    make_dirs(dir);
    FILE *f = fopen(out, "wb");
    if (!f) { cheats_close(); return 0; }
    bool ok = true;
    for (int i = 0; i < s_n && ok; i++) {
        if (s_rows[i].folder || !s_rows[i].on) continue;
        ok = fwrite(s_rows[i].codes, 4, s_rows[i].ncodes, f) == s_rows[i].ncodes;
    }
    static const uint8_t end[4] = { 0, 0, 0, 0xCF };
    if (ok) ok = fwrite(end, 1, 4, f) == 4;
    fclose(f);
    if (!ok) { remove(out); chosen = 0; }
    cheats_close();
    return chosen;
}

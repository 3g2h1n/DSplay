#include "romdb.h"
#include "gfx.h"
#include "stats.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef struct {
    const char *ext;
    ItemType type;
} ExtMap;

static const ExtMap s_exts[] = {
    { ".nds", TYPE_NDS }, { ".dsi", TYPE_NDS }, { ".srl", TYPE_NDS }, { ".ids", TYPE_NDS },
    { ".gba", TYPE_GBA }, { ".agb", TYPE_GBA },
    { ".gb", TYPE_GB }, { ".sgb", TYPE_GB },
    { ".gbc", TYPE_GBC },
    { ".nes", TYPE_NES }, { ".fds", TYPE_NES },
    { ".sfc", TYPE_SNES }, { ".smc", TYPE_SNES },
    { ".gen", TYPE_GENESIS }, { ".md", TYPE_GENESIS }, { ".bin", TYPE_GENESIS },
    { ".sms", TYPE_SMS },
    { ".gg", TYPE_GG },
};

ItemType romdb_type_from_name(const char *name) {
    if (!name) return TYPE_UNKNOWN;
    const char *dot = strrchr(name, '.');
    if (!dot) return TYPE_UNKNOWN;
    for (size_t i = 0; i < sizeof(s_exts) / sizeof(s_exts[0]); i++) {
        if (strcasecmp(dot, s_exts[i].ext) == 0) return s_exts[i].type;
    }
    return TYPE_UNKNOWN;
}

const char *romdb_system_short(ItemType t) {
    switch (t) {
        case TYPE_NDS:     return "DS";
        case TYPE_GBA:     return "GBA";
        case TYPE_GB:      return "GB";
        case TYPE_GBC:     return "GBC";
        case TYPE_NES:     return "NES";
        case TYPE_SNES:    return "SNES";
        case TYPE_GENESIS: return "MD";
        case TYPE_SMS:     return "SMS";
        case TYPE_GG:      return "GG";
        case TYPE_SLOT1:   return "DS";
        case TYPE_APP:     return "DSi";
        default:           return "";
    }
}

const char *romdb_system_name(ItemType t) {
    switch (t) {
        case TYPE_NDS:     return "Nintendo DS";
        case TYPE_GBA:     return "Game Boy Advance";
        case TYPE_GB:      return "Game Boy";
        case TYPE_GBC:     return "Game Boy Color";
        case TYPE_NES:     return "Nintendo (NES)";
        case TYPE_SNES:    return "Super Nintendo";
        case TYPE_GENESIS: return "Mega Drive / Genesis";
        case TYPE_SMS:     return "Master System";
        case TYPE_GG:      return "Game Gear";
        case TYPE_SLOT1:   return "Nintendo DS";
        case TYPE_APP:     return "Nintendo DSi";
        default:           return "";
    }
}

// Append one Unicode code point as UTF-8
static size_t put_utf8(char *dst, size_t pos, size_t max, uint32_t cp) {
    if (cp < 0x80) {
        if (pos + 1 >= max) return pos;
        dst[pos++] = (char)cp;
    } else if (cp < 0x800) {
        if (pos + 2 >= max) return pos;
        dst[pos++] = (char)(0xC0 | (cp >> 6));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    } else {
        if (pos + 3 >= max) return pos;
        dst[pos++] = (char)(0xE0 | (cp >> 12));
        dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    }
    return pos;
}

static bool keep_codepoint(uint16_t ch) {
    if (ch < 32) return false;
    if (ch == 0x2122 || ch == 0x00AE || ch == 0xE000 || (ch >= 0xE000 && ch <= 0xF8FF)) return false; // TM, (R), private use
    return true;
}

void romdb_banner_to_utf8(const uint16_t *src, char *title, size_t title_len, char *subtitle, size_t sub_len) {
    int len = 0, breaks[8], nb = 0;
    while (len < 128 && src[len]) {
        if (src[len] == '\n' && nb < 8) breaks[nb++] = len;
        len++;
    }
    // Banner = 1..3 lines; when there are 2+ lines the last one is the publisher
    int title_end = (nb >= 1) ? breaks[nb - 1] : len;
    size_t o = 0;
    bool space = true;
    for (int i = 0; i < title_end; i++) {
        uint16_t ch = src[i];
        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == 0x3000) {
            if (!space) { o = put_utf8(title, o, title_len, ' '); space = true; }
            continue;
        }
        if (!keep_codepoint(ch)) continue;
        o = put_utf8(title, o, title_len, ch);
        space = false;
    }
    while (o > 0 && title[o - 1] == ' ') o--;
    title[o] = '\0';

    if (subtitle && sub_len) {
        o = 0;
        if (nb >= 1) {
            for (int i = breaks[nb - 1] + 1; i < len; i++) {
                if (!keep_codepoint(src[i])) continue;
                o = put_utf8(subtitle, o, sub_len, src[i]);
            }
        }
        subtitle[o] = '\0';
    }
}

void romdb_title_from_filename(const char *filename, char *dst, size_t max_len) {
    if (!dst || !max_len) return;
    char tmp[MAX_DISPLAY_TITLE];
    strncpy(tmp, filename ? filename : "", sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *dot = strrchr(tmp, '.');
    if (dot && dot != tmp) *dot = '\0';
    // strip trailing region / dump tags: "(USA)", "[!]", "(Rev 1)"
    for (;;) {
        size_t l = strlen(tmp);
        while (l > 0 && tmp[l - 1] == ' ') tmp[--l] = '\0';
        if (l < 4) break;
        char close = tmp[l - 1];
        char open = (close == ')') ? '(' : (close == ']') ? '[' : 0;
        if (!open) break;
        char *o = strrchr(tmp, open);
        if (!o || o == tmp) break;
        *o = '\0';
    }
    // leading numbering like "0123 - " from no-intro sets
    const char *s = tmp;
    if (s[0] >= '0' && s[0] <= '9' && s[1] >= '0' && s[1] <= '9' && s[2] >= '0' && s[2] <= '9' && s[3] >= '0' && s[3] <= '9' &&
        s[4] == ' ' && s[5] == '-' && s[6] == ' ') {
        s += 7;
    }
    size_t o = 0;
    bool space = true;
    for (; *s && o + 1 < max_len; s++) {
        char c = *s;
        if (c == '_') c = ' ';
        if (c == ' ') {
            if (!space) { dst[o++] = ' '; space = true; }
            continue;
        }
        if ((unsigned char)c < 32) continue;
        dst[o++] = c;
        space = false;
    }
    while (o > 0 && dst[o - 1] == ' ') o--;
    // do not cut a UTF-8 sequence in half
    while (o > 0 && ((unsigned char)dst[o - 1] & 0xC0) == 0x80 && o + 1 >= max_len) o--;
    dst[o] = '\0';
    if (!o) {
        strncpy(dst, filename ? filename : "?", max_len - 1);
        dst[max_len - 1] = '\0';
    }
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool romdb_nds_is_homebrew(const uint8_t *header) {
    // The caller decides with the ARM9 entry signature (see romdb_fill_item); this is the
    // quick header-only fallback.
    if (memcmp(&header[0x0C], "####", 4) == 0 || header[0x0C] == 0) return true;
    return rd32(&header[0x20]) < 0x4000;
}

// Full game name from _hyper/titles.txt ("CODE<TAB>Title" lines, written by DSplay Studio from
// the GameTDB list): the exact code first, else the same game from another region
bool romdb_lookup_title(const char *code, char *out, size_t len) {
    if (!code || strlen(code) < 3) return false;
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/_hyper/titles.txt", stats_drive());
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[160], best3[MAX_TITLE_LEN] = "";
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strlen(line) < 6 || line[4] != '\t') continue;
        if (strncasecmp(line, code, 3) != 0) continue;
        char *t = line + 5;
        t[strcspn(t, "\r\n")] = '\0';
        if (!t[0] || !gfx_text_renderable(t)) continue;
        if (strlen(code) >= 4 && strncasecmp(line, code, 4) == 0) { snprintf(out, len, "%s", t); found = true; break; }
        if (!best3[0]) snprintf(best3, sizeof(best3), "%s", t);
    }
    fclose(f);
    if (!found && best3[0]) { snprintf(out, len, "%s", best3); found = true; }
    return found;
}

bool romdb_parse_nds_buffers(GameItem *item, const uint8_t *header, const uint8_t *banner, size_t banner_len) {
    char code[5] = { 0 };
    memcpy(code, &header[0x0C], 4);
    for (int i = 0; i < 4; i++) if ((unsigned char)code[i] < 32 || (unsigned char)code[i] > 126) code[i] = 0;
    snprintf(item->game_code, sizeof(item->game_code), "%s", code);
    if (header[0x12] & 0x02) item->flags |= ITEM_FLAG_DSI;

    bool have_title = false;
    if (banner && banner_len >= 0x240) {
        bool icon_present = false;
        memcpy(item->icon_data, banner + 0x20, ICON_TILE_BYTES);
        memcpy(item->icon_palette, banner + 0x220, sizeof(item->icon_palette));
        for (int i = 0; i < ICON_TILE_BYTES; i++) if (item->icon_data[i]) { icon_present = true; break; }
        if (!icon_present) memset(item->icon_data, 0, ICON_TILE_BYTES);
        if (banner_len >= 0x340 + 0x100 * 2) {
            // English first, then the other languages; a title the font cannot draw (Japanese,
            // Chinese, Korean) is skipped so it never shows up as "????"
            static const int order[] = { 1, 2, 3, 4, 5, 0, 6, 7 };
            int langs = banner_len >= 0x240 + 0x100 * 8 ? 8 : (banner_len >= 0x240 + 0x100 * 7 ? 7 : 6);
            uint16_t utf16[128];
            for (int k = 0; k < 8 && !have_title; k++) {
                int lang = order[k];
                if (lang >= langs) continue;
                memcpy(utf16, banner + 0x240 + lang * 0x100, sizeof(utf16));
                romdb_banner_to_utf8(utf16, item->title, sizeof(item->title), item->subtitle, sizeof(item->subtitle));
                have_title = strlen(item->title) >= 2 && gfx_text_renderable(item->title);
            }
            if (!have_title) item->title[0] = '\0';
            if (!gfx_text_renderable(item->subtitle)) item->subtitle[0] = '\0';
        }
    }
    if (!have_title && romdb_lookup_title(item->game_code, item->title, sizeof(item->title))) have_title = true;
    if (!have_title && item->filename[0]) {
        romdb_title_from_filename(item->filename, item->title, sizeof(item->title));
        have_title = item->title[0] && gfx_text_renderable(item->title);
    }
    if (!have_title) {
        char rom_title[13] = { 0 };
        memcpy(rom_title, header, 12);
        for (int i = 11; i >= 0 && (rom_title[i] == ' ' || rom_title[i] == 0); i--) rom_title[i] = 0;
        snprintf(item->title, sizeof(item->title), "%s", rom_title[0] ? rom_title : item->game_code);
    }
    return true;
}

static bool sdk_signature(FILE *f, const uint8_t *header) {
    uint32_t rom_off = rd32(&header[0x20]);
    uint32_t entry = rd32(&header[0x24]);
    uint32_t dest = rd32(&header[0x28]);
    if (entry < dest) return false;
    uint32_t off = rom_off + ((memcmp(&header[0x0C], "BIG", 3) == 0) ? 0x02000800 : entry) - dest;
    uint32_t sig[4];
    if (fseek(f, (long)off, SEEK_SET) != 0 || fread(sig, sizeof(u32), 4, f) != 4) return false;
    if ((sig[0] == 0xE3A0C301 || (sig[0] >= 0xEA000000 && sig[0] < 0xEC000000)) && sig[1] == 0xE58CC208) {
        if (sig[2] >= 0xEB000000 && sig[2] < 0xEC000000 && sig[3] >= 0xE3A00000 && sig[3] < 0xE3A01000) return true;
        if (sig[2] == 0xE1DC00B6 && sig[3] == 0xE3500000) return true;
        if (sig[2] == 0xEAFFFFFF && sig[3] == 0xE1DC00B6) return true;
    }
    if (memcmp(&header[0x0C], "HNA", 3) == 0) return true;
    return false;
}

bool romdb_fill_item(GameItem *item, const char *path, const char *filename, uint32_t size, uint32_t mtime) {
    memset(item, 0, sizeof(*item));
    snprintf(item->path, sizeof(item->path), "%s", path);
    snprintf(item->filename, sizeof(item->filename), "%s", filename);
    item->type = (uint32_t)romdb_type_from_name(filename);
    item->file_size = size;
    item->mtime = mtime;

    if (item->type != TYPE_NDS) {
        romdb_title_from_filename(filename, item->title, sizeof(item->title));
        return item->type != TYPE_UNKNOWN;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        romdb_title_from_filename(filename, item->title, sizeof(item->title));
        return true;
    }
    uint8_t header[0x200];
    static uint8_t banner[0x840];
    size_t banner_len = 0;
    if (fread(header, 1, sizeof(header), f) == sizeof(header)) {
        uint32_t boff = rd32(&header[0x68]);
        if (boff >= 0x200 && (size == 0 || boff < size) && fseek(f, (long)boff, SEEK_SET) == 0) {
            banner_len = fread(banner, 1, sizeof(banner), f);
        }
        romdb_parse_nds_buffers(item, header, banner_len ? banner : NULL, banner_len);
        if (!sdk_signature(f, header)) item->flags |= ITEM_FLAG_HOMEBREW;
    } else {
        romdb_title_from_filename(filename, item->title, sizeof(item->title));
    }
    fclose(f);
    return true;
}


// --------------------------------------------------------------------------
// Animated DSi icons
// --------------------------------------------------------------------------
bool romdb_load_anim(const char *path, NdsAnim *a) {
    memset(a, 0, sizeof(*a));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[0x6C];
    bool ok = fread(h, 1, sizeof(h), f) == sizeof(h);
    uint32_t boff = ok ? rd32(&h[0x68]) : 0;
    uint16_t ver = 0;
    if (ok && boff >= 0x200 && fseek(f, (long)boff, SEEK_SET) == 0 && fread(&ver, 2, 1, f) == 1 && ver >= 0x0103) {
        ok = fseek(f, (long)(boff + 0x1240), SEEK_SET) == 0 &&
             fread(a->bmp, 1, sizeof(a->bmp), f) == sizeof(a->bmp) &&
             fread(a->pal, 1, sizeof(a->pal), f) == sizeof(a->pal) &&
             fread(a->seq, 1, sizeof(a->seq), f) == sizeof(a->seq);
    } else {
        ok = false;
    }
    fclose(f);
    if (!ok) return false;
    for (int i = 0; i < 64 && (a->seq[i] & 0xFF); i++) { a->steps++; a->total += a->seq[i] & 0xFF; }
    return a->steps > 1 && a->total > 0;    // one step only = not really animated
}

int romdb_anim_step(const NdsAnim *a, uint32_t frame) {
    if (!a->steps || !a->total) return 0;
    uint32_t t = frame % a->total;
    for (int i = 0; i < a->steps; i++) {
        uint32_t d = a->seq[i] & 0xFF;
        if (t < d) return i;
        t -= d;
    }
    return a->steps - 1;
}

void romdb_anim_icon(const NdsAnim *a, int step, uint8_t *icon_out, uint16_t *pal_out) {
    uint16_t e = a->seq[step < 0 ? 0 : step % 64];
    int bi = (e >> 8) & 7, pi = (e >> 11) & 7;
    bool hf = (e >> 14) & 1, vf = (e >> 15) & 1;
    memcpy(pal_out, a->pal[pi], 32);
    const uint8_t *src = a->bmp[bi];
    memset(icon_out, 0, 512);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int sx = hf ? 31 - x : x, sy = vf ? 31 - y : y;
            uint8_t v = src[((sy >> 3) * 4 + (sx >> 3)) * 32 + (sy & 7) * 4 + ((sx & 7) >> 1)];
            int idx = (sx & 1) ? (v >> 4) : (v & 0xF);
            uint8_t *d = &icon_out[((y >> 3) * 4 + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1)];
            *d |= (x & 1) ? (uint8_t)(idx << 4) : (uint8_t)idx;
        }
}

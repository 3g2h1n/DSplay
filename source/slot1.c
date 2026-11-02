#include "slot1.h"
#include "romdb.h"
#include "stats.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#ifdef ARM9
#include <nds.h>
#include <calico/nds/scfg.h>
#include <calico/nds/ntrcard.h>
#endif

static GameItem s_card;
static bool     s_present = false;
static bool     s_enabled = false;
static bool     s_info_ok = false;

static uint16_t crc16_modbus(const uint8_t *d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}

bool slot1_header_valid(const uint8_t *hdr) {
    uint16_t stored = (uint16_t)(hdr[0x15E] | (hdr[0x15F] << 8));
    return stored != 0 && stored != 0xFFFF && crc16_modbus(hdr, 0x15E) == stored;
}

static const struct { const char *code; const char *name; } k_known[] = {
    { "AMC", "Mario Kart DS" }, { "ASM", "Super Mario 64 DS" }, { "A2D", "New Super Mario Bros." },
    { "CPU", "Pokémon Platinum" }, { "ADA", "Pokémon Diamond" }, { "APA", "Pokémon Pearl" },
    { "IPK", "Pokémon HeartGold" }, { "IPG", "Pokémon SoulSilver" }, { "IRB", "Pokémon Black" },
    { "IRA", "Pokémon White" }, { "IRE", "Pokémon Black 2" }, { "IRD", "Pokémon White 2" },
    { "AZE", "Zelda: Phantom Hourglass" }, { "BKI", "Zelda: Spirit Tracks" }, { "ADM", "Animal Crossing: Wild World" },
    { "YQU", "Chrono Trigger" }, { "YKW", "Kirby Super Star Ultra" }, { "ANT", "Nintendogs" },
    { "AYW", "Yoshi's Island DS" }, { "CLJ", "Mario & Luigi: Bowser's Inside Story" }, { "AKW", "Kirby: Squeak Squad" },
    { "A5F", "Professor Layton" }, { "ATR", "Tetris DS" }, { "AMP", "Mario Party DS" },
};

void slot1_fill_from_header(GameItem *it, const uint8_t *hdr) {
    memset(it, 0, sizeof(*it));
    it->type = TYPE_SLOT1;
    snprintf(it->path, sizeof(it->path), "slot1:/");
    snprintf(it->filename, sizeof(it->filename), "SLOT1:%.4s", (const char *)&hdr[0x0C]);
    romdb_parse_nds_buffers(it, hdr, NULL, 0);
    it->type = TYPE_SLOT1;
    for (size_t k = 0; k < sizeof(k_known) / sizeof(k_known[0]); k++) {
        if (strncasecmp(it->game_code, k_known[k].code, 3) == 0) {
            snprintf(it->title, sizeof(it->title), "%s", k_known[k].name);
            return;
        }
    }
    // Header title is 12 upper-case chars ("MARIOKARTDS") - make it a bit friendlier
    char t[13] = { 0 };
    memcpy(t, hdr, 12);
    for (int i = 11; i >= 0 && (t[i] == ' ' || !t[i]); i--) t[i] = 0;
    for (int i = 0; t[i]; i++) if (t[i] == '_') t[i] = ' ';
    if (t[0]) snprintf(it->title, sizeof(it->title), "%s", t);
}

// Full game name from _hyper/titles.txt ("CODE<TAB>Title" lines, written by DSplay
// Studio from the GameTDB list). The card header only holds a 12-letter name ("MARIOPARTYDS").
static void lookup_title(GameItem *it) {
    char t[MAX_TITLE_LEN];
    if (romdb_lookup_title(it->game_code, t, sizeof(t))) snprintf(it->title, sizeof(it->title), "%s", t);
}

// Remembers the codes of cards that were inserted (_hyper/slot1_seen.txt), so DSplay
// Studio can fetch their box art next time
static void note_seen(const GameItem *it) {
    if (!it->game_code[0]) return;
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/_hyper/slot1_seen.txt", stats_drive());
    FILE *f = fopen(path, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, it->game_code, 4) == 0) { fclose(f); return; }
        }
        fclose(f);
    }
    f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%.4s\t%s\n", it->game_code, it->title);
    fclose(f);
}

static bool s_first = true;     // first detection: the DSi Menu already powered and reset the card
static int  s_pending = 0;      // frames until a freshly inserted card is read
static int  s_retries = 0;

void slot1_init(bool enable) {
    s_enabled = enable;
#ifdef ARM9
    if (!scfgIsPresent()) s_enabled = false;
#else
    s_enabled = false;
#endif
    s_present = false;
    s_info_ok = false;
    s_first = true;
    s_pending = 0;
}

static bool card_inserted(void) {
#ifdef ARM9
    return (REG_SCFG_MC & SCFG_MC_IS_EJECTED) == 0;
#else
    return false;
#endif
}

static void set_placeholder(void) {
    memset(&s_card, 0, sizeof(s_card));
    s_card.type = TYPE_SLOT1;
    snprintf(s_card.path, sizeof(s_card.path), "slot1:/");
    snprintf(s_card.title, sizeof(s_card.title), "Nintendo DS");
}

// Reads the card header. After a card swap the slot has to be powered up again and
// the new card taken through the reset / init sequence; the state calico remembers
// belongs to the previous card, so it is always cleared first.
static void read_info(bool reset_card) {
    s_info_ok = false;
#ifdef ARM9
    static uint8_t hdr[0x200] __attribute__((aligned(32)));
    memset(hdr, 0, sizeof(hdr));
    if (reset_card) {
        scfgSetMcPower(false);
        scfgSetMcPower(true);
    }
    if (ntrcardOpen()) {
        NtrCardMode mode = ntrcardGetMode();
        bool ready = !reset_card && (mode == NtrCardMode_Main || mode == NtrCardMode_Init);
        if (!ready) {
            ntrcardClearState();
            ready = ntrcardStartup(-1);
        }
        if (ready && ntrcardRomRead(-1, 0, hdr, sizeof(hdr)) && slot1_header_valid(hdr)) {
            slot1_fill_from_header(&s_card, hdr);
            s_info_ok = true;
        }
        ntrcardClose();
    }
#else
    (void)reset_card;
#endif
    if (!s_info_ok) { set_placeholder(); return; }
    lookup_title(&s_card);
    note_seen(&s_card);
}

static void forget_card(void) {
#ifdef ARM9
    if (ntrcardOpen()) {
        ntrcardClearState();
        ntrcardClose();
    }
#endif
    set_placeholder();
    s_info_ok = false;
}

bool slot1_poll(void) {
    if (!s_enabled) return false;
    bool now = card_inserted();
    if (now != s_present) {
        s_present = now;
        s_retries = 0;
        if (!now) {
            s_pending = 0;
            forget_card();
        } else if (s_first) {
            read_info(false);
            if (!s_info_ok) s_pending = 20;          // not ready yet: retry with a reset
        } else {
            set_placeholder();
            s_pending = 30;                          // let the contacts settle (~0.5 s), then read
        }
        s_first = false;
        return true;
    }
    if (s_present && s_pending > 0 && --s_pending == 0) {
        read_info(true);
        if (!s_info_ok && ++s_retries < 4) s_pending = 30;
        return s_info_ok;                            // the HOME menu only redraws when the name is known
    }
    return false;
}

bool slot1_present(void) { return s_enabled && s_present; }

const GameItem *slot1_item(void) { return slot1_present() ? &s_card : NULL; }

// ---------------------------------------------------------------------------
// Save chip of the inserted card (read only), over the card's SPI bus
// ---------------------------------------------------------------------------
#ifdef ARM9
static void spi_wait(void) { while (REG_NTRCARD_CNT & NTRCARD_CNT_SPI_BUSY) {} }

static uint8_t spi_xfer(uint8_t b) {
    REG_NTRCARD_SPIDATA = b;
    spi_wait();
    return (uint8_t)REG_NTRCARD_SPIDATA;
}

static void spi_begin(void) {
    REG_NTRCARD_CNT = NTRCARD_CNT_ENABLE | NTRCARD_CNT_MODE_SPI | NTRCARD_CNT_SPI_HOLD | NTRCARD_CNT_SPI_BAUD(0);
}

static void spi_end(void) {
    // the last byte is sent with HOLD cleared (chip select released)
    REG_NTRCARD_CNT = NTRCARD_CNT_ENABLE | NTRCARD_CNT_MODE_SPI | NTRCARD_CNT_SPI_BAUD(0);
    REG_NTRCARD_SPIDATA = 0;
    spi_wait();
    REG_NTRCARD_CNT = 0;
}

static uint8_t spi_cmd1(uint8_t cmd) {        // command with one reply byte
    spi_begin();
    spi_xfer(cmd);
    uint8_t v = spi_xfer(0);
    spi_end();
    return v;
}

static uint32_t spi_jedec(void) {
    spi_begin();
    spi_xfer(0x9F);
    uint32_t id = (uint32_t)spi_xfer(0) << 16;
    id |= (uint32_t)spi_xfer(0) << 8;
    id |= spi_xfer(0);
    spi_end();
    return id;
}

// addr_bytes: 1 (512 B EEPROM, address bit 8 in the command), 2 or 3
static void spi_read(uint32_t addr, uint8_t *dst, uint32_t len, int addr_bytes) {
    spi_begin();
    if (addr_bytes == 1) {
        spi_xfer((uint8_t)(0x03 | ((addr >> 8) & 1) << 3));
        spi_xfer((uint8_t)addr);
    } else {
        spi_xfer(0x03);
        if (addr_bytes == 3) spi_xfer((uint8_t)(addr >> 16));
        spi_xfer((uint8_t)(addr >> 8));
        spi_xfer((uint8_t)addr);
    }
    for (uint32_t i = 0; i < len; i++) dst[i] = spi_xfer(0);
    spi_end();
}
#endif

// Reads the whole save chip. Returns the size (0 = no save chip / unsupported) and a malloc'd buffer.
uint32_t slot1_read_save(uint8_t **out, uint32_t max_size) {
    *out = NULL;
#ifdef ARM9
    if (!slot1_present() || !s_info_ok) return 0;
    // Pokemon cards with an infrared chip need a different protocol: not supported
    if (s_card.game_code[0] == 'I') return 0;
    if (!ntrcardOpen()) return 0;
    uint8_t sr = spi_cmd1(0x05);
    uint32_t id = spi_jedec();
    uint32_t size = 0;
    int ab = 2;
    if ((sr == 0xFF && id == 0xFFFFFF) || (sr == 0 && id == 0)) {
        size = 0;                                       // nothing answers
    } else if (id != 0xFFFFFF && (id >> 16) != 0 && (id & 0xFF) >= 0x10 && (id & 0xFF) <= 0x17) {
        size = 1u << (id & 0xFF);                       // FLASH: size from its ID (256 KB .. 8 MB)
        ab = 3;
    } else if (sr == 0xF0) {
        size = 512;                                     // tiny EEPROM
        ab = 1;
    } else {
        size = 64 * 1024;                               // EEPROM 8 / 64 KB (mirrors checked below)
        ab = 2;
    }
    if (!size || size > max_size) { ntrcardClose(); return 0; }
    uint8_t *buf = malloc(size);
    if (!buf) { ntrcardClose(); return 0; }
    for (uint32_t off = 0; off < size; off += 4096) {
        uint32_t n = size - off < 4096 ? size - off : 4096;
        spi_read(off, buf + off, n, ab);
    }
    ntrcardClose();
    if (ab == 2) {
        // an 8 KB EEPROM shows its contents again every 8 KB
        bool mirror = true;
        for (uint32_t off = 8192; off < size && mirror; off += 8192) mirror = memcmp(buf, buf + off, 8192) == 0;
        if (mirror) size = 8192;
    }
    *out = buf;
    return size;
#else
    (void)max_size;
    return 0;
#endif
}

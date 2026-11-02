// Homebrew store catalog (see include/store.h)
#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

_Static_assert(sizeof(StoreEntry) == 2444, "StoreEntry must match studio/store.py");

#define STORE_MAX 255
#define MAX_INSTALLED 128

static StoreEntry *s_items;
static int s_n;
static char s_inst[MAX_INSTALLED][40];
static char s_inst_file[MAX_INSTALLED][48];   // first installed file (to notice it was deleted)
static int s_inst_n;

void store_free(void) {
    free(s_items);
    s_items = NULL;
    s_n = 0;
}

bool store_parse(const uint8_t *d, int len) {
    store_free();
    if (len < 16 || memcmp(d, "HSTR", 4) != 0) return false;
    uint32_t ver, n, esz;
    memcpy(&ver, d + 4, 4);
    memcpy(&n, d + 8, 4);
    memcpy(&esz, d + 12, 4);
    if (ver != 1 || esz != sizeof(StoreEntry) || n > STORE_MAX || (uint64_t)16 + (uint64_t)n * esz > (uint64_t)len) return false;
    if (!n) return true;
    s_items = (StoreEntry *)malloc(sizeof(StoreEntry) * n);
    if (!s_items) return false;
    memcpy(s_items, d + 16, sizeof(StoreEntry) * n);
    for (uint32_t i = 0; i < n; i++) {             // the strings come from the computer: always terminated
        StoreEntry *e = &s_items[i];
        e->slug[39] = e->title[47] = e->author[31] = e->version[15] = e->desc[199] = e->file[47] = '\0';
    }
    s_n = (int)n;
    return true;
}

void store_parse_installed(const char *text) {
    s_inst_n = 0;
    const char *p = text;
    while (p && *p && s_inst_n < MAX_INSTALLED) {
        const char *bar = strchr(p, '|');
        const char *nl = strchr(p, '\n');
        if (bar && (!nl || bar < nl) && bar > p) {
            size_t k = (size_t)(bar - p);
            if (k > 39) k = 39;
            memcpy(s_inst[s_inst_n], p, k);
            s_inst[s_inst_n][k] = '\0';
            // "slug|version|file1,file2"
            s_inst_file[s_inst_n][0] = '\0';
            const char *b2 = strchr(bar + 1, '|');
            if (b2 && (!nl || b2 < nl)) {
                const char *e = b2 + 1;
                size_t m = 0;
                while (e[m] && e[m] != ',' && e[m] != '\n' && e[m] != '\r' && m < 47) m++;
                memcpy(s_inst_file[s_inst_n], e, m);
                s_inst_file[s_inst_n][m] = '\0';
            }
            s_inst_n++;
        }
        p = nl ? nl + 1 : NULL;
    }
}

bool store_load(const char *drive) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/_hyper/store/installed.txt", drive);
    FILE *f = fopen(path, "rb");
    static char txt[8192];
    size_t tn = 0;
    if (f) { tn = fread(txt, 1, sizeof(txt) - 1, f); fclose(f); }
    txt[tn] = '\0';
    store_parse_installed(txt);
    // programs deleted from the card (e.g. with "Delete game") are not installed any more
    int w = 0;
    for (int i = 0; i < s_inst_n; i++) {
        bool keep = true;
        if (s_inst_file[i][0]) {
            snprintf(path, sizeof(path), "%s/roms/NDS/Homebrew/%s", drive, s_inst_file[i]);
            FILE *t = fopen(path, "rb");
            keep = t != NULL;
            if (t) fclose(t);
        }
        if (keep) { if (w != i) { memcpy(s_inst[w], s_inst[i], 40); memcpy(s_inst_file[w], s_inst_file[i], 48); } w++; }
    }
    s_inst_n = w;

    snprintf(path, sizeof(path), "%s/_hyper/store/catalog.bin", drive);
    f = fopen(path, "rb");
    if (!f) { store_free(); return false; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = false;
    if (len > 16 && len < 16 + (long)sizeof(StoreEntry) * STORE_MAX + 1) {
        uint8_t *buf = (uint8_t *)malloc((size_t)len);
        if (buf && fread(buf, 1, (size_t)len, f) == (size_t)len) ok = store_parse(buf, (int)len);
        free(buf);
    }
    fclose(f);
    return ok;
}

int store_count(void) { return s_n; }
const StoreEntry *store_get(int i) { return (i >= 0 && i < s_n) ? &s_items[i] : NULL; }

bool store_installed(const char *slug) {
    for (int i = 0; i < s_inst_n; i++) if (!strcmp(s_inst[i], slug)) return true;
    return false;
}

int store_view(int category, int *out, int max) {
    int n = 0;
    for (int i = 0; i < s_n && n < max; i++) {
        if (category == STORE_CAT_ALL || s_items[i].category == category) out[n++] = i;
    }
    return n;
}

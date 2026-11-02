// Game folders: see include/collections.h
#include "collections.h"
#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    uint32_t mask;
    char    *path;       // without drive prefix
} CollEntry;

static char      s_names[MAX_COLLECTIONS][COLL_NAME_LEN];
static uint8_t   s_colors[MAX_COLLECTIONS];
static int       s_n;
static CollEntry *s_e;
static int       s_en, s_ecap;
static uint32_t  s_ver;

uint32_t coll_version(void) { return s_ver; }

static const char *strip(const char *path) { return path_without_drive(path); }

static int find(const char *p) {
    for (int i = 0; i < s_en; i++) if (!strcasecmp(s_e[i].path, p)) return i;
    return -1;
}

static void clear(void) {
    for (int i = 0; i < s_en; i++) free(s_e[i].path);
    s_en = 0;
    s_n = 0;
}

static int add_entry(const char *p) {
    int k = find(p);
    if (k >= 0) return k;
    if (s_en == s_ecap) {
        int cap = s_ecap ? s_ecap * 2 : 64;
        CollEntry *n = realloc(s_e, sizeof(CollEntry) * (size_t)cap);
        if (!n) return -1;
        s_e = n;
        s_ecap = cap;
    }
    size_t len = strlen(p);
    char *copy = malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, p, len + 1);
    s_e[s_en].path = copy;
    s_e[s_en].mask = 0;
    return s_en++;
}

void coll_load(void) {
    s_ver++;
    clear();
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "folders.txt");
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_PATH_LEN + 8];
    int cur = -1;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        if (line[0] == '#') {
            cur = -1;
            if (s_n >= MAX_COLLECTIONS) continue;
            char *bar = strchr(line, '|');
            int color = atoi(line + 1);
            snprintf(s_names[s_n], COLL_NAME_LEN, "%s", bar ? bar + 1 : line + 1);
            s_colors[s_n] = (uint8_t)((color >= 0 && color < 8) ? color : 0);
            cur = s_n++;
        } else if (cur >= 0) {
            int k = add_entry(line);
            if (k >= 0) s_e[k].mask |= 1u << cur;
        }
    }
    fclose(f);
}

bool coll_save(void) {
    char path[MAX_PATH_LEN];
    stats_path(path, sizeof(path), "folders.txt");
    FILE *f = fopen(path, "w");
    if (!f) return false;
    for (int c = 0; c < s_n; c++) {
        fprintf(f, "#%d|%s\n", s_colors[c], s_names[c]);
        for (int i = 0; i < s_en; i++) if (s_e[i].mask & (1u << c)) fprintf(f, "%s\n", s_e[i].path);
    }
    fclose(f);
    return true;
}

int coll_count(void) { return s_n; }
const char *coll_name(int i) { return (i >= 0 && i < s_n) ? s_names[i] : ""; }
int coll_color(int i) { return (i >= 0 && i < s_n) ? s_colors[i] : 0; }

int coll_create(const char *name, int color) {
    s_ver++;
    if (s_n >= MAX_COLLECTIONS) return -1;
    snprintf(s_names[s_n], COLL_NAME_LEN, "%s", (name && name[0]) ? name : "?");
    s_colors[s_n] = (uint8_t)(color & 7);
    for (int i = 0; i < s_en; i++) s_e[i].mask &= ~(1u << s_n);
    return s_n++;
}

void coll_delete(int c) {
    s_ver++;
    if (c < 0 || c >= s_n) return;
    for (int k = c; k < s_n - 1; k++) {
        memcpy(s_names[k], s_names[k + 1], COLL_NAME_LEN);
        s_colors[k] = s_colors[k + 1];
    }
    for (int i = 0; i < s_en; i++) {
        uint32_t m = s_e[i].mask;
        uint32_t low = m & ((1u << c) - 1u);
        uint32_t high = (m >> (c + 1)) << c;
        s_e[i].mask = low | high;
    }
    s_n--;
}

void coll_rename(int i, const char *name) {
    s_ver++;
    if (i >= 0 && i < s_n && name && name[0]) snprintf(s_names[i], COLL_NAME_LEN, "%s", name);
}

void coll_set_color(int i, int color) {
    s_ver++;
    if (i >= 0 && i < s_n) s_colors[i] = (uint8_t)(color & 7);
}

uint32_t coll_mask(const char *path) {
    int k = find(strip(path));
    return k >= 0 ? s_e[k].mask : 0;
}

bool coll_has(int i, const char *path) { return (coll_mask(path) >> i) & 1u; }

void coll_set(int i, const char *path, bool on) {
    s_ver++;
    if (i < 0 || i >= s_n || !path) return;
    const char *p = strip(path);
    int k = on ? add_entry(p) : find(p);
    if (k < 0) return;
    if (on) s_e[k].mask |= 1u << i;
    else s_e[k].mask &= ~(1u << i);
}

int coll_size(int i) {
    int n = 0;
    for (int k = 0; k < s_en; k++) if (s_e[k].mask & (1u << i)) n++;
    return n;
}

void coll_forget(const char *path) {
    s_ver++;
    int k = find(strip(path));
    if (k >= 0) s_e[k].mask = 0;
}

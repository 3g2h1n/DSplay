// "Nearby" (see include/nearby.h)
#include "nearby.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <unistd.h>
#ifdef ARM9
#include <nds.h>
#include <dswifi9.h>
#else
#include <arpa/inet.h>
#define closesocket close
#endif

#define LIST_MAGIC 0x31424E48u   // "HNB1"
#define AGAIN_SECS (30 * 60)

static NearbyCard   s_me;
static NearbyPerson s_list[NEARBY_MAX];
static int          s_n;
static int          s_heard[NEARBY_MAX];   // frame of the last packet
static char         s_path[MAX_PATH_LEN];
static int          s_sock = -1;
static int          s_frame;
static bool         s_dirty;
static char         s_met[24];
static char         s_ph_dir[MAX_PATH_LEN];
static const uint16_t *s_my_ph;
static uint32_t     s_my_ph_stamp;
static bool         s_ph_new;
// photos arriving (one per sender at a time)
#define PH_SLOTS 3
typedef struct { uint32_t id, stamp, mask; int age; uint16_t px[NEARBY_PH_W * NEARBY_PH_H]; } PhIn;
static PhIn *s_in;           // only while the screen is open (72 KB)
static struct { uint32_t id, stamp; } s_have[NEARBY_MAX];   // saved this session
static int          s_have_n;

static void wr(uint8_t **p, const void *src, size_t n) { memcpy(*p, src, n); *p += n; }
static void rd(const uint8_t **p, void *dst, size_t n) { memcpy(dst, *p, n); *p += n; }

int nearby_encode(const NearbyCard *c, uint8_t *out, int max) {
    int need = 5 + 4 + 20 + 1 + 1 + 48 + 48 + 4 + 2 + (c->has_avatar ? NEARBY_AV * NEARBY_AV * 2 : 0);
    if (max < need) return 0;
    uint8_t *p = out;
    wr(&p, "HNBR", 4);
    *p++ = 1;
    wr(&p, &c->id, 4);
    wr(&p, c->name, 20);
    *p++ = c->color;
    *p++ = c->has_avatar ? 1 : 0;
    wr(&p, c->greet, 48);
    wr(&p, c->game, 48);
    wr(&p, &c->minutes, 4);
    wr(&p, &c->games, 2);
    if (c->has_avatar) wr(&p, c->avatar, NEARBY_AV * NEARBY_AV * 2);
    return (int)(p - out);
}

bool nearby_decode(const uint8_t *d, int len, NearbyCard *c) {
    if (len < 5 + 128 || memcmp(d, "HNBR", 4) != 0 || d[4] != 1) return false;
    memset(c, 0, sizeof(*c));
    const uint8_t *p = d + 5;
    rd(&p, &c->id, 4);
    rd(&p, c->name, 20);
    c->color = *p++;
    c->has_avatar = *p++;
    rd(&p, c->greet, 48);
    rd(&p, c->game, 48);
    rd(&p, &c->minutes, 4);
    rd(&p, &c->games, 2);
    c->name[19] = c->greet[47] = c->game[47] = '\0';      // from the network: always terminated
    if (c->color > 7) c->color = 0;
    if (c->has_avatar) {
        if (len < (int)(p - d) + NEARBY_AV * NEARBY_AV * 2) c->has_avatar = 0;
        else rd(&p, c->avatar, NEARBY_AV * NEARBY_AV * 2);
    }
    return c->id != 0 && c->name[0] != '\0';
}

bool nearby_merge(const NearbyCard *c, uint32_t now) {
    if (c->id == s_me.id) return false;                   // our own broadcast
    int i = 0;
    while (i < s_n && s_list[i].c.id != c->id) i++;
    bool meeting;
    NearbyPerson p;
    if (i < s_n) {
        p = s_list[i];
        meeting = now - p.last_met >= AGAIN_SECS;
        if (meeting) p.times++;
        memmove(&s_list[1], &s_list[0], sizeof(NearbyPerson) * (size_t)i);
        memmove(&s_heard[1], &s_heard[0], sizeof(int) * (size_t)i);
    } else {
        memset(&p, 0, sizeof(p));
        p.first_met = now;
        p.times = 1;
        meeting = true;
        int keep = s_n < NEARBY_MAX ? s_n : NEARBY_MAX - 1;
        memmove(&s_list[1], &s_list[0], sizeof(NearbyPerson) * (size_t)keep);
        memmove(&s_heard[1], &s_heard[0], sizeof(int) * (size_t)keep);
        s_n = keep + 1;
    }
    p.c = *c;
    p.last_met = now;
    s_list[0] = p;
    s_heard[0] = s_frame ? s_frame : 1;
    s_dirty = true;
    if (meeting) snprintf(s_met, sizeof(s_met), "%s", c->name);
    return meeting;
}

void nearby_set_me(const NearbyCard *me) { s_me = *me; }

void nearby_set_photo(const uint16_t *px, uint32_t stamp) { s_my_ph = px; s_my_ph_stamp = stamp ? stamp : 1; }

int nearby_photo_encode(int part, uint8_t *out, int max) {
    if (!s_my_ph || part < 0 || part >= NEARBY_PH_PARTS || max < 15 + NEARBY_PH_PART) return 0;
    uint8_t *p = out;
    wr(&p, "HNPH", 4);
    *p++ = 1;
    wr(&p, &s_me.id, 4);
    wr(&p, &s_my_ph_stamp, 4);
    *p++ = (uint8_t)part;
    *p++ = (uint8_t)NEARBY_PH_PARTS;
    wr(&p, (const uint8_t *)s_my_ph + part * NEARBY_PH_PART, NEARBY_PH_PART);
    return (int)(p - out);
}

bool nearby_photo_path(uint32_t id, char *out, size_t len) {
    if (!s_ph_dir[0] || !id) return false;
    snprintf(out, len, "%s/%08x.bin", s_ph_dir, (unsigned)id);
    FILE *f = fopen(out, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool nearby_take_photo(void) { bool r = s_ph_new; s_ph_new = false; return r; }

static bool save_photo(uint32_t id, const uint16_t *px) {
    if (!s_ph_dir[0]) return false;
    mkdir(s_ph_dir, 0777);
    char path[MAX_PATH_LEN + 16], tmp[MAX_PATH_LEN + 24];
    snprintf(path, sizeof(path), "%s/%08x.bin", s_ph_dir, (unsigned)id);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    uint8_t h[8] = { 'H', 'F', 'C', '1', NEARBY_PH_W & 255, NEARBY_PH_W >> 8, NEARBY_PH_H & 255, NEARBY_PH_H >> 8 };
    bool ok = fwrite(h, 1, 8, f) == 8 && fwrite(px, 2, NEARBY_PH_W * NEARBY_PH_H, f) == NEARBY_PH_W * NEARBY_PH_H;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    remove(path);
    return rename(tmp, path) == 0;
}

bool nearby_photo_receive(const uint8_t *d, int len) {
    if (len < 15 + NEARBY_PH_PART || memcmp(d, "HNPH", 4) != 0 || d[4] != 1) return false;
    uint32_t id, stamp;
    memcpy(&id, d + 5, 4);
    memcpy(&stamp, d + 9, 4);
    int part = d[13], parts = d[14];
    if (!s_in || !id || id == s_me.id || parts != NEARBY_PH_PARTS || part >= parts) return false;
    for (int i = 0; i < s_have_n; i++) if (s_have[i].id == id && s_have[i].stamp == stamp) return false;   // already kept
    int slot = -1, oldest = 0;
    for (int i = 0; i < PH_SLOTS; i++) {
        if (s_in[i].id == id) { slot = i; break; }
        if (s_in[i].age < s_in[oldest].age) oldest = i;
    }
    if (slot < 0) { slot = oldest; s_in[slot].id = id; s_in[slot].stamp = stamp; s_in[slot].mask = 0; }
    if (s_in[slot].stamp != stamp) { s_in[slot].stamp = stamp; s_in[slot].mask = 0; }
    s_in[slot].age = s_frame + 1;
    memcpy((uint8_t *)s_in[slot].px + part * NEARBY_PH_PART, d + 15, NEARBY_PH_PART);
    s_in[slot].mask |= 1u << part;
    if (s_in[slot].mask != (1u << NEARBY_PH_PARTS) - 1) return false;
    for (int i = 0; i < NEARBY_PH_W * NEARBY_PH_H; i++) s_in[slot].px[i] |= 0x8000;
    bool ok = save_photo(id, s_in[slot].px);
    if (ok) {
        if (s_have_n < NEARBY_MAX) { s_have[s_have_n].id = id; s_have[s_have_n].stamp = stamp; s_have_n++; }
        s_ph_new = true;
    }
    s_in[slot].id = 0;
    s_in[slot].mask = 0;
    return ok;
}

static void save(void) {
    if (!s_path[0] || !s_dirty) return;
    char tmp[MAX_PATH_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", s_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    uint32_t h[3] = { LIST_MAGIC, (uint32_t)s_n, (uint32_t)sizeof(NearbyPerson) };
    bool ok = fwrite(h, 4, 3, f) == 3 && (s_n == 0 || fwrite(s_list, sizeof(NearbyPerson), (size_t)s_n, f) == (size_t)s_n);
    fclose(f);
    if (!ok) { remove(tmp); return; }
    remove(s_path);
    rename(tmp, s_path);
    s_dirty = false;
}

void nearby_open(const char *list_path) {
    snprintf(s_path, sizeof(s_path), "%s", list_path ? list_path : "");
    snprintf(s_ph_dir, sizeof(s_ph_dir), "%s", s_path);
    char *sl = strrchr(s_ph_dir, '/');
    if (sl) snprintf(sl + 1, sizeof(s_ph_dir) - (size_t)(sl + 1 - s_ph_dir), "nearby_photos");
    else s_ph_dir[0] = '\0';
    free(s_in);
    s_in = calloc(PH_SLOTS, sizeof(PhIn));
    s_have_n = 0;
    s_ph_new = false;
    s_n = 0;
    s_frame = 0;
    s_met[0] = '\0';
    memset(s_heard, 0, sizeof(s_heard));
    FILE *f = s_path[0] ? fopen(s_path, "rb") : NULL;
    if (f) {
        uint32_t h[3];
        if (fread(h, 4, 3, f) == 3 && h[0] == LIST_MAGIC && h[2] == sizeof(NearbyPerson) && h[1] <= NEARBY_MAX)
            s_n = (int)fread(s_list, sizeof(NearbyPerson), h[1], f);
        fclose(f);
    }
    s_dirty = false;
}

void nearby_close(void) {
    save();
    free(s_in);
    s_in = NULL;
    if (s_sock >= 0) { closesocket(s_sock); s_sock = -1; }
}

static void send_to(uint32_t ip, const uint8_t *b, int n) {
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(NEARBY_PORT);
    to.sin_addr.s_addr = ip;
    sendto(s_sock, b, (size_t)n, 0, (const struct sockaddr *)&to, sizeof(to));
}

static void broadcast(const uint8_t *b, int n, uint32_t ip, uint32_t mask) {
#ifdef ARM9
    send_to(0xFFFFFFFF, b, n);
    if (ip && mask) send_to(ip | ~mask, b, n);
#else
    (void)ip; (void)mask;
    const char *peer = getenv("HL_NEARBY_PEER");   // tests: where the "other console" listens
    if (peer) {
        struct sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons((uint16_t)atoi(strchr(peer, ':') ? strchr(peer, ':') + 1 : "39514"));
        char host[32];
        snprintf(host, sizeof(host), "%.*s", (int)(strchr(peer, ':') ? strchr(peer, ':') - peer : (long)strlen(peer)), peer);
        to.sin_addr.s_addr = inet_addr(host);
        sendto(s_sock, b, (size_t)n, 0, (const struct sockaddr *)&to, sizeof(to));
    }
#endif
}

void nearby_frame(uint32_t ip, uint32_t mask) {
    s_frame++;
    if (s_sock < 0) {
        s_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (s_sock < 0) return;
        int on = 1;
#ifndef ARM9
        setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
#endif
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(NEARBY_PORT);
        a.sin_addr.s_addr = INADDR_ANY;
        bind(s_sock, (struct sockaddr *)&a, sizeof(a));
        ioctl(s_sock, FIONBIO, &on);
    }
    static uint8_t b[1400];
    int n = 0;
    if ((s_frame % 120) == 1) n = nearby_encode(&s_me, b, sizeof(b));
    else if (s_my_ph && (s_frame % 120) >= 10 && (s_frame % 120) < 10 + NEARBY_PH_PARTS)
        n = nearby_photo_encode((s_frame % 120) - 10, b, sizeof(b));   // the shared photo, a part per frame
    if (n > 0) broadcast(b, n, ip, mask);
    static uint8_t buf[1600];
    for (int k = 0; k < 8; k++) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(s_sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) break;
        NearbyCard c;
        if (nearby_decode(buf, n, &c)) nearby_merge(&c, (uint32_t)time(NULL));
        else nearby_photo_receive(buf, n);
    }
    if ((s_frame % 600) == 0) save();
}

int nearby_count(void) { return s_n; }
const NearbyPerson *nearby_get(int i) { return (i >= 0 && i < s_n) ? &s_list[i] : NULL; }
bool nearby_here(int i) { return i >= 0 && i < s_n && s_heard[i] && s_frame - s_heard[i] < 600; }

bool nearby_take_met(char *name, size_t len) {
    if (!s_met[0]) return false;
    if (name && len) snprintf(name, len, "%s", s_met);
    s_met[0] = '\0';
    return true;
}

bool nearby_remove(int i) {
    if (i < 0 || i >= s_n) return false;
    memmove(&s_list[i], &s_list[i + 1], sizeof(NearbyPerson) * (size_t)(s_n - i - 1));
    memmove(&s_heard[i], &s_heard[i + 1], sizeof(int) * (size_t)(s_n - i - 1));
    s_n--;
    s_dirty = true;
    save();
    return true;
}

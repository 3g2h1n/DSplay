// Wireless transfer over TCP (see include/xfer.h)
#include "xfer.h"
#include "stats.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>
#ifdef ARM9
#include <nds.h>
#else
#define closesocket close
#endif

enum { X_IDLE, X_HDR, X_CMD, X_PUT_DATA, X_GET_DATA };

static char     s_drv[64];
static uint32_t s_token;
static int      s_listen = -1, s_conn = -1;
static int      s_state = X_IDLE;
static uint8_t  s_in[16384];
static int      s_in_n;
static uint8_t *s_out;
static int      s_out_n, s_out_cap, s_out_pos;
static FILE    *s_file;
static char     s_part[MAX_PATH_LEN + 8], s_dest[MAX_PATH_LEN];
static uint32_t s_left, s_size;
static bool     s_overwrite, s_rescan;
static char     s_name[64];
static int      s_done_files;

static void set_nonblock(int fd) {
    int on = 1;
    ioctl(fd, FIONBIO, &on);
}

// ---------------------------------------------------------------------------
bool xfer_path_ok(const char *rel, bool for_write) {
    size_t n = strlen(rel);
    if (n > 200 || rel[0] == '/' || strchr(rel, '\\') || strchr(rel, ':')) return false;
    // no ".." components
    const char *p = rel;
    while (*p) {
        const char *e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.') return false;
        if (!e) break;
        p = e + 1;
    }
    if (!for_write) return true;
    if (!n) return false;
    return !strncasecmp(rel, "roms/", 5) || !strncasecmp(rel, "_hyper/covers/", 14) || !strncasecmp(rel, "_hyper/store/", 13);
}

static bool is_save_name(const char *rel) {
    const char *dot = strrchr(rel, '.');
    if (!dot) return false;
    return !strncasecmp(dot, ".sav", 4) || !strcasecmp(dot, ".srm") || !strcasecmp(dot, ".dsv");
}

static void full_path(const char *rel, char *out, size_t len) {
    snprintf(out, len, "%s/%s", s_drv, rel);
}

// ---------------------------------------------------------------------------
// output queue
static bool out_reserve(int extra) {
    if (s_out_n + extra <= s_out_cap) return true;
    int cap = s_out_cap ? s_out_cap : 4096;
    while (cap < s_out_n + extra) cap *= 2;
    if (cap > 2 * 1024 * 1024) return false;
    uint8_t *n = realloc(s_out, (size_t)cap);
    if (!n) return false;
    s_out = n;
    s_out_cap = cap;
    return true;
}

static void out_put(const void *d, int n) {
    if (!out_reserve(n)) return;
    memcpy(s_out + s_out_n, d, (size_t)n);
    s_out_n += n;
}

static void out_u8(uint8_t v) { out_put(&v, 1); }
static void out_u16(uint16_t v) { out_put(&v, 2); }
static void out_u32(uint32_t v) { out_put(&v, 4); }

static void out_flush(void) {
    while (s_out_pos < s_out_n) {
        int n = (int)send(s_conn, s_out + s_out_pos, (size_t)(s_out_n - s_out_pos), 0);
        if (n <= 0) break;
        s_out_pos += n;
    }
    if (s_out_pos == s_out_n) s_out_pos = s_out_n = 0;
}

// ---------------------------------------------------------------------------
static void drop_conn(void) {
    if (s_file) { fclose(s_file); s_file = NULL; remove(s_part); }
    if (s_conn >= 0) { closesocket(s_conn); s_conn = -1; }
    s_state = X_IDLE;
    s_in_n = s_out_n = s_out_pos = 0;
    s_name[0] = '\0';
}

static void consume(int n) {
    memmove(s_in, s_in + n, (size_t)(s_in_n - n));
    s_in_n -= n;
}

static void set_name(const char *rel) {
    const char *b = strrchr(rel, '/');
    snprintf(s_name, sizeof(s_name), "%s", b ? b + 1 : rel);
}

static void list_dir(const char *rel, int depth) {
    char path[MAX_PATH_LEN];
    full_path(rel, path, sizeof(path));
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char child[MAX_PATH_LEN];
        if (rel[0]) snprintf(child, sizeof(child), "%s/%s", rel, e->d_name);
        else snprintf(child, sizeof(child), "%s", e->d_name);
        if (!rel[0]) {           // card root: leave out the system's own folders
            static const char *const skip[] = { "_nds", "title", "ticket", "sys", "shared1", "shared2", "private", "tmp",
                                                "system volume information" };
            bool sk = false;
            for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) if (!strcasecmp(e->d_name, skip[i])) sk = true;
            if (sk) continue;
        }
        char full[MAX_PATH_LEN];
        full_path(child, full, sizeof(full));
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 6) list_dir(child, depth + 1);
        } else {
            uint16_t n = (uint16_t)strlen(child);
            out_u16(n);
            out_put(child, n);
            out_u32((uint32_t)st.st_size);
        }
    }
    closedir(d);
}

// returns false when more bytes are needed
static bool handle_cmd(void) {
    if (s_in_n < 1) return false;
    int op = s_in[0];
    if (op == 4) {                                    // DONE
        consume(1);
        out_u8(XFER_OK);
        s_rescan = true;
        return true;
    }
    if (s_in_n < 3) return false;
    uint16_t plen;
    memcpy(&plen, s_in + 1, 2);
    if (plen > 220) { drop_conn(); return false; }
    int need = 3 + plen + (op == 1 ? 5 : op == 2 ? 8 : 0);
    if (s_in_n < need) return false;
    char rel[224];
    memcpy(rel, s_in + 3, plen);
    rel[plen] = '\0';
    const uint8_t *q = s_in + 3 + plen;
    char path[MAX_PATH_LEN];
    if (op == 1) {                                    // PUT
        uint32_t size;
        memcpy(&size, q, 4);
        s_overwrite = q[4] != 0;
        consume(need);
        uint8_t st = XFER_OK;
        struct stat sb;
        full_path(rel, path, sizeof(path));
        bool exists = stat(path, &sb) == 0;
        if (!xfer_path_ok(rel, true)) st = XFER_DENIED;
        else if (exists && !s_overwrite && (uint32_t)sb.st_size == size) st = XFER_SAME;
        else if (exists && (is_save_name(rel) || !s_overwrite)) st = XFER_EXISTS;   // saves are never replaced
        if (st == XFER_OK) {
            char dir[MAX_PATH_LEN];
            snprintf(dir, sizeof(dir), "%s", path);
            char *sl = strrchr(dir, '/');
            if (sl) { *sl = '\0'; make_dirs(dir); }
            snprintf(s_part, sizeof(s_part), "%s.hlpart", path);
            snprintf(s_dest, sizeof(s_dest), "%s", path);
            s_file = fopen(s_part, "wb");
            if (!s_file) st = XFER_IOERR;
        }
        out_u8(st);
        if (st != XFER_OK) return true;
        s_left = s_size = size;
        set_name(rel);
        s_state = X_PUT_DATA;
        if (!size) { s_left = 0; }
        return true;
    }
    if (op == 2) {                                    // GET
        uint32_t off, len;
        memcpy(&off, q, 4);
        memcpy(&len, q + 4, 4);
        consume(need);
        full_path(rel, path, sizeof(path));
        FILE *f = xfer_path_ok(rel, false) ? fopen(path, "rb") : NULL;
        if (!f) { out_u8(XFER_IOERR); out_u32(0); return true; }
        fseek(f, 0, SEEK_END);
        long total = ftell(f);
        if ((long)off > total) off = (uint32_t)total;
        uint32_t n = (uint32_t)(total - (long)off);
        if (len && len < n) n = len;
        fseek(f, (long)off, SEEK_SET);
        out_u8(XFER_OK);
        out_u32(n);
        s_file = f;
        s_left = s_size = n;
        set_name(rel);
        s_state = X_GET_DATA;
        return true;
    }
    if (op == 3) {                                    // LIST
        consume(need);
        if (xfer_path_ok(rel, false)) list_dir(rel, 0);
        out_u16(0);
        return true;
    }
    drop_conn();
    return false;
}

static void finish_put(void) {
    bool ok = s_file && fclose(s_file) == 0;
    s_file = NULL;
    if (ok) {
        struct stat sb;
        ok = stat(s_part, &sb) == 0 && (uint32_t)sb.st_size == s_size;
    }
    if (ok) {
        if (s_overwrite) remove(s_dest);
        ok = rename(s_part, s_dest) == 0;
    }
    if (!ok) remove(s_part);
    out_u8(ok ? XFER_OK : XFER_IOERR);
    if (ok) {
        s_done_files++;
        if (!strncasecmp(s_dest + strlen(s_drv) + 1, "roms/", 5)) s_rescan = true;
    }
    s_name[0] = '\0';
    s_state = X_CMD;
}

// ---------------------------------------------------------------------------
void xfer_start(const char *drive, uint32_t token) {
    snprintf(s_drv, sizeof(s_drv), "%s", drive);
    s_token = token;
    if (s_listen >= 0) return;
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen < 0) return;
    int on = 1;
#ifndef ARM9
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
    (void)on;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(XFER_PORT);
    a.sin_addr.s_addr = INADDR_ANY;
    if (bind(s_listen, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s_listen, 1) != 0) {
        closesocket(s_listen);
        s_listen = -1;
        return;
    }
    set_nonblock(s_listen);
}

void xfer_stop(void) {
    drop_conn();
    if (s_listen >= 0) { closesocket(s_listen); s_listen = -1; }
    free(s_out);
    s_out = NULL;
    s_out_cap = 0;
}

void xfer_poll(void) {
    if (s_listen < 0) return;
    if (s_conn < 0) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int c = accept(s_listen, (struct sockaddr *)&from, &fl);
        if (c < 0) return;
        s_conn = c;
        set_nonblock(s_conn);
        s_state = X_HDR;
        s_in_n = 0;
    }
    int budget = 96 * 1024;              // bytes of file data per frame: the screen stays responsive
    for (int guard = 0; guard < 64 && budget > 0 && s_conn >= 0; guard++) {
        out_flush();
        if (s_out_n > 32 * 1024) break;  // the computer is slow to read
        if (s_state == X_GET_DATA) {
            if (!s_left) { fclose(s_file); s_file = NULL; s_state = X_CMD; s_name[0] = '\0'; continue; }
            int n = s_left > 8192 ? 8192 : (int)s_left;
            if (!out_reserve(n)) break;
            int got = (int)fread(s_out + s_out_n, 1, (size_t)n, s_file);
            if (got <= 0) { drop_conn(); break; }
            s_out_n += got;
            s_left -= (uint32_t)got;
            budget -= got;
            continue;
        }
        if (s_state == X_PUT_DATA && s_left == 0) { finish_put(); continue; }
        int r = (int)recv(s_conn, s_in + s_in_n, sizeof(s_in) - (size_t)s_in_n, 0);
        if (r == 0) { drop_conn(); break; }
        if (r > 0) s_in_n += r;
        if (s_in_n == 0) break;
        if (s_state == X_HDR) {
            if (s_in_n < 8) break;
            uint32_t tok;
            memcpy(&tok, s_in + 4, 4);
            if (memcmp(s_in, "HXFR", 4) != 0 || tok != s_token || !s_token) { drop_conn(); break; }
            consume(8);
            s_state = X_CMD;
            continue;
        }
        if (s_state == X_PUT_DATA) {
            int n = s_in_n < (int)s_left ? s_in_n : (int)s_left;
            if (n > 0) {
                if (fwrite(s_in, 1, (size_t)n, s_file) != (size_t)n) {
                    fclose(s_file); s_file = NULL; remove(s_part);
                    drop_conn();
                    break;
                }
                consume(n);
                s_left -= (uint32_t)n;
                budget -= n;
            }
            if (s_left == 0) finish_put();
            if (r <= 0 && n == 0) break;
            continue;
        }
        if (s_state == X_CMD) {
            if (!handle_cmd() && r <= 0) break;
        }
    }
    if (s_conn >= 0) out_flush();
}

bool xfer_busy(void) { return s_conn >= 0; }
const char *xfer_name(void) { return s_name; }
int xfer_percent(void) { return s_size ? (int)((uint64_t)(s_size - s_left) * 100 / s_size) : 0; }
int xfer_files_done(void) { return s_done_files; }
bool xfer_take_rescan(void) { bool r = s_rescan; s_rescan = false; return r; }

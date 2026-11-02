// "Kumanda": the DSi as a wireless controller for DSplay Studio (see include/pad.h)
#include "pad.h"
#include "ui.h"
#include "i18n.h"
#include "xfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <unistd.h>
#ifdef ARM9
#include <nds.h>
#include <dswifi9.h>
#include <wfc.h>
#else
// Host build (tests / simulator): no Wi-Fi, the network is the computer's own.
// Networking is only switched on when HL_PAD_HOST is set to the address of the "PC".
#include <arpa/inet.h>
#define closesocket close
#endif

// ---------------------------------------------------------------------------
// Layout: pages of widgets on the lower screen
//   "HLAY", u8 version (1), u8 pages, u16 reserved
//   page:   u8 widget count, char name[12]
//   widget: u8 id, u8 type, u8 x, u8 y, u8 w, u8 h, u8 color, u8 shape, char label[12]
// ---------------------------------------------------------------------------
enum { W_BUTTON = 1, W_STICK = 2, W_TRACKPAD = 3, W_SLIDER = 4 };
#define MAX_PAGES   4
#define MAX_WIDGETS 16

typedef struct {
    uint8_t id, type, x, y, w, h, color, shape;
    char    label[13];
} Widget;

typedef struct {
    char   name[13];
    int    n;
    Widget w[MAX_WIDGETS];
} Page;

static Page s_pages[MAX_PAGES];
static int  s_npages;

int pad_layout_parse(const uint8_t *d, int len) {
    if (len < 8 || memcmp(d, "HLAY", 4) != 0 || d[4] != 1) return 0;
    int np = d[5];
    if (np < 1 || np > MAX_PAGES) return 0;
    Page tmp[MAX_PAGES];
    memset(tmp, 0, sizeof(tmp));
    int o = 8;
    for (int p = 0; p < np; p++) {
        if (o + 13 > len) return 0;
        int nw = d[o];
        if (nw > MAX_WIDGETS) return 0;
        memcpy(tmp[p].name, d + o + 1, 12);
        o += 13;
        for (int i = 0; i < nw; i++) {
            if (o + 20 > len) return 0;
            Widget *w = &tmp[p].w[i];
            w->id = d[o]; w->type = d[o + 1]; w->x = d[o + 2]; w->y = d[o + 3];
            w->w = d[o + 4]; w->h = d[o + 5]; w->color = d[o + 6]; w->shape = d[o + 7];
            memcpy(w->label, d + o + 8, 12);
            if (w->type < W_BUTTON || w->type > W_SLIDER || w->id >= 32 || w->w < 8 || w->h < 8) return 0;
            if (w->y + w->h > 172) w->h = (uint8_t)(172 - w->y);
            o += 20;
        }
        tmp[p].n = nw;
    }
    memcpy(s_pages, tmp, sizeof(tmp));
    s_npages = np;
    return np;
}

static void add_w(Page *p, int id, int type, int x, int y, int w, int h, int color, const char *label) {
    Widget *wd = &p->w[p->n++];
    memset(wd, 0, sizeof(*wd));
    wd->id = (uint8_t)id; wd->type = (uint8_t)type; wd->x = (uint8_t)x; wd->y = (uint8_t)y;
    wd->w = (uint8_t)w; wd->h = (uint8_t)h; wd->color = (uint8_t)color;
    snprintf(wd->label, sizeof(wd->label), "%s", label);
}

// Used until Studio sends a layout: a stick + four buttons, and a mouse page
void pad_layout_default(void) {
    memset(s_pages, 0, sizeof(s_pages));
    Page *p = &s_pages[0];
    snprintf(p->name, sizeof(p->name), "Kol");
    add_w(p, 0, W_STICK, 8, 26, 112, 112, 0, "R");
    add_w(p, 1, W_BUTTON, 130, 26, 56, 40, 1, "LT");
    add_w(p, 2, W_BUTTON, 192, 26, 56, 40, 1, "RT");
    add_w(p, 3, W_BUTTON, 130, 72, 56, 40, 8, "L3");
    add_w(p, 4, W_BUTTON, 192, 72, 56, 40, 8, "R3");
    add_w(p, 5, W_BUTTON, 130, 118, 118, 30, 9, "Guide");
    p = &s_pages[1];
    snprintf(p->name, sizeof(p->name), "Fare");
    add_w(p, 6, W_TRACKPAD, 8, 26, 196, 130, 9, "");
    add_w(p, 7, W_BUTTON, 210, 26, 38, 62, 0, "Sol");
    add_w(p, 8, W_BUTTON, 210, 94, 38, 62, 0, "Sağ");
    s_npages = 2;
}

int pad_layout_pages(void) { return s_npages; }
int pad_layout_widgets(int page) { return (page >= 0 && page < s_npages) ? s_pages[page].n : 0; }

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
typedef enum { ST_WIFI, ST_WIFI_FAIL, ST_SEARCH, ST_CONNECTED } PadState;

static PadState s_st;
static bool     s_store_mode;        // the store screen uses the connection: no controller input is sent
static bool     s_net_only;          // "Nearby": only Wi-Fi, no computer is looked for
static struct { bool have; uint8_t op, status; char slug[40]; } s_store_res;
static char     s_drv[64], s_name[24];
static int      s_lang;
static int      s_page;
static uint32_t s_pressed;           // widgets held
static int8_t   s_stick_x, s_stick_y;
static int      s_pad_dx, s_pad_dy, s_last_tx, s_last_ty;
static bool     s_was_touching;
static int      s_touch_widget = -1;
static uint8_t  s_slider[MAX_WIDGETS * MAX_PAGES];
static uint16_t s_code;              // pairing code shown on the screen
static uint32_t s_pc_id, s_token;    // computer we are paired with (pad.cfg)
static uint32_t s_dsi_id;            // this console, made up once
static uint32_t s_pc_ip;             // where that computer was last time (network order)
static uint32_t s_my_ip, s_my_mask;  // this console on the network
static int      s_rx;                // valid packets from a computer (shown while searching)
// what the computer tells about itself (T_INFO, every 2 s): shown on the upper screen
static struct { bool have; uint8_t cpu, ram, batt, charging; char title[49], artist[33]; } s_info;
static uint8_t  s_mic_level;         // loudest microphone sample of the last frame (0..127)
static char     s_pc_name[33];
static int      s_rtt = -1;          // ms
static int      s_frames, s_last_rx, s_exit_hold;
static uint16_t s_seq;
static int      s_sig_extra;
static uint32_t s_held;

static int  s_sock = -1;
static struct sockaddr_in s_pc_addr;
static int  s_bad_codes;
#ifdef ARM9
static bool s_wifi_inited;
#endif

static const char *T_(StrId id) { return tr(id, (uint8_t)s_lang); }

static void cfg_path(char *out, size_t len) { snprintf(out, len, "%s/_hyper/pad.cfg", s_drv); }

static void cfg_save(void);

static void cfg_load(void) {
    s_pc_id = s_token = s_dsi_id = 0;
    s_pc_name[0] = '\0';
    char p[96];
    cfg_path(p, sizeof(p));
    FILE *f = fopen(p, "rb");
    uint8_t b[52];
    s_pc_ip = 0;
    if (f) {
        size_t n = fread(b, 1, sizeof(b), f);
        if (n >= 48 && (!memcmp(b, "HPC2", 4) || !memcmp(b, "HPC3", 4))) {
            memcpy(&s_dsi_id, b + 4, 4);
            memcpy(&s_pc_id, b + 8, 4);
            memcpy(&s_token, b + 12, 4);
            memcpy(s_pc_name, b + 16, 32);
            s_pc_name[32] = '\0';
            if (n >= 52 && !memcmp(b, "HPC3", 4)) memcpy(&s_pc_ip, b + 48, 4);
        }
        fclose(f);
    }
    if (!s_dsi_id) {
        s_dsi_id = ((uint32_t)time(NULL) * 2654435761u) ^ ((uint32_t)(uintptr_t)&b * 40503u) ^ 0x5D51u;
        if (!s_dsi_id) s_dsi_id = 1;
        cfg_save();
    }
}

static void cfg_save(void) {
    char p[96];
    cfg_path(p, sizeof(p));
    FILE *f = fopen(p, "wb");
    if (!f) return;
    uint8_t b[52] = { 'H', 'P', 'C', '3' };
    memcpy(b + 4, &s_dsi_id, 4);
    memcpy(b + 8, &s_pc_id, 4);
    memcpy(b + 12, &s_token, 4);
    memcpy(b + 16, s_pc_name, 32);
    memcpy(b + 48, &s_pc_ip, 4);
    fwrite(b, 1, sizeof(b), f);
    fclose(f);
}

// last layout from Studio, kept so the right screen shows up before the computer answers
static void layout_cache(const uint8_t *d, int len, bool save) {
    char p[96];
    snprintf(p, sizeof(p), "%s/_hyper/pad_layout.bin", s_drv);
    FILE *f = fopen(p, save ? "wb" : "rb");
    if (!f) return;
    if (save) fwrite(d, 1, (size_t)len, f);
    else {
        static uint8_t buf[1500];
        int n = (int)fread(buf, 1, sizeof(buf), f);
        if (n > 0) pad_layout_parse(buf, n);
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
enum { T_HELLO = 1, T_PAIR = 2, T_ACCEPT = 3, T_CONNECT = 4, T_STATE = 5, T_LAYOUT = 6, T_PING = 7, T_PONG = 8, T_BYE = 9, T_SEEK = 10, T_INFO = 11, T_STORE = 12, T_STORE_RES = 13 };

static int put_header(uint8_t *b, int type) {
    memcpy(b, "HPAD", 4);
    b[4] = 1;
    b[5] = (uint8_t)type;
    uint16_t seq = s_seq++;
    memcpy(b + 6, &seq, 2);
    memcpy(b + 8, &s_token, 4);
    return 12;
}

static void net_send_to(const struct sockaddr_in *to, const uint8_t *b, int n) {
    if (s_sock >= 0) sendto(s_sock, b, (size_t)n, 0, (const struct sockaddr *)to, sizeof(*to));
}

static void send_hello_to(uint32_t ip_net, uint16_t port_host);

// Many home routers drop some kinds of broadcast between wireless devices, so the console
// calls out three ways: to everyone, to its own subnet, and to the computer it knew before.
static void send_hello(void) {
#ifdef ARM9
    send_hello_to(0xFFFFFFFF, PAD_PORT_PC);
    if (s_my_ip && s_my_mask) send_hello_to(s_my_ip | ~s_my_mask, PAD_PORT_PC);
    if (s_pc_ip) send_hello_to(s_pc_ip, PAD_PORT_PC);
#else
    send_hello_to(inet_addr(getenv("HL_PAD_HOST")), PAD_PORT_PC);
#endif
}

static void send_hello_to(uint32_t ip_net, uint16_t port_host) {
    // the token and the pairing code are never broadcast: the computer has to know them
    uint8_t b[64];
    int o = put_header(b, T_HELLO);
    memset(b + 8, 0, 4);
    memset(b + o, 0, 24);
    memcpy(b + o, s_name, strlen(s_name) < 23 ? strlen(s_name) : 23);
    o += 24;
    memcpy(b + o, &s_dsi_id, 4); o += 4;
    memcpy(b + o, &s_pc_id, 4); o += 4;
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port_host);
    to.sin_addr.s_addr = ip_net;
    net_send_to(&to, b, o);
}

// Blowing into the microphone: Studio can map it to a key (Controller page)
#ifdef ARM9
static s8 s_mic_buf[1024] __attribute__((aligned(32)));
static bool s_mic_on;
static void mic_on(void) {
    if (s_mic_on) return;
    micInit();
    pmMicSetAmp(true, PmMicGain_80);
    micSetDmaRate(MicRate_Div4);
    s_mic_on = micStart(s_mic_buf, sizeof(s_mic_buf), MicFmt_Pcm8, MicMode_Repeat);
}
static void mic_off(void) {
    if (!s_mic_on) return;
    micStop();
    pmMicSetAmp(false, 0);
    s_mic_on = false;
}
static void mic_read(void) {
    if (!s_mic_on) { s_mic_level = 0; return; }
    DC_InvalidateRange(s_mic_buf, sizeof(s_mic_buf));
    int mx = 0;
    for (size_t i = 0; i < sizeof(s_mic_buf); i += 2) { int v = s_mic_buf[i]; if (v < 0) v = -v; if (v > mx) mx = v; }
    s_mic_level = (uint8_t)(mx > 127 ? 127 : mx);
}
#else
static void mic_on(void) {}
static void mic_off(void) {}
static void mic_read(void) {}
#endif

static void send_state(void) {
    uint8_t b[48];
    int o = put_header(b, T_STATE);
    uint16_t keys = (uint16_t)(s_held & 0x0FFF);
    memcpy(b + o, &keys, 2); o += 2;
    b[o++] = (uint8_t)(s_was_touching ? s_last_tx : 0);
    b[o++] = (uint8_t)(s_was_touching ? s_last_ty : 0);
    b[o++] = s_was_touching ? 1 : 0;
    b[o++] = (uint8_t)s_page;
    memcpy(b + o, &s_pressed, 4); o += 4;
    b[o++] = (uint8_t)s_stick_x;
    b[o++] = (uint8_t)s_stick_y;
    int16_t dx = (int16_t)s_pad_dx, dy = (int16_t)s_pad_dy;
    memcpy(b + o, &dx, 2); o += 2;
    memcpy(b + o, &dy, 2); o += 2;
    s_pad_dx = s_pad_dy = 0;
    // slider values of the current page (up to 4)
    int k = 0;
    uint8_t sl[4] = { 0 };
    for (int i = 0; i < s_pages[s_page].n && k < 4; i++)
        if (s_pages[s_page].w[i].type == W_SLIDER) sl[k++] = s_slider[s_pages[s_page].w[i].id];
    memcpy(b + o, sl, 4); o += 4;
    unsigned bs = ui_battery_state();
    b[o++] = (uint8_t)bs;
#ifdef ARM9
    b[o++] = (uint8_t)wlmgrGetSignalStrength();
#else
    b[o++] = 3;
#endif
    mic_read();
    b[o++] = s_mic_level;
    net_send_to(&s_pc_addr, b, o);
}

static void handle_packet(const uint8_t *b, int n, const struct sockaddr_in *from) {
    if (n < 12 || memcmp(b, "HPAD", 4) != 0 || b[4] != 1) return;
    int type = b[5];
    const uint8_t *p = b + 12;
    int len = n - 12;
    uint32_t tok;
    memcpy(&tok, b + 8, 4);
    if ((type == T_PAIR || type == T_CONNECT) && s_st == ST_CONNECTED && from->sin_addr.s_addr != s_pc_addr.sin_addr.s_addr) return;
    if (s_st == ST_WIFI || s_st == ST_WIFI_FAIL) return;
    s_rx++;
    if (type == T_SEEK) {                               // a computer looking for consoles: answer it directly
        if (s_st == ST_SEARCH) send_hello_to(from->sin_addr.s_addr, PAD_PORT_PC);
        return;
    }
    if (type == T_PAIR && len >= 2 + 4 + 4 + 32) {
        uint16_t code;
        memcpy(&code, p, 2);
        if (code != s_code) {                           // wrong code typed on the computer
            if (++s_bad_codes >= 5) {                   // guessing? show a new code
                s_bad_codes = 0;
                s_code = (uint16_t)(1000 + (s_code * 7919u + (unsigned)s_frames * 104729u) % 9000);
                s_sig_extra++;
            }
            return;
        }
        s_bad_codes = 0;
        memcpy(&s_pc_id, p + 2, 4);
        memcpy(&s_token, p + 6, 4);
        memcpy(s_pc_name, p + 10, 32);
        s_pc_name[32] = '\0';
        cfg_save();
    } else if (type == T_CONNECT && len >= 4 + 4 + 32) {
        uint32_t id, t2;
        memcpy(&id, p, 4);
        memcpy(&t2, p + 4, 4);
        if (!s_pc_id || id != s_pc_id || t2 != s_token) return;   // not the computer we know
        memcpy(s_pc_name, p + 8, 32);
        s_pc_name[32] = '\0';
    } else if (s_st == ST_CONNECTED && tok == s_token) {
        if (type == T_LAYOUT) {
            if (pad_layout_parse(p, len)) {
                layout_cache(p, len, true);
                if (s_page >= s_npages) s_page = 0;
                s_pressed = 0;
                s_sig_extra++;
            }
        } else if (type == T_PING && len >= 6) {
            uint16_t rtt;
            memcpy(&rtt, p + 4, 2);
            s_rtt = rtt == 0xFFFF ? -1 : rtt;
            uint8_t r[24];
            int o = put_header(r, T_PONG);
            memcpy(r + o, p, 4); o += 4;
            net_send_to(&s_pc_addr, r, o);
        } else if (type == T_INFO && len >= 4 + 48 + 32) {
            s_info.have = true;
            s_info.cpu = p[0]; s_info.ram = p[1]; s_info.batt = p[2]; s_info.charging = p[3];
            memcpy(s_info.title, p + 4, 48); s_info.title[48] = '\0';
            memcpy(s_info.artist, p + 52, 32); s_info.artist[32] = '\0';
            s_sig_extra++;
        } else if (type == T_STORE_RES && len >= 2 + 40) {
            s_store_res.have = true;
            s_store_res.op = p[0];
            s_store_res.status = p[1];
            memcpy(s_store_res.slug, p + 2, 39);
            s_store_res.slug[39] = '\0';
        } else if (type == T_BYE) {
            s_st = ST_SEARCH;
            s_sig_extra++;
            return;
        }
        s_last_rx = s_frames;
        return;
    } else {
        return;
    }
    // paired / reconnected: answer and start sending
    s_pc_addr = *from;
    s_pc_addr.sin_port = htons(PAD_PORT_PC);
    if (s_pc_ip != from->sin_addr.s_addr) { s_pc_ip = from->sin_addr.s_addr; cfg_save(); }
    uint8_t r[24];
    int o = put_header(r, T_ACCEPT);
    memcpy(r + o, &s_pc_id, 4); o += 4;
    memcpy(r + o, &s_dsi_id, 4); o += 4;
    net_send_to(&s_pc_addr, r, o);
    s_st = ST_CONNECTED;
    xfer_start(s_drv, s_token);
    if (!s_store_mode) mic_on();   // Studio can copy games / read saves while connected
    s_last_rx = s_frames;
    s_sig_extra++;
}

static void net_poll(void) {
    if (s_sock < 0) return;
    static uint8_t buf[1600];
    for (int i = 0; i < 8; i++) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(s_sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) break;
        handle_packet(buf, n, &from);
    }
}

static bool net_open(void) {
    if (s_sock >= 0) return true;
    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock < 0) return false;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(PAD_PORT_DSI);
    a.sin_addr.s_addr = INADDR_ANY;
    int on = 1;
#ifndef ARM9
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
    bind(s_sock, (struct sockaddr *)&a, sizeof(a));
    ioctl(s_sock, FIONBIO, &on);
    return true;
}

#ifdef ARM9
// Wi-Fi: the network typed in DSplay Studio (_hyper/wifi.txt), otherwise the
// connections saved in the DSi's own Internet settings
static bool s_need_begin;

static void wifi_start_ex(bool only_typed) {
    if (!s_wifi_inited) {
        if (!wlmgrInitDefault() || !wfcInit()) { s_st = ST_WIFI_FAIL; return; }
        s_wifi_inited = true;
    }
    if (only_typed) wfcClearConnSlots();     // Settings > Wi-Fi: test just the network that was chosen
    else wfcLoadFromNvram();                 // (starts over from an empty slot list)
    char p[96];
    snprintf(p, sizeof(p), "%s/_hyper/wifi.txt", s_drv);
    FILE *f = fopen(p, "r");
    if (f) {
        char ssid[40] = "", pass[72] = "";
        if (fgets(ssid, sizeof(ssid), f) && fgets(pass, sizeof(pass), f)) {
            ssid[strcspn(ssid, "\r\n")] = '\0';
            pass[strcspn(pass, "\r\n")] = '\0';
        }
        fclose(f);
        if (ssid[0]) {
            static WfcConnSlotEx slot;
            memset(&slot, 0, sizeof(slot));
            size_t sl = strlen(ssid) > 32 ? 32 : strlen(ssid);
            memcpy(slot.base.ssid, ssid, sl);
            slot.base.ssid_len = (u16)sl;
            if (pass[0]) {
                // the key derivation takes a few seconds: keep the result next to wifi.txt
                char kp[96];
                snprintf(kp, sizeof(kp), "%s/_hyper/wifi.key", s_drv);
                uint8_t cached[32 + 104];
                bool have = false;
                FILE *k = fopen(kp, "rb");
                if (k) {
                    have = fread(cached, 1, sizeof(cached), k) == sizeof(cached) &&
                           !strncmp((char *)cached + 32, ssid, 40) && !strncmp((char *)cached + 72, pass, 64);
                    fclose(k);
                }
                if (!have) {
                    WlanAuthData auth;
                    memset(&auth, 0, sizeof(auth));
                    wfcDeriveWpaKey(&auth, ssid, (unsigned)sl, pass, (unsigned)strlen(pass));
                    memset(cached, 0, sizeof(cached));
                    memcpy(cached, auth.wpa_psk, 32);
                    strncpy((char *)cached + 32, ssid, 40);
                    strncpy((char *)cached + 72, pass, 64);
                    k = fopen(kp, "wb");
                    if (k) { fwrite(cached, 1, sizeof(cached), k); fclose(k); }
                }
                memcpy(slot.wpa_pmk, cached, 32);
                slot.base.conn_type = WfcConnType_WpaNormal;
                slot.wpa_mode = 0;           // WPA or WPA2, as the router announces (patched wfc.c)
            } else {
                slot.base.conn_type = WfcConnType_WepNormal;
                slot.base.wep_mode = WfcWepMode_Open;
            }
            if (wfcGetNumSlots() >= WFC_MAX_CONN_SLOTS) wfcClearConnSlots();   // the typed network wins
            wfcLoadSlotEx(&slot);
        }
    }
    if (!wfcGetNumSlots()) { s_st = ST_WIFI_FAIL; return; }
    s_need_begin = true;                     // started from pad_frame: the radio may still be stopping
}
static void wifi_start(void) { wifi_start_ex(false); }

static bool wifi_up(void) {
    if (s_need_begin && wfcBeginAutoConnect()) { s_need_begin = false; s_frames = 0; }
    return !s_need_begin && wfcGetStatus() == WfcStatus_Connected;
}
static bool wifi_gave_up(void) {
    if (s_need_begin) return s_frames > 180;
    return (wfcGetStatus() == WfcStatus_Disconnected && s_frames > 90) || s_frames > 60 * 25;
}

// ---- Settings > Wi-Fi: scan, choose, type the password, test ----------------
static WlanBssScanFilter s_scan_filter;
static int s_wt_frames;
static bool s_wt_on;
static bool s_scan_started;

bool wifi_scan_begin(const char *drive) {
    snprintf(s_drv, sizeof(s_drv), "%s", drive);
    if (!s_wifi_inited) {
        if (!wlmgrInitDefault() || !wfcInit()) return false;
        s_wifi_inited = true;
    }
    if (wlmgrGetState() == WlMgrState_Associated) wlmgrDisassociate();
    memset(&s_scan_filter, 0, sizeof(s_scan_filter));
    s_scan_filter.channel_mask = UINT32_MAX;
    memset(s_scan_filter.target_bssid, 0xFF, sizeof(s_scan_filter.target_bssid));
    s_wt_on = false;
    s_wt_frames = 0;
    s_scan_started = false;
    return true;
}

// -1 while scanning (call once a frame), else the number of networks (strongest first, one per name)
int wifi_scan_poll(WifiNet *out, int max) {
    bool started = s_scan_started;
    if (!started || s_wt_frames == 0) {
        s_wt_frames++;
        s_scan_started = started = wfcBeginScan(&s_scan_filter);
        if (!started) return s_wt_frames > 300 ? 0 : -1;    // the radio may still be busy
        return -1;
    }
    s_wt_frames++;
    if (wfcGetStatus() == WfcStatus_Scanning && s_wt_frames < 60 * 10) return -1;
    s_scan_started = false;
    s_wt_frames = 0;
    unsigned cnt = 0;
    WlanBssDesc *list = wfcGetScanBssList(&cnt);
    int n = 0;
    for (unsigned i = 0; list && i < cnt; i++) {
        const WlanBssDesc *b = &list[i];
        if (!b->ssid_len || b->ssid_len > 32 || !b->ssid[0]) continue;   // hidden networks
        char name[33];
        memcpy(name, b->ssid, b->ssid_len);
        name[b->ssid_len] = '\0';
        int at = -1;
        for (int k = 0; k < n; k++) if (!strcmp(out[k].ssid, name)) at = k;
        if (at >= 0) { if (b->rssi > out[at].rssi) out[at].rssi = b->rssi; continue; }
        if (n >= max) continue;
        WifiNet *w = &out[n++];
        memset(w, 0, sizeof(*w));
        snprintf(w->ssid, sizeof(w->ssid), "%s", name);
        w->rssi = b->rssi;
        unsigned m = b->auth_mask;
        w->secure = !(m & 1);
        w->wep_only = w->secure && !(m & 0xF0);
    }
    for (int i = 1; i < n; i++)               // strongest first
        for (int k = i; k > 0 && out[k].rssi > out[k - 1].rssi; k--) { WifiNet t = out[k]; out[k] = out[k - 1]; out[k - 1] = t; }
    return n;
}

void wifi_test_begin(void) {
    s_st = ST_WIFI;
    wifi_start_ex(true);
    s_wt_frames = 0;
    s_wt_on = true;
}

int wifi_test_poll(void) {
    if (!s_wt_on) return -1;
    if (s_st == ST_WIFI_FAIL) { s_wt_on = false; return -1; }
    s_frames++;
    s_wt_frames++;
    if (wifi_up()) { s_wt_on = false; return 1; }
    if (wifi_gave_up()) { s_wt_on = false; return -1; }
    return 0;
}

void wifi_off(void) {
    s_wt_on = false;
    if (s_wifi_inited) { wlmgrDisassociate(); wlmgrStop(); }
}

void pad_wifi_retry(void) {
    if (s_st == ST_WIFI_FAIL) { s_frames = 0; s_st = ST_WIFI; wifi_start(); }
}
#else
static void wifi_start(void) {}
static bool wifi_up(void) { return getenv("HL_PAD_HOST") != NULL; }
static bool wifi_gave_up(void) { return getenv("HL_WIFI_FAIL") != NULL && s_frames > 30; }

// simulator: two made-up networks; HL_WIFI_FAIL makes every connection fail
bool wifi_scan_begin(const char *drive) { snprintf(s_drv, sizeof(s_drv), "%s", drive); s_frames = 0; return true; }
int wifi_scan_poll(WifiNet *out, int max) {
    if (++s_frames < 20) return -1;
    static const char *const names[] = { "Ev_Agi_5G", "Ev_Agi", "Kafe Misafir" };
    int n = 0;
    for (int i = 0; i < 3 && n < max; i++) {
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", names[i]);
        out[n].rssi = 200 - i * 40;
        out[n].secure = i < 2;
        n++;
    }
    return n;
}
static int s_sim_test;
void wifi_test_begin(void) { s_sim_test = 1; }
int wifi_test_poll(void) {
    if (!s_sim_test) return -1;
    if (++s_sim_test < 40) return 0;
    s_sim_test = 0;
    return getenv("HL_WIFI_FAIL") ? -1 : 1;
}
void wifi_off(void) {}
void pad_wifi_retry(void) { if (s_st == ST_WIFI_FAIL) { s_frames = 0; s_st = ST_WIFI; } }
#endif

// _hyper/wifi.txt: the network chosen in Settings > Wi-Fi (or typed in DSplay Studio)
bool wifi_saved(const char *drive, char *ssid, size_t len) {
    char p[96];
    snprintf(p, sizeof(p), "%s/_hyper/wifi.txt", drive);
    FILE *f = fopen(p, "r");
    if (!f) return false;
    char line[72] = "";
    bool ok = fgets(line, sizeof(line), f) != NULL;
    fclose(f);
    line[strcspn(line, "\r\n")] = '\0';
    if (!ok || !line[0]) return false;
    if (ssid) snprintf(ssid, len, "%s", line);
    return true;
}

bool wifi_save(const char *drive, const char *ssid, const char *pass) {
    char p[96], tmp[104];
    snprintf(p, sizeof(p), "%s/_hyper/wifi.txt", drive);
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    fprintf(f, "%s\n%s\n", ssid, pass ? pass : "");
    bool ok = fclose(f) == 0;
    if (ok) { remove(p); ok = rename(tmp, p) == 0; }
    return ok;
}

void wifi_forget(const char *drive) {
    char p[96];
    snprintf(p, sizeof(p), "%s/_hyper/wifi.txt", drive);
    remove(p);
    snprintf(p, sizeof(p), "%s/_hyper/wifi.key", drive);
    remove(p);
}

// ---------------------------------------------------------------------------
// Touch -> widget states
// ---------------------------------------------------------------------------
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int widget_at(int x, int y) {
    Page *p = &s_pages[s_page];
    for (int i = 0; i < p->n; i++) {
        Widget *w = &p->w[i];
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return i;
    }
    return -1;
}

static void touch_update(const PadInput *in) {
    Page *p = &s_pages[s_page];
    if (in->touching) {
        if (!s_was_touching) s_touch_widget = widget_at(in->tx, in->ty);
        Widget *w = s_touch_widget >= 0 ? &p->w[s_touch_widget] : NULL;
        s_pressed = 0;
        if (w) {
            if (w->type == W_BUTTON) {
                if (in->tx >= w->x && in->tx < w->x + w->w && in->ty >= w->y && in->ty < w->y + w->h) s_pressed = 1u << w->id;
            } else if (w->type == W_STICK) {
                int cx = w->x + w->w / 2, cy = w->y + w->h / 2, r = (w->w < w->h ? w->w : w->h) / 2 - 6;
                int dx = in->tx - cx, dy = in->ty - cy;
                int vx = r > 0 ? dx * 127 / r : 0, vy = r > 0 ? dy * 127 / r : 0;
                s_stick_x = (int8_t)clampi(vx, -127, 127);
                s_stick_y = (int8_t)clampi(vy, -127, 127);
                s_pressed = 1u << w->id;
            } else if (w->type == W_TRACKPAD) {
                if (s_was_touching) { s_pad_dx += in->tx - s_last_tx; s_pad_dy += in->ty - s_last_ty; }
                s_pressed = 1u << w->id;
            } else if (w->type == W_SLIDER) {
                int v = (w->w >= w->h) ? (in->tx - w->x) * 255 / (w->w > 1 ? w->w - 1 : 1)
                                       : (w->y + w->h - 1 - in->ty) * 255 / (w->h > 1 ? w->h - 1 : 1);
                s_slider[w->id] = (uint8_t)clampi(v, 0, 255);
                s_pressed = 1u << w->id;
            }
        }
        s_last_tx = in->tx;
        s_last_ty = in->ty;
    } else {
        s_pressed = 0;
        s_stick_x = s_stick_y = 0;
        s_touch_widget = -1;
    }
    s_was_touching = in->touching;
}

// ---------------------------------------------------------------------------
void pad_enter(const char *drive, const char *profile_name, int language) {
    s_store_mode = false;
    s_net_only = false;
    memset(&s_store_res, 0, sizeof(s_store_res));
    snprintf(s_drv, sizeof(s_drv), "%s", drive ? drive : "");
    snprintf(s_name, sizeof(s_name), "%s", profile_name ? profile_name : "DSi");
    s_lang = language;
    s_page = 0;
    s_pressed = 0;
    s_rtt = -1;
    s_frames = 0;
    s_exit_hold = 0;
    s_code = (uint16_t)(1000 + ((unsigned)time(NULL) * 2654435761u >> 7) % 9000);
    pad_layout_default();
    layout_cache(NULL, 0, false);
    cfg_load();
    s_st = ST_WIFI;
    s_bad_codes = 0;
    wifi_start();
}

void pad_leave(void) {
    if (s_st == ST_CONNECTED) {
        uint8_t b[16];
        int o = put_header(b, T_BYE);
        net_send_to(&s_pc_addr, b, o);
    }
    xfer_stop();
    mic_off();
    memset(&s_info, 0, sizeof(s_info));
    if (s_sock >= 0) { closesocket(s_sock); s_sock = -1; }
#ifdef ARM9
    if (s_wifi_inited) { wlmgrDisassociate(); wlmgrStop(); }   // radio off: battery
#endif
    s_st = ST_WIFI;
}

bool pad_frame(const PadInput *in) {
    s_frames++;
    s_held = in->held;
    // leave: START + SELECT held for one second
    if ((in->held & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT)) {
        if (++s_exit_hold >= 60) return false;
    } else {
        s_exit_hold = 0;
    }
    if (s_st == ST_WIFI) {
        if (wifi_up()) {
#ifdef ARM9
            struct in_addr gw, mask, d1, d2;
            struct in_addr me = Wifi_GetIPInfo(&gw, &mask, &d1, &d2);
            s_my_ip = me.s_addr;
            s_my_mask = mask.s_addr;
#endif
            s_rx = 0;
            net_open();
            s_st = ST_SEARCH;
        } else if (wifi_gave_up()) {
            s_st = ST_WIFI_FAIL;
        }
    } else if (s_st == ST_WIFI_FAIL) {
        if (in->down & KEY_A) { s_frames = 0; s_st = ST_WIFI; wifi_start(); }
    } else {
        if (!wifi_up()) { s_st = ST_WIFI; s_frames = 0; wifi_start(); return true; }
        net_poll();
        if (s_st == ST_SEARCH) {
            if ((s_frames % 30) == 1 && !s_net_only) send_hello();
        } else {
            // the strip under the widgets switches pages: left half back, right half forward
            if (in->touching && !s_was_touching && in->ty >= 172 && s_npages > 1) {
                s_page = (s_page + (in->tx < 128 ? s_npages - 1 : 1)) % s_npages;
                s_pressed = 0;
                s_sig_extra++;
            }
            if (s_store_mode) {
                // keep the connection alive (twice a second), without any buttons or touches
                s_held = 0;
                s_was_touching = false;
                s_pressed = 0;
                if ((s_frames % 30) == 0) send_state();
            } else {
                touch_update(in);
                send_state();
            }
            xfer_poll();
            if (s_frames - s_last_rx > 180) { s_st = ST_SEARCH; s_rtt = -1; }   // nothing heard for 3 s
        }
    }
    return true;
}

uint16_t pad_pair_code(void) { return s_code; }

void pad_enter_store(const char *drive, const char *profile_name, int language) {
    s_store_mode = true;
    pad_enter(drive, profile_name, language);
    s_store_mode = true;
}

void pad_set_store_mode(bool on) { s_store_mode = on; }

void pad_enter_nearby(const char *drive, const char *profile_name, int language) {
    pad_enter(drive, profile_name, language);
    s_net_only = true;
    s_store_mode = true;
}

bool pad_net_info(uint32_t *ip, uint32_t *mask) {
    if (s_st != ST_SEARCH && s_st != ST_CONNECTED) return false;
    if (ip) *ip = s_my_ip;
    if (mask) *mask = s_my_mask;
    return true;
}

uint32_t pad_console_id(void) { return s_dsi_id; }

int pad_status(void) {
    switch (s_st) {
        case ST_WIFI: return PAD_ST_WIFI;
        case ST_WIFI_FAIL: return PAD_ST_WIFI_FAIL;
        case ST_SEARCH: return s_token ? PAD_ST_SEARCH : PAD_ST_UNPAIRED;
        default: return PAD_ST_CONNECTED;
    }
}

bool pad_store_request(int op, const char *slug) {
    if (s_st != ST_CONNECTED) return false;
    uint8_t b[64];
    int o = put_header(b, T_STORE);
    b[o++] = (uint8_t)op;
    memset(b + o, 0, 40);
    if (slug) memcpy(b + o, slug, strnlen(slug, 39));
    o += 40;
    net_send_to(&s_pc_addr, b, o);
    return true;
}

bool pad_store_result(int *op, int *status, char *slug, size_t len) {
    if (!s_store_res.have) return false;
    s_store_res.have = false;
    if (op) *op = s_store_res.op;
    if (status) *status = s_store_res.status;
    if (slug && len) snprintf(slug, len, "%s", s_store_res.slug);
    return true;
}
bool pad_connected(void) { return s_st == ST_CONNECTED; }
bool pad_take_rescan(void) { return xfer_take_rescan(); }

uint32_t pad_sig_top(void) {
    uint32_t xh = (uint32_t)xfer_percent() * 7u + (uint32_t)xfer_files_done() * 131u + (uint32_t)(xfer_name()[0] != 0);
    uint32_t h = (uint32_t)s_st * 7919u + (uint32_t)s_page * 31u + (uint32_t)s_sig_extra * 977u + (uint32_t)(s_exit_hold / 6) * 3u;
    h ^= (uint32_t)(s_rtt / 5) << 20;
    if (s_st == ST_WIFI || s_st == ST_SEARCH) h ^= (uint32_t)(s_frames / 4) * 2654435761u;   // spinner
    h ^= (uint32_t)s_rx * 40503u ^ s_my_ip ^ (s_my_mask & 1u);
    h += xh * 2246822519u;
    return h;
}

uint32_t pad_sig(void) {
    uint32_t h = (uint32_t)s_st * 7919u + (uint32_t)s_page * 31u + s_pressed * 131u + (uint32_t)(uint8_t)s_stick_x * 17u +
                 (uint32_t)(uint8_t)s_stick_y * 19u + (uint32_t)s_sig_extra * 977u + (uint32_t)(s_exit_hold / 6) * 3u;
    h ^= (uint32_t)(s_rtt / 5) << 20;
        for (int i = 0; i < s_pages[s_page].n; i++) if (s_pages[s_page].w[i].type == W_SLIDER) h = h * 33 + s_slider[s_pages[s_page].w[i].id];
    return h;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static Color widget_color(int idx) {
    static const Color pal[10] = { C8(0, 195, 227), C8(235, 70, 70), C8(255, 140, 30), C8(232, 182, 0), C8(40, 190, 100),
                                   C8(140, 90, 230), C8(240, 80, 160), C8(70, 100, 235), C8(96, 98, 108), C8(60, 62, 70) };
    return pal[idx < 10 ? idx : 8];
}

// filled circle of any size with soft edges (gfx_circle stops at radius 48)
static void disc(Canvas *c, int cx, int cy, int r, Color col) {
    if (r <= 40) { gfx_circle(c, cx, cy, r, col, 32); return; }
    int r2 = r * r * 64;                        // 1/8 pixel precision
    for (int dy = -r; dy < r; dy++) {
        int yy = dy * 8 + 4;
        int left = r2 - yy * yy;
        if (left <= 0) continue;
        int hw8 = 0;                            // half width in 1/8 pixels: integer sqrt
        while ((hw8 + 1) * (hw8 + 1) <= left) hw8 += (hw8 + 16) * (hw8 + 16) <= left ? 16 : 1;
        int full = hw8 / 8, frac = hw8 & 7;
        gfx_rect_a(c, cx - full, cy + dy, full * 2, 1, col, 32);
        if (frac) {
            gfx_pixel_a(c, cx - full - 1, cy + dy, col, frac * 4);
            gfx_pixel_a(c, cx + full, cy + dy, col, frac * 4);
        }
    }
}

static void draw_widget(Canvas *c, const Widget *w, bool down) {
    Color col = widget_color(w->color);
    Color face = down ? gfx_lighten(col, 8) : col;
    int r = w->shape == 1 ? (w->w < w->h ? w->w : w->h) / 2 : 8;
    switch (w->type) {
        case W_BUTTON:
            gfx_round_rect(c, w->x, w->y + (down ? 2 : 0), w->w, w->h - 2, r, face, 32);
            if (!down) gfx_round_rect(c, w->x, w->y + w->h - 4, w->w, 4, r < 4 ? r : 4, gfx_darken(col, 8), 32);
            gfx_text_align(c, FONT_BOLD, w->x, w->y + (w->h - 14) / 2 + (down ? 2 : 0), w->w, w->label, C8(255, 255, 255), ALIGN_CENTER);
            break;
        case W_STICK: {
            int cx = w->x + w->w / 2, cy = w->y + w->h / 2, rad = (w->w < w->h ? w->w : w->h) / 2;
            disc(c, cx, cy, rad, col);
            disc(c, cx, cy, rad - 3, T.panel);
            int kr = rad / 3;
            int kx = cx + (down ? s_stick_x * (rad - kr) / 127 : 0), ky = cy + (down ? s_stick_y * (rad - kr) / 127 : 0);
            disc(c, kx, ky, kr, face);
            if (w->label[0]) gfx_text_align(c, FONT_SMALL, kx - kr, ky - 6, kr * 2, w->label, C8(255, 255, 255), ALIGN_CENTER);
            break;
        }
        case W_TRACKPAD:
            gfx_round_rect(c, w->x, w->y, w->w, w->h, 10, T.panel, 32);
            gfx_round_ring(c, w->x, w->y, w->w, w->h, 10, 2, down ? col : T.divider, 32);
            gfx_icon_centered(c, ICON_GAMEPAD, w->x + w->w / 2, w->y + w->h / 2, T.text3);
            if (w->label[0]) gfx_text_align(c, FONT_SMALL, w->x, w->y + w->h - 16, w->w, w->label, T.text3, ALIGN_CENTER);
            break;
        case W_SLIDER: {
            gfx_round_rect(c, w->x, w->y, w->w, w->h, 6, T.panel, 32);
            int v = s_slider[w->id];
            if (w->w >= w->h) gfx_round_rect(c, w->x, w->y, 8 + (w->w - 8) * v / 255, w->h, 6, col, 32);
            else { int hh = 8 + (w->h - 8) * v / 255; gfx_round_rect(c, w->x, w->y + w->h - hh, w->w, hh, 6, col, 32); }
            if (w->label[0]) gfx_text_align(c, FONT_SMALL, w->x, w->y + w->h / 2 - 6, w->w, w->label, T.text, ALIGN_CENTER);
            break;
        }
    }
}

static void status_line(Canvas *c, int y, const char *text, Color col) {
    gfx_text_align(c, FONT_BODY, 10, y, 236, text, col, ALIGN_CENTER);
}

void pad_render_top(Canvas *c) {
    gfx_vgradient(c, 0, 0, 256, 192, gfx_mix(T.bg_top, T.accent, T.light ? 50 : 70), T.bg_top);
    gfx_text(c, FONT_BOLD, 10, 6, T_(S_PAD), T.text, 150);
    ui_battery(c, 228, 7);
    char line[96];
    if (s_st == ST_CONNECTED && s_info.have) {
        // panels: the computer's load, battery and music
        snprintf(line, sizeof(line), T_(S_PAD_CONNECTED), s_pc_name[0] ? s_pc_name : "PC");
        if (s_rtt >= 0) { size_t l = strlen(line); snprintf(line + l, sizeof(line) - l, "  ·  %d ms", s_rtt); }
        gfx_text_align(c, FONT_SMALL, 10, 24, 236, line, T.text2, ALIGN_CENTER);
        const char *lab[3] = { "CPU", "RAM", T_(S_PAD_PC_BATT) };
        uint8_t val[3] = { s_info.cpu, s_info.ram, s_info.batt };
        for (int i = 0; i < 3; i++) {
            int x = 10 + i * 80;
            gfx_round_rect(c, x, 42, 76, 52, 8, T.panel, 28);
            gfx_text(c, FONT_SMALL, x + 8, 46, lab[i], T.text2, 64);
            if (val[i] == 255) { gfx_text(c, FONT_TITLE, x + 8, 60, "-", T.text3, 60); continue; }
            snprintf(line, sizeof(line), "%u%%%s", (unsigned)val[i], (i == 2 && s_info.charging) ? " +" : "");
            gfx_text(c, FONT_TITLE, x + 8, 58, line, T.text, 64);
            int pct = val[i] > 100 ? 100 : val[i];
            Color bar = i == 2 ? (pct < 20 ? T.danger : T.ok) : (pct > 85 ? T.danger : T.accent);
            gfx_round_rect(c, x + 8, 82, 60, 5, 2, T.bg_top, 32);
            gfx_round_rect(c, x + 8, 82, 2 + 58 * pct / 100, 5, 2, bar, 32);
        }
        if (xfer_name()[0]) {
            snprintf(line, sizeof(line), T_(S_XFER_COPYING), xfer_name());
            gfx_text_align(c, FONT_SMALL, 10, 108, 236, line, T.text, ALIGN_CENTER);
            gfx_round_rect(c, 40, 124, 176, 6, 3, T.panel, 32);
            gfx_round_rect(c, 40, 124, 6 + 170 * xfer_percent() / 100, 6, 3, T.accent, 32);
        } else if (s_info.title[0]) {
            gfx_round_rect(c, 10, 100, 236, 44, 8, T.panel, 28);
            gfx_circle(c, 32, 122, 13, T.accent, 32);
            gfx_text_align(c, FONT_BOLD, 19, 114, 26, "♪", C8(255, 255, 255), ALIGN_CENTER);
            gfx_text(c, FONT_BOLD, 52, 106, s_info.title, T.text, 186);
            gfx_text(c, FONT_SMALL, 52, 124, s_info.artist, T.text2, 186);
        } else if (xfer_files_done()) {
            snprintf(line, sizeof(line), T_(S_XFER_DONE_N), xfer_files_done());
            status_line(c, 116, line, T.text2);
        }
        if (s_npages > 1) {
            snprintf(line, sizeof(line), "%s  ·  %d/%d", s_pages[s_page].name, s_page + 1, s_npages);
            status_line(c, 154, line, T.text2);
        }
    } else if (s_st == ST_CONNECTED) {
        gfx_circle(c, 128, 70, 30, T.accent, 32);
        gfx_icon_centered(c, ICON_GAMEPAD_L, 128, 70, C8(255, 255, 255));
        snprintf(line, sizeof(line), T_(S_PAD_CONNECTED), s_pc_name[0] ? s_pc_name : "PC");
        gfx_text_align(c, FONT_TITLE, 0, 110, 256, line, T.text, ALIGN_CENTER);
        if (s_rtt >= 0) { snprintf(line, sizeof(line), T_(S_PAD_MS), s_rtt); status_line(c, 132, line, T.text2); }
        if (xfer_name()[0]) {
            // a file is being copied over Wi-Fi
            snprintf(line, sizeof(line), T_(S_XFER_COPYING), xfer_name());
            gfx_text_align(c, FONT_SMALL, 10, 146, 236, line, T.text, ALIGN_CENTER);
            gfx_round_rect(c, 40, 162, 176, 6, 3, T.panel, 32);
            gfx_round_rect(c, 40, 162, 6 + 170 * xfer_percent() / 100, 6, 3, T.accent, 32);
        } else if (xfer_files_done()) {
            snprintf(line, sizeof(line), T_(S_XFER_DONE_N), xfer_files_done());
            status_line(c, 150, line, T.text2);
        } else if (s_npages > 1) {
            snprintf(line, sizeof(line), "%s  ·  %d/%d", s_pages[s_page].name, s_page + 1, s_npages);
            status_line(c, 150, line, T.text2);
        }
    } else {
        if (s_st == ST_WIFI_FAIL) {
            gfx_icon_centered(c, ICON_WARNING_L, 128, 50, T.danger);
            gfx_text_wrap(c, FONT_BODY, 14, 78, 228, 6, 15, T_(S_PAD_WIFI_FAIL), T.text);
        } else {
            ui_spinner(c, 128, 66, 14, s_frames, T.accent);
            gfx_text_align(c, FONT_BODY, 8, 96, 240, T_(s_st == ST_WIFI ? S_PAD_WIFI : S_PAD_SEARCH), T.text, ALIGN_CENTER);
        }
        if (s_st == ST_SEARCH) {
            // where this console is + whether anything arrives: helps when the two can't see each other
            const uint8_t *ip = (const uint8_t *)&s_my_ip;
            snprintf(line, sizeof(line), T_(S_PAD_NETINFO), ip[0], ip[1], ip[2], ip[3], s_rx);
            gfx_text_align(c, FONT_SMALL, 0, 112, 256, line, T.text3, ALIGN_CENTER);
            if (!s_pc_id) {
                status_line(c, 128, T_(S_PAD_CODE), T.text2);
                snprintf(line, sizeof(line), "%04u", (unsigned)s_code);
                gfx_text_align(c, FONT_HUGE, 0, 144, 256, line, T.accent, ALIGN_CENTER);
            } else {
                snprintf(line, sizeof(line), T_(S_PAD_KNOWN), s_pc_name);
                gfx_text_wrap(c, FONT_SMALL, 18, 132, 220, 3, 13, line, T.text2);
            }
        }
    }
    // leave hint / progress
    int y = 178;
    if (s_exit_hold > 0) gfx_round_rect(c, 10, y, 236 * s_exit_hold / 60, 4, 2, T.accent, 32);
    else gfx_text_align(c, FONT_SMALL, 0, y - 2, 256, T_(S_PAD_EXIT), T.text3, ALIGN_CENTER);
}

void pad_render_bottom(Canvas *c) {
    gfx_clear(c, T.bg);
    if (s_st != ST_CONNECTED) {
        gfx_icon_centered(c, ICON_GAMEPAD_L, 128, 70, T.text3);
        gfx_text_wrap(c, FONT_BODY, 20, 96, 216, 4, 15, T_(S_PAD_STUDIO_HINT), T.text2);
        return;
    }
    Page *p = &s_pages[s_page];
    for (int i = 0; i < p->n; i++) draw_widget(c, &p->w[i], (s_pressed >> p->w[i].id) & 1u);
    // page dots
    if (s_npages > 1) {
        gfx_rect(c, 0, 173, 256, 1, T.divider);
        for (int i = 0; i < s_npages; i++) gfx_circle(c, 128 - (s_npages - 1) * 6 + i * 12, 183, 3, i == s_page ? T.accent : T.text3, 32);
        gfx_text(c, FONT_BOLD, 8, 175, "<", T.text3, 20);
        gfx_text(c, FONT_BOLD, 240, 175, ">", T.text3, 20);
        gfx_text(c, FONT_SMALL, 24, 177, s_pages[s_page].name, T.text2, 80);
    }
}

#ifndef PAD_H
#define PAD_H
// "Kumanda": the DSi as a wireless controller for a computer running DSplay Studio.
//
// UDP protocol (little endian), header: "HPAD", version, type, u16 seq, u32 token
//   HELLO   DSi -> broadcast:39511   name[24], dsi id, id of the computer it was paired with (token 0)
//   PAIR    PC  -> DSi:39510         code, pc id, token, pc name[32]  (first time, code typed in Studio)
//   CONNECT PC  -> DSi:39510         pc id, token, pc name[32]        (known computer)
//   ACCEPT  DSi -> PC                pc id, dsi id
//   STATE   DSi -> PC (60 Hz)        buttons, touch, widget states, battery
//   LAYOUT  PC  -> DSi               touch screen layout (see pad.c)
//   PING    PC  -> DSi, PONG back    latency; the PC also tells the last round trip time
//   BYE     either way
#include "common.h"
#include "gfx.h"

#define PAD_PORT_DSI 39510
#define PAD_PORT_PC  39511

typedef struct {
    u32  down, held;      // DS keys (KEY_*)
    bool touching;
    int  tx, ty;
} PadInput;

void     pad_enter(const char *drive, const char *profile_name, int language);
// One frame of controller mode. Returns false when the user leaves (START+SELECT held).
bool     pad_frame(const PadInput *in);
void     pad_leave(void);
void     pad_render_top(Canvas *c);
void     pad_render_bottom(Canvas *c);
uint32_t pad_sig(void);           // changes whenever the lower screen needs to be redrawn
uint32_t pad_sig_top(void);
uint16_t pad_pair_code(void);     // shown on the upper screen until paired
bool     pad_connected(void);
bool     pad_take_rescan(void);   // games arrived over Wi-Fi: read the card again

// The store screen (source/store.c) uses the same connection to Studio, without controller input.
//   STORE     DSi -> PC   op (1 = send the catalog, 2 = install), slug[40]
//   STORE_RES PC  -> DSi  op, status (STORE_RES_*), slug[40]   (files arrive over the transfer port)
enum { PAD_ST_WIFI, PAD_ST_WIFI_FAIL, PAD_ST_UNPAIRED, PAD_ST_SEARCH, PAD_ST_CONNECTED };

// Settings > Wi-Fi (the radio is used by nothing else while that screen is open)
typedef struct { char ssid[33]; int rssi; bool secure; bool wep_only; } WifiNet;
bool wifi_scan_begin(const char *drive);           // false: the radio did not start
int  wifi_scan_poll(WifiNet *out, int max);        // once a frame: -1 scanning, else the count
void wifi_test_begin(void);                        // connects to the network in wifi.txt only
int  wifi_test_poll(void);                         // once a frame: 0 busy, 1 connected, -1 failed
void wifi_off(void);
bool wifi_saved(const char *drive, char *ssid, size_t len);
bool wifi_save(const char *drive, const char *ssid, const char *pass);
void wifi_forget(const char *drive);
void pad_wifi_retry(void);                         // after "Wi-Fi failed": try again
void     pad_enter_store(const char *drive, const char *profile_name, int language);
void     pad_set_store_mode(bool on);
int      pad_status(void);
bool     pad_store_request(int op, const char *slug);
bool     pad_store_result(int *op, int *status, char *slug, size_t len);

// "Nearby" (nearby.h) only needs Wi-Fi: no computer is looked for
void     pad_enter_nearby(const char *drive, const char *profile_name, int language);
bool     pad_net_info(uint32_t *ip, uint32_t *mask);   // Wi-Fi is up (addresses in network order)
uint32_t pad_console_id(void);

// Layout blob helpers (shared with the host tests)
int      pad_layout_parse(const uint8_t *data, int len);   // -> number of pages, 0 on error
void     pad_layout_default(void);
int      pad_layout_pages(void);
int      pad_layout_widgets(int page);

#endif // PAD_H

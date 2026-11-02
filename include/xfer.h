#ifndef XFER_H
#define XFER_H
// Wireless transfer: while the console is connected to DSplay Studio (PC mode), Studio
// can copy games and covers onto the SD card and read saves / photos from it over Wi-Fi.
//
// TCP, port 39512, little endian. The computer starts with "HXFR" + u32 token (the pairing
// token), then sends requests one after the other:
//   PUT  1: u16 len, path, u32 size, u8 overwrite  -> u8 status; if 0: <size bytes> -> u8 status
//   GET  2: u16 len, path, u32 offset, u32 length   -> u8 status, u32 n, <n bytes>  (length 0 = to the end)
//   LIST 3: u16 len, folder                         -> { u16 len, path, u32 size }..., u16 0
//   DONE 4:                                         -> u8 0   (the menu reads the card again)
// Paths are relative to the card root. Games may only be written under roms/, covers under
// _hyper/covers/, the store catalog under _hyper/store/; an existing save is never replaced.
#include "common.h"

#define XFER_PORT 39512

enum { XFER_OK = 0, XFER_SAME = 1, XFER_EXISTS = 2, XFER_DENIED = 3, XFER_NOSPACE = 4, XFER_IOERR = 5 };

void xfer_start(const char *drive, uint32_t token);   // listen (after pairing)
void xfer_stop(void);
void xfer_poll(void);                                 // once per frame; does a bounded amount of work
bool xfer_busy(void);                                 // a computer is connected
const char *xfer_name(void);                          // file being copied ("" = none)
int  xfer_percent(void);
int  xfer_files_done(void);
bool xfer_take_rescan(void);                          // true once after DONE / new files
bool xfer_path_ok(const char *rel, bool for_write);   // (host tests)

#endif // XFER_H

#!/usr/bin/env python3
"""DSplay Studio - prepares an SD card for DSplay on a Nintendo DSi.

    python3 launcher.py            opens the Studio window
    python3 launcher.py --no-ui    only starts the local server (prints the address)
"""

import os
import sys
import traceback

if sys.version_info < (3, 8):
    sys.exit("DSplay Studio needs Python 3.8 or newer.")

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def _report_crash():
    text = traceback.format_exc()
    print(text, file=sys.stderr)
    try:
        with open(os.path.join(HERE, "studio_hata.txt"), "w", encoding="utf-8") as f:
            f.write(f"Python {sys.version}\n{sys.platform}\n\n{text}")
    except OSError:
        pass


if __name__ == "__main__":
    try:
        from studio.server import main
        main(open_ui="--no-ui" not in sys.argv)
    except KeyboardInterrupt:
        pass
    except Exception:  # noqa: BLE001 - show the reason instead of a closing window
        _report_crash()
        sys.exit(1)

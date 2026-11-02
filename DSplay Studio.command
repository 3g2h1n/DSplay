#!/bin/bash
# DSplay Studio (macOS) - double-click to start
cd "$(dirname "$0")"
if ! command -v python3 >/dev/null 2>&1; then
  osascript -e 'display alert "Python 3 gerekli" message "python.org adresinden Python 3 kurun, sonra tekrar deneyin."'
  exit 1
fi
exec python3 launcher.py

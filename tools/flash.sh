#!/usr/bin/env bash
# Flash a Ghost board over USB from your own computer (no browser needed).
# Usage: tools/flash.sh sender|listener [port]
#   port examples: COM3 (Windows, Git Bash), /dev/ttyUSB0 (Linux), /dev/cu.usbserial-0001 (Mac)
# Needs Python 3: pip install esptool
set -euo pipefail
cd "$(dirname "$0")/.."
role="${1:-}"
case "$role" in sender|listener) ;; *) echo "usage: tools/flash.sh sender|listener [port]"; exit 2;; esac
bin="web/firmware/$role/ghost-$role.bin"
port_args=()
[ -n "${2:-}" ] && port_args=(--port "$2")
python3 -m esptool --chip esp32 "${port_args[@]}" erase_flash
python3 -m esptool --chip esp32 "${port_args[@]}" --baud 460800 write_flash 0x0 "$bin"
echo "Done. Unplug the board and label it ${role^^}."

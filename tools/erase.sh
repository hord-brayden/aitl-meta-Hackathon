#!/usr/bin/env bash
# Wipe the whole flash (settings, pairing, cert). The device will run the setup wizard on next boot.
set -euo pipefail
PORT="${1:-$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -1)}"
esptool.py --port "$PORT" --baud 460800 erase_flash

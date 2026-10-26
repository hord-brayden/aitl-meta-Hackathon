#!/usr/bin/env bash
# Raw serial monitor (device logs start with '#', protocol frames are JSON). Stop `aitl serve` first.
PORT="${1:-$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -1)}"
cd "$(dirname "$0")/../firmware" && pio device monitor -p "$PORT" -b 115200

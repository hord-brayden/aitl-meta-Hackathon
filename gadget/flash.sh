#!/usr/bin/env bash
# Flash the gadget build onto the CYD. This REPLACES the Musecorder firmware (different partition layout);
# `tools/flash.sh cyd24c` (or hosyond40r) puts Musecorder back. Stop `musecorder serve` first: it holds the port.
set -euo pipefail
BOARD="${BOARD:-cyd24}"   # cyd24 = 2.4" ILI9341 board, cyd40 = Hosyond 4" ST7796 board
cd "$(dirname "$0")/.."
SDK="${MUSE_GADGET_SDK_DIR:-$HOME/esp/muse-gadget-sdk}/esp32"; PORT="${1:-$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no CYD on USB"; exit 1; }
IDF="${IDF_PATH:-$HOME/esp/esp-idf-v6}"; . "$IDF/export.sh" >/dev/null
cd "$SDK"
idf.py -B build-$BOARD -DIDF_TARGET=esp32 -DSDKCONFIG=build-$BOARD/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.$BOARD" -p "$PORT" flash 2>&1 | tail -8
echo; echo "done. Watch it boot:  cd $SDK && . ~/esp/esp-idf-v6/export.sh && idf.py -B build-$BOARD -p $PORT monitor   (Ctrl-] quits)"
echo "Then in the Muse app: Settings > Devices > Developer mode ON > Add Device > MuseGadget-CYD-XXXXXX (or -CYD4- on the 4\"). Press BOOT when the bars breathe blue."

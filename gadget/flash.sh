#!/usr/bin/env bash
# Flash the gadget build onto the CYD. This REPLACES the Musecorder firmware (different partition layout);
# `tools/flash.sh cyd24c` (or hosyond40r) puts Musecorder back. Stop `musecorder serve` first: it holds the port.
set -euo pipefail
BOARD="${BOARD:-cyd24}"   # cyd24 = 2.4" ILI9341 board, cyd40 = Hosyond 4" ST7796 board
TARGET=esp32; case "$BOARD" in nano|4827) TARGET=esp32s3;; esac   # Nano ESP32 and ESP32-4827S043 are ESP32-S3
cd "$(dirname "$0")/.."
REPO="$PWD"
SDK="${MUSE_GADGET_SDK_DIR:-$HOME/esp/muse-gadget-sdk}/esp32"
# The Nano ESP32 enumerates over native USB (/dev/cu.usbmodem*); the CYD/4827 use a USB-serial bridge (usbserial/wchusbserial).
case "$BOARD" in
  nano) PORT="${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}";;
  *)    PORT="${1:-$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -1)}";;
esac
[ -n "$PORT" ] || { echo "no board on USB for BOARD=$BOARD"; exit 1; }
IDF="${IDF_PATH:-$HOME/esp/esp-idf-v6}"; . "$IDF/export.sh" >/dev/null
# Match build.sh's defaults (token file included) so idf.py reuses the existing build without reconfiguring the token away.
DEFAULTS="sdkconfig.defaults;devices/sdkconfig.$BOARD"
[ -f "$SDK/devices/sdkconfig.$BOARD-token" ] && DEFAULTS="$DEFAULTS;devices/sdkconfig.$BOARD-token"
cd "$SDK"
idf.py -B build-$BOARD -DIDF_TARGET=$TARGET -DSDKCONFIG=build-$BOARD/sdkconfig -DSDKCONFIG_DEFAULTS="$DEFAULTS" -p "$PORT" flash 2>&1 | tail -8
echo; echo "done. Watch it boot:  cd $SDK && . ~/esp/esp-idf-v6/export.sh && idf.py -B build-$BOARD -p $PORT monitor   (Ctrl-] quits)"
echo "Then in the Muse app: Settings > Devices > Developer mode ON > Add Device > pick the MuseGadget-* entry. Confirm pairing when the status shows it."

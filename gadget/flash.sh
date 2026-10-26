#!/usr/bin/env bash
# Flash the gadget build onto the CYD. This REPLACES the Musecorder firmware (different partition layout);
# `firmware: tools/flash.sh cyd24c` puts Musecorder back. Stop `musecorder serve` first: it holds the port.
set -euo pipefail
cd "$(dirname "$0")/.."
SDK="$PWD/gadget/sdk/esp32"; PORT="${1:-$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no CYD on USB"; exit 1; }
IDF="${IDF_PATH:-$HOME/esp/esp-idf-v6}"; . "$IDF/export.sh" >/dev/null
cd "$SDK"
idf.py -B build-cyd24 -DIDF_TARGET=esp32 -DSDKCONFIG=build-cyd24/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.cyd24" -p "$PORT" flash 2>&1 | tail -8
echo; echo "done. Watch it boot:  cd gadget/sdk/esp32 && . ~/esp/esp-idf-v6/export.sh && idf.py -B build-cyd24 -p $PORT monitor   (Ctrl-] quits)"
echo "Then in the Muse app: Settings > Devices > Developer mode ON > Add Device > MuseGadget-CYD-XXXXXX. Press BOOT when the bars breathe blue."

#!/usr/bin/env bash
# Build Meta's Muse gadget firmware (facebookincubator/muse-gadget-sdk) for the Musecorder's CYD 2.4" board.
#   gadget/build.sh            # clone SDK (once), apply the port, build -> gadget/sdk/esp32/build-cyd24/muse-gadget.bin
#   gadget/flash.sh [PORT]     # flash it (replaces the Musecorder firmware; tools/flash.sh cyd24c brings it back)
# Needs ESP-IDF v6.0.1 (~/esp/esp-idf-v6, or IDF_PATH) and an SDK token in .env as MUSE_GADGET_SDK_TOKEN
# (gadgets.muse.ai > Account > SDK tokens). Without a token the build warns; you can send it later over serial.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$PWD"; SDK="$REPO/gadget/sdk"
[ -d "$SDK/esp32" ] || git clone --depth 1 https://github.com/facebookincubator/muse-gadget-sdk.git "$SDK"
python3 "$REPO/gadget/port/apply.py" "$SDK/esp32"
IDF="${IDF_PATH:-$HOME/esp/esp-idf-v6}"
[ -f "$IDF/export.sh" ] || { echo "ESP-IDF v6.0.1 not found at $IDF (see gadget/README.md)"; exit 1; }
# shellcheck disable=SC1091
. "$IDF/export.sh" >/dev/null
DEFAULTS="sdkconfig.defaults;devices/sdkconfig.cyd24"
TOKEN="$(grep -E '^MUSE_GADGET_SDK_TOKEN=' "$REPO/.env" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '"'"'" || true)"
if [ -n "$TOKEN" ]; then
  printf 'CONFIG_GADGET_SDK_TOKEN="%s"\n' "$TOKEN" > "$SDK/esp32/devices/sdkconfig.cyd24-token"   # inside the gitignored SDK clone
  DEFAULTS="$DEFAULTS;devices/sdkconfig.cyd24-token"; echo "SDK token: from .env"
else
  echo "SDK token: none in .env (MUSE_GADGET_SDK_TOKEN=...); the gadget will not pair until one is set (serial: >sdk_token=mgst_...)"
fi
cd "$SDK/esp32"
idf.py -B build-cyd24 -DIDF_TARGET=esp32 -DSDKCONFIG=build-cyd24/sdkconfig -DSDKCONFIG_DEFAULTS="$DEFAULTS" build 2>&1 | tail -25
echo; echo "built: $SDK/esp32/build-cyd24/muse-gadget.bin"; echo "flash: gadget/flash.sh /dev/cu.usbserial-110"

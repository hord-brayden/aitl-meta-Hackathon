#!/usr/bin/env bash
# Build Meta's Muse gadget firmware (facebookincubator/muse-gadget-sdk) for the Musecorder's CYD 2.4" board.
#   gadget/build.sh            # clone SDK (once), apply the port, build -> ~/esp/muse-gadget-sdk/esp32/build-$BOARD/muse-gadget.bin
#   gadget/flash.sh [PORT]     # flash it (replaces the Musecorder firmware; tools/flash.sh cyd24c brings it back)
# Needs ESP-IDF v6.0.1 (~/esp/esp-idf-v6, or IDF_PATH) and an SDK token in .env as MUSE_GADGET_SDK_TOKEN
# (gadgets.muse.ai > Account > SDK tokens). Without a token the build warns; you can send it later over serial.
set -euo pipefail
BOARD="${BOARD:-cyd24}"   # cyd24 = 2.4" ILI9341 board, cyd40 = Hosyond 4" ST7796 board
TARGET=esp32; [ "$BOARD" = nano ] && TARGET=esp32s3   # Arduino Nano ESP32 (u-blox NORA-W106) is an ESP32-S3
cd "$(dirname "$0")/.."
REPO="$PWD"
# The SDK clone lives OUTSIDE the repo: ESP-IDF's response files break on paths with spaces (this repo sits in iCloud),
# which silently drops the picolibc specs and fails with "unknown type name __FILE". Override with MUSE_GADGET_SDK_DIR.
SDK="${MUSE_GADGET_SDK_DIR:-$HOME/esp/muse-gadget-sdk}"
case "$SDK" in *" "*) echo "SDK dir must not contain spaces: $SDK"; exit 1;; esac
[ -d "$SDK/esp32" ] || git clone --depth 1 https://github.com/facebookincubator/muse-gadget-sdk.git "$SDK"
python3 "$REPO/gadget/port/apply.py" "$SDK/esp32"
IDF="${IDF_PATH:-$HOME/esp/esp-idf-v6}"
[ -f "$IDF/export.sh" ] || { echo "ESP-IDF v6.0.1 not found at $IDF (see gadget/README.md)"; exit 1; }
# shellcheck disable=SC1091
. "$IDF/export.sh" >/dev/null
DEFAULTS="sdkconfig.defaults;devices/sdkconfig.$BOARD"
TOKEN="$(grep -E '^MUSE_GADGET_SDK_TOKEN=' "$REPO/.env" 2>/dev/null | head -1 | cut -d= -f2- | tr -d '"'"'" || true)"
if [ -n "$TOKEN" ]; then
  printf 'CONFIG_GADGET_SDK_TOKEN="%s"\n' "$TOKEN" > "$SDK/esp32/devices/sdkconfig.$BOARD-token"   # inside the gitignored SDK clone
  DEFAULTS="$DEFAULTS;devices/sdkconfig.$BOARD-token"; echo "SDK token: from .env"
else
  echo "SDK token: none in .env (MUSE_GADGET_SDK_TOKEN=...); the gadget will not pair until one is set (serial: >sdk_token=mgst_...)"
fi
cd "$SDK/esp32"
# idf.py only applies SDKCONFIG_DEFAULTS when build-$BOARD/sdkconfig is absent; remove it so edits to
# the sdkconfig.$BOARD profile (e.g. PSRAM mode) always take effect on a rebuild.
rm -f "build-$BOARD/sdkconfig"
idf.py -B build-$BOARD -DIDF_TARGET=$TARGET -DSDKCONFIG=build-$BOARD/sdkconfig -DSDKCONFIG_DEFAULTS="$DEFAULTS" build 2>&1 | tail -25
echo; echo "built: $SDK/esp32/build-$BOARD/muse-gadget.bin"; echo "flash: BOARD=$BOARD gadget/flash.sh"

#!/usr/bin/env bash
# Build + flash AITL firmware. Usage: tools/flash.sh [env] [port]   (env: aitl | aitl_ili9341)
set -euo pipefail
cd "$(dirname "$0")/../firmware"
ENV="${1:-aitl}"; PORT="${2:-$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no CYD on USB"; exit 1; }
echo "flashing env=$ENV port=$PORT"
pio run -e "$ENV" -t upload --upload-port "$PORT"

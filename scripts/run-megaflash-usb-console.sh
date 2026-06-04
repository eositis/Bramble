#!/usr/bin/env bash
# Run MegaFlash in Bramble for USB CDC UserTerminal testing.
# Do NOT use megaflash-bus.stub here — a2phi / Apple-bus events make
# IsAppleConnected() true and firmware skips the USB diagnostic menu.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BRAMBLE="${BRAMBLE:-$ROOT/bramble}"
UF2="${MEGAFLASH_UF2:-$ROOT/../MegaFlash/pico/pico2_debug/megaflash.uf2}"
PORT="${USB_CONSOLE_PORT:-5555}"

if [[ ! -x "$BRAMBLE" ]]; then
  BRAMBLE="$ROOT/build/bramble"
fi
if [[ ! -f "$UF2" ]]; then
  echo "UF2 not found: $UF2" >&2
  echo "Set MEGAFLASH_UF2 to your megaflash.uf2 path." >&2
  exit 1
fi

exec "$BRAMBLE" "$UF2" \
  -arch m33 \
  -clock 150 \
  -cores 2 \
  -usb-console "$PORT" \
  -usb-stdio \
  -timeout "${TIMEOUT:-120}" \
  "$@"

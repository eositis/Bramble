#!/usr/bin/env bash
# Run MegaFlash UF2 in Bramble with Apple-bus GPIO stub script.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BRAMBLE="${BRAMBLE:-$ROOT/bramble}"
UF2="${MEGAFLASH_UF2:-$ROOT/../MegaFlash/pico/pico2_debug/megaflash.uf2}"
STUB="$ROOT/scripts/megaflash-bus.stub"

if [[ ! -x "$BRAMBLE" ]]; then
  BRAMBLE="$ROOT/build/bramble"
fi
if [[ ! -f "$UF2" ]]; then
  echo "UF2 not found: $UF2" >&2
  echo "Set MEGAFLASH_UF2 to your megaflash.uf2 path." >&2
  exit 1
fi

EXTRA=()
if [[ -n "${UART_CONSOLE_PORT:-}" ]]; then
  EXTRA+=(-uart-console "$UART_CONSOLE_PORT")
fi
if [[ -n "${USB_CONSOLE_PORT:-}" ]]; then
  EXTRA+=(-usb-console "$USB_CONSOLE_PORT" -usb-stdio)
fi

exec "$BRAMBLE" "$UF2" \
  -arch m33 \
  -clock 150 \
  -cores 2 \
  -trace-mc \
  -script "$STUB" \
  -timeout "${TIMEOUT:-15}" \
  "${EXTRA[@]}" \
  "$@"


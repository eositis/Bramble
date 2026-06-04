#!/usr/bin/env bash
# Run MegaFlash UF2 in Bramble with Apple-bus GPIO stub script.
#
# WARNING: Do not use this script for USB UserTerminal / -usb-console testing.
# The stub's a2phi makes IsAppleConnected() true → firmware takes core0Loop
# (Apple bus) and skips UserTerminal. Use scripts/run-megaflash-usb-console.sh
# instead (no -script).
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
  echo "run-megaflash-stub.sh: USB_CONSOLE_PORT is set but this script uses the Apple bus stub." >&2
  echo "  Apple online suppresses the USB UserTerminal menu. Use instead:" >&2
  echo "  USB_CONSOLE_PORT=$USB_CONSOLE_PORT ./scripts/run-megaflash-usb-console.sh" >&2
  exit 1
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


#!/usr/bin/env bash
# Run MegaFlash in Bramble with UART0 bridged to TCP (stdio_uart_init path).
# Do NOT use megaflash-bus.stub — Apple-bus stub skips the diagnostic menu path.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BRAMBLE="${BRAMBLE:-$ROOT/bramble}"
UF2="${MEGAFLASH_UF2:-$ROOT/../MegaFlash/pico/pico2_debug/megaflash.uf2}"
PORT="${UART_CONSOLE_PORT:-4444}"

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
  -uart-console "$PORT" \
  -uart-console-mirror \
  -timeout "${TIMEOUT:-60}" \
  "$@"

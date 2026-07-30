#!/usr/bin/env bash
# Run MegaFlash in Bramble with UART0 bridged to TCP (stdio_uart_init path).
# Do NOT use megaflash-bus.stub — Apple-bus stub skips the diagnostic menu path.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MFVM="${MEGAFLASH_VM_ROOT:-$ROOT/../megaflash-vm}"
BRAMBLE="${BRAMBLE:-$ROOT/bramble}"
UF2="${MEGAFLASH_UF2:-$MFVM/firmware/megaflash.uf2}"
PORT="${UART_CONSOLE_PORT:-4444}"

if [[ ! -x "$BRAMBLE" ]]; then
  BRAMBLE="$ROOT/build/bramble"
fi
if [[ ! -f "$UF2" ]]; then
  echo "UF2 not found: $UF2" >&2
  echo "Set MEGAFLASH_UF2, or sync: ../megaflash-vm/scripts/sync-firmware-from-megaflash.sh" >&2
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

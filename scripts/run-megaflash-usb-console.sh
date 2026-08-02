#!/usr/bin/env bash
# Moved to megaflash-vm — thin forwarder for old paths / muscle memory.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MFVM="${MEGAFLASH_VM_ROOT:-$ROOT/../megaflash-vm}"
TARGET="$MFVM/scripts/run-megaflash-usb-console.sh"
if [[ ! -x "$TARGET" ]]; then
  echo "USB console runner moved to megaflash-vm." >&2
  echo "  Expected: $TARGET" >&2
  echo "  See: $MFVM/docs/USB-CONSOLE.md" >&2
  exit 1
fi
exec "$TARGET" "$@"

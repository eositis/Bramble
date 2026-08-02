#!/usr/bin/env bash
# Moved to megaflash-vm — thin forwarder for old paths / muscle memory.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MFVM="${MEGAFLASH_VM_ROOT:-$ROOT/../megaflash-vm}"
TARGET="$MFVM/scripts/test-32mb-xmodem.sh"
if [[ ! -x "$TARGET" ]]; then
  echo "test-32mb-xmodem.sh moved to megaflash-vm: $TARGET" >&2
  exit 1
fi
exec "$TARGET" "$@"

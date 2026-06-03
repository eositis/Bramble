#!/usr/bin/env bash
# Attach to Bramble USB CDC TCP console (-usb-console).
set -euo pipefail

HOST="${USB_CONSOLE_HOST:-127.0.0.1}"
PORT="${1:-${USB_CONSOLE_PORT:-5555}}"

if command -v nc >/dev/null 2>&1; then
  exec nc "$HOST" "$PORT"
fi
if command -v ncat >/dev/null 2>&1; then
  exec ncat "$HOST" "$PORT"
fi
if command -v socat >/dev/null 2>&1; then
  exec socat -,raw,echo=0 "TCP:${HOST}:${PORT}"
fi

echo "Need nc, ncat, or socat to connect to ${HOST}:${PORT}" >&2
exit 1

#!/usr/bin/env bash
# Attach a terminal to Bramble's UART0 TCP console (-uart-console / -net-uart0).
set -euo pipefail

HOST="${UART_CONSOLE_HOST:-127.0.0.1}"
PORT="${1:-${UART_CONSOLE_PORT:-4444}}"

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

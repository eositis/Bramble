#!/usr/bin/env bash
# Run MegaFlash in Bramble for USB CDC UserTerminal testing.
# Do NOT use megaflash-bus.stub here — a2phi / Apple-bus events make
# IsAppleConnected() true and firmware skips the USB diagnostic menu.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BRAMBLE="${BRAMBLE:-$ROOT/bramble}"
UF2="${MEGAFLASH_UF2:-$ROOT/../MegaFlash/pico/pico2_debug/megaflash.uf2}"
PORT="${USB_CONSOLE_PORT:-5555}"
PTY_PATH="${USB_CONSOLE_PTY_PATH:-/tmp/bramble-usb-console}"

if [[ ! -x "$BRAMBLE" ]]; then
  BRAMBLE="$ROOT/build/bramble"
fi
if [[ ! -f "$UF2" ]]; then
  echo "UF2 not found: $UF2" >&2
  echo "Set MEGAFLASH_UF2 to your megaflash.uf2 path." >&2
  exit 1
fi

# macOS: default to virtual serial (PTY) so screen/cu/Serial.app can attach.
# Set USB_CONSOLE_TCP=1 to keep the legacy TCP socket mode instead.
USE_PTY=0
if [[ "${USB_CONSOLE_TCP:-0}" == 1 ]]; then
  USE_PTY=0
elif [[ "${USB_CONSOLE_PTY:-0}" == 1 ]]; then
  USE_PTY=1
elif [[ "$(uname -s)" == Darwin ]]; then
  USE_PTY=1
fi

USB_CONSOLE_ARG=(-usb-console "$PORT")
if [[ "$USE_PTY" == 1 ]]; then
  USB_CONSOLE_ARG=(-usb-console "pty:${PTY_PATH}")
fi

SPI_FLASH_ARGS=()
if [[ -n "${SPI_FLASH1:-}" ]]; then
  SPI_FLASH_ARGS+=(-spi-flash1 "$SPI_FLASH1")
else
  SPI_FLASH_ARGS+=(-spi-flash1)
fi
if [[ -n "${SPI_FLASH1_SIZE:-}" ]]; then
  SPI_FLASH_ARGS+=(-spi-flash1-size "$SPI_FLASH1_SIZE")
fi
if [[ -n "${SPI_FLASH2:-}" ]]; then
  SPI_FLASH_ARGS+=(-spi-flash2 "$SPI_FLASH2")
else
  SPI_FLASH_ARGS+=(-spi-flash2)
fi
if [[ -n "${SPI_FLASH2_SIZE:-}" ]]; then
  SPI_FLASH_ARGS+=(-spi-flash2-size "$SPI_FLASH2_SIZE")
fi

exec "$BRAMBLE" "$UF2" \
  -arch m33 \
  -clock 150 \
  -cores "${CORES:-1}" \
  "${USB_CONSOLE_ARG[@]}" \
  -usb-stdio \
  "${SPI_FLASH_ARGS[@]}" \
  -timeout "${TIMEOUT:-7200}" \
  "$@"

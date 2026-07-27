#!/usr/bin/env bash
# Start Bramble (MegaFlash) + MAME Apple //c (rev 4) with MegaFlash ROM and
# $C0C0-$C0C3 TCP bridge. Keep default 128K RAM (do not pass -ramsize).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BRAMBLE="${BRAMBLE:-$ROOT/bramble}"
UF2="${MEGAFLASH_UF2:-$ROOT/../MegaFlash/pico/pico2_debug/megaflash.uf2}"
ELF="${MEGAFLASH_ELF:-$ROOT/../MegaFlash/pico/pico2_debug/megaflash.elf}"
IIC_BIN="${IIC_BIN:-$ROOT/iic.bin}"
STUB="$ROOT/scripts/megaflash-mame.stub"
PORT="${BRAMBLE_A2BUS_PORT:-19765}"
ROMPATH="${MAME_ROMPATH:-$ROOT/roms}"
PLUGINPATH="$ROOT/scripts/mame_plugins"
MAME_BIN="${MAME:-mame}"
STAGE_DIR="$ROMPATH/apple2c4"

if [[ ! -x "$BRAMBLE" ]]; then
  BRAMBLE="$ROOT/build/bramble"
fi
if [[ ! -x "$BRAMBLE" ]]; then
  echo "bramble not found; build with: cmake -B build && make -C build bramble" >&2
  exit 1
fi
if [[ ! -f "$UF2" ]]; then
  echo "UF2 not found: $UF2 (set MEGAFLASH_UF2)" >&2
  exit 1
fi
if [[ ! -f "$IIC_BIN" ]]; then
  echo "MegaFlash IIc ROM not found: $IIC_BIN (set IIC_BIN)" >&2
  exit 1
fi
if ! command -v "$MAME_BIN" >/dev/null 2>&1; then
  echo "mame not found in PATH (brew install mame, or set MAME=...)" >&2
  exit 1
fi

mkdir -p "$STAGE_DIR"
# MAME apple2c4 maincpu dump name; CRC mismatch is OK at runtime.
cp -f "$IIC_BIN" "$STAGE_DIR/3410445b.256"

REGS_ARGS=()
if [[ -f "$ELF" ]]; then
  REGS_ARGS+=(-symbols "$ELF")
fi

BRAMBLE_PID=""
cleanup() {
  if [[ -n "$BRAMBLE_PID" ]] && kill -0 "$BRAMBLE_PID" 2>/dev/null; then
    kill "$BRAMBLE_PID" 2>/dev/null || true
    wait "$BRAMBLE_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

echo "[mame] starting Bramble MegaFlash bridge on 127.0.0.1:$PORT"
"$BRAMBLE" "$UF2" \
  -arch m33 \
  -clock 150 \
  -cores 2 \
  -a2bus-bridge "$PORT" \
  -script "$STUB" \
  ${TIMEOUT:+-timeout "$TIMEOUT"} \
  "${REGS_ARGS[@]}" \
  "$@" &
BRAMBLE_PID=$!

# Wait until TCP accepts PING, then until Slinky register shadow is live (r[2]=0xf0).
python3 - "$PORT" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
deadline = time.time() + 90
last_err = None

def rpc(op, *payload):
    s = socket.create_connection(("127.0.0.1", port), timeout=2)
    s.sendall(bytes((op,) + payload))
    rsp = s.recv(2)
    s.close()
    if len(rsp) != 2 or rsp[0] != 0:
        raise OSError(f"bad rsp {rsp!r}")
    return rsp[1]

while time.time() < deadline:
    try:
        pong = rpc(0x00)
        print(f"[mame] bridge PING ok (data=0x{pong:02x})")
        break
    except OSError as e:
        last_err = e
        time.sleep(0.2)
else:
    print(f"[mame] bridge not ready: {last_err}", file=sys.stderr)
    sys.exit(1)

while time.time() < deadline:
    try:
        r2 = rpc(0x04, 2)  # PEEK PARAM/high addr nibble in Slinky shadow
        if r2 == 0xF0:
            print("[mame] MegaFlash BusLoopSlinky ready (registers[2]=0xf0)")
            sys.exit(0)
    except OSError as e:
        last_err = e
    time.sleep(0.3)
print(f"[mame] firmware bus loop not ready: {last_err}", file=sys.stderr)
sys.exit(1)
PY

echo "[mame] launching apple2c4 (default 128K; MegaFlash iic.bin as maincpu)"
exec "$MAME_BIN" apple2c4 \
  -rompath "$ROMPATH" \
  -pluginspath "$PLUGINPATH" \
  -plugin megaflash_bridge \
  -skip_gameinfo \
  -window

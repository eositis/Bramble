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
PLUGINPATH="$ROOT/scripts/mame_plugins"
MAME_BIN="${MAME:-mame}"

# Prefer Ample's Apple II romset (CHR/keyboard/speech + stock maincpu) when present.
AMPLE_ROMS="${AMPLE_ROMS:-$HOME/Library/Application Support/Ample/roms}"
LOCAL_ROMS="${MAME_LOCAL_ROMS:-$ROOT/roms}"
# MAME rompath list separator: ';' on Windows/macOS, ':' on Linux.
case "$(uname -s)" in
  Darwin|MINGW*|MSYS*|CYGWIN*) ROMPATH_SEP=';' ;;
  *) ROMPATH_SEP=':' ;;
esac
if [[ -n "${MAME_ROMPATH:-}" ]]; then
  ROMPATH="$MAME_ROMPATH"
else
  # Ample first for stock dumps; local roms/ can still supply overrides.
  if [[ -d "$AMPLE_ROMS" ]]; then
    ROMPATH="${AMPLE_ROMS}${ROMPATH_SEP}${LOCAL_ROMS}"
  else
    ROMPATH="$LOCAL_ROMS"
  fi
fi

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

# Preflight: stock apple2c4 dumps must be findable (do NOT replace maincpu with
# iic.bin on disk — wrong CRC aborts some builds; Lua overlays iic.bin after load).
if ! "$MAME_BIN" apple2c4 -rompath "$ROMPATH" -verifyroms >/dev/null 2>&1; then
  echo "[mame] apple2c4 romset incomplete for -rompath:" >&2
  echo "  $ROMPATH" >&2
  echo "Need at least:" >&2
  echo "  341-0265-a.chr, 342-0132-c.e12, 3410445b.256, sc01a.bin (votrsc01a)" >&2
  echo "Tip: Ample's roms dir usually has apple2c.zip + votrsc01a.zip:" >&2
  echo "  export MAME_ROMPATH=\"\$HOME/Library/Application Support/Ample/roms\"" >&2
  "$MAME_BIN" apple2c4 -rompath "$ROMPATH" -verifyroms 2>&1 | head -20 >&2 || true
  exit 1
fi

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
# Keep one connection to avoid spam/reconnects during polling.
python3 - "$PORT" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
deadline = time.time() + 90
last_err = None
sock = None

def connect():
    global sock, last_err
    if sock is not None:
        return sock
    try:
        sock = socket.create_connection(("127.0.0.1", port), timeout=2)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return sock
    except OSError as e:
        last_err = e
        sock = None
        return None

def rpc(op, *payload):
    global sock, last_err
    s = connect()
    if s is None:
        raise OSError(last_err or "connect failed")
    try:
        s.sendall(bytes((op,) + payload))
        rsp = s.recv(2)
    except OSError as e:
        last_err = e
        try:
            s.close()
        except OSError:
            pass
        sock = None
        raise
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
        r2 = rpc(0x04, 2)
        if r2 == 0xF0:
            print("[mame] MegaFlash BusLoopSlinky ready (registers[2]=0xf0)")
            if sock is not None:
                sock.close()
            sys.exit(0)
    except OSError as e:
        last_err = e
    time.sleep(0.3)
print(f"[mame] firmware bus loop not ready: {last_err}", file=sys.stderr)
sys.exit(1)
PY

export BRAMBLE_A2BUS_PORT="$PORT"
export BRAMBLE_IIC_BIN="$IIC_BIN"

echo "[mame] launching apple2c4 (rompath=$ROMPATH; overlay $IIC_BIN via plugin)"
exec "$MAME_BIN" apple2c4 \
  -rompath "$ROMPATH" \
  -pluginspath "$PLUGINPATH" \
  -plugin megaflash_bridge \
  -skip_gameinfo \
  -window

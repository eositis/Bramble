# UART console over TCP

Bramble already bridges the emulated PL011 UART to a **bidirectional TCP socket**. Guest TX bytes go to the client; client bytes are pushed into the UART RX FIFO.

## Quick start (macOS / Linux)

**Terminal 1 — run the emulator:**

```bash
./build/bramble megaflash.uf2 -arch m33 -clock 150 -uart-console 4444
```

Wait for:

```text
[Net] UART0 listening on port 4444
```

**Terminal 2 — attach a console:**

```bash
nc localhost 4444
# or: ./scripts/connect-uart-console.sh 4444
```

Type in terminal 2; input reaches guest UART0 RX. Firmware `printf` / `stdio_uart` output appears in terminal 2.

Do **not** use `-stdin` on Bramble when using `nc` for input (only one source should feed RX).

### Optional: mirror TX on the Bramble terminal

```bash
./build/bramble firmware.uf2 -uart-console 4444 -uart-console-mirror
```

Guest UART output is sent to the TCP client **and** copied to Bramble’s stderr (handy for quick logs without `nc`).

## CLI flags

| Flag | Meaning |
|------|---------|
| `-uart-console <port>` | TCP server on UART0 (preferred alias) |
| `-uart-console1 <port>` | TCP server on UART1 |
| `-uart-console-mirror` | Duplicate guest UART0 TX to host stderr |
| `-net-uart0 <port>` | Same as `-uart-console` |
| `-net-uart0-connect host:port` | TCP client (Bramble connects outward) |

Implementation: `src/netbridge.c`, polled from the main loop as `net_bridge_poll()`.

## MegaFlash example

```bash
export MEGAFLASH_UF2=../MegaFlash/pico/pico2_debug/megaflash.uf2
UART_CONSOLE_PORT=4444 ./scripts/run-megaflash-stub.sh -uart-console "$UART_CONSOLE_PORT" -timeout 60
# other terminal:
./scripts/connect-uart-console.sh "$UART_CONSOLE_PORT"
```

Firmware calls `stdio_uart_init()` in flash; that uses **UART0**, not USB CDC, unless you also pass `-usb-stdio`.

## Other tools

| Tool | Command |
|------|---------|
| netcat | `nc localhost 4444` |
| socat | `socat -,raw,echo=0 TCP:localhost:4444` |
| minicom | `minicom -D tcp:localhost:4444` |
| telnet | `telnet localhost 4444` (raw mode may need stty) |

## Inter-instance wiring (Unix socket)

To connect two Bramble processes (no TCP):

```bash
# A: ./bramble fw_a.uf2 -wire-uart0 /tmp/uart.sock
# B: ./bramble fw_b.uf2 -wire-uart0 /tmp/uart.sock
```

See `include/wire.h`.

## Windows

- **Native Windows build:** Bramble’s TCP bridge uses POSIX sockets (`poll`, `arpa/inet.h`). A native MSVC build would need a Winsock port of `netbridge.c` (not done yet).
- **Practical options today:**
  - Run Bramble in **WSL2** or on a Mac/Linux host; use **PuTTY** / **ncat** on Windows as a TCP client to `host:4444`.
  - Run Bramble on the same machine as the firmware dev host; connect from any OS that can open TCP.

## GDB vs UART console

| Service | Default port | Protocol |
|---------|--------------|----------|
| GDB remote | 3333 (`-gdb`) | GDB remote serial |
| UART console | user (`-uart-console`) | Raw bytes |

Use different ports if both are enabled.

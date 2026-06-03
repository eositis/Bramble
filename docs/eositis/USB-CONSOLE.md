# USB CDC console (MegaFlash diagnostic terminal)

MegaFlash on Pico W checks **`stdio_usb_connected()`** after boot. That maps to TinyUSB **`tud_cdc_n_connected()`**, which requires:

1. USB device stack running (`stdio_usb_init()` in firmware)
2. Bramble’s **USB host simulation** to enumerate the device and assert **DTR** (`SET_CONTROL_LINE_STATE`)
3. Firmware not taking the “Apple II connected” path that disables the USB menu (Release builds)

Bramble implements (1)–(2) in `src/usb.c` whenever the guest enables the USB controller.

## Quick start — TCP session (recommended)

Same pattern as [UART-CONSOLE.md](UART-CONSOLE.md), but traffic goes to **USB CDC**, not UART0.

**Terminal 1 — emulator:**

```bash
./build/bramble megaflash.uf2 -arch m33 -clock 150 -cores 2 \
  -usb-console 5555 -usb-stdio -timeout 120
```

Wait for:

```text
[USB] CDC console listening on TCP port 5555
[USB] CDC active — host may call stdio_usb_connected() (DTR asserted)
```

**Terminal 2 — menu / diagnostics:**

```bash
./scripts/connect-usb-console.sh 5555
```

You should see the MegaFlash ASCII banner and `UserTerminal()` menu (TFTP, flash tools, etc.).

### Flags

| Flag | Purpose |
|------|---------|
| `-usb-console <port>` | Bidirectional USB CDC ↔ TCP (`nc localhost <port>`) |
| `-usb-stdio` | Route host input to **USB CDC first** (needed when firmware also prints on UART0) |
| `-stdin` | Bridge terminal to guest console (UART or USB); implies USB stdout when CDC is active |
| `-trace-usb` | Log enumeration steps to stderr (debug “connected at boot”) |

**Do not** combine `-stdin` on Bramble with a second `nc` feeding the same port — use **either** interactive Bramble stdin **or** TCP `nc`, not both.

### MegaFlash stub helper

```bash
USB_CONSOLE_PORT=5555 ./scripts/run-megaflash-stub.sh -usb-stdio -timeout 120
# other terminal:
./scripts/connect-usb-console.sh 5555
```

(`run-megaflash-stub.sh` adds `-usb-console` when `USB_CONSOLE_PORT` is set.)

## How firmware chooses USB vs Apple bus

From `MegaFlash/pico/main.c` (Pico W / CheckPicoW):

| Build | Condition | Core0 behavior |
|-------|-----------|----------------|
| **Debug** | `stdio_usb_connected()` | `UserTerminal()` (USB diagnostic module) |
| **Release** | `stdio_usb_connected() && !IsAppleConnected()` | `UserTerminal()` |
| Either | Apple connected at boot | `core0Loop()` (network), USB terminal suppressed in Release |

Implications for Bramble:

- Use a **Debug** UF2 (`pico2_debug/megaflash.uf2`) for easiest USB-menu testing, **or**
- In **Release**, do **not** drive `IsAppleConnected()` true before core0 checks USB — avoid early `a2phi` in `megaflash-bus.stub` if you want the USB menu.
- UART debug (`-uart-console`) does **not** satisfy `stdio_usb_connected()`; use **`-usb-console`**.

## What Bramble’s USB host does

On each `usb_step()` after the guest enables USB:

1. Simulates attach / bus reset / descriptor fetch / `SET_CONFIGURATION`
2. Sends **`SET_CONTROL_LINE_STATE` with DTR+RTS** so `tud_cdc_n_connected()` returns true
3. Bridges CDC **IN** (device → host) to TCP or stdout
4. Bridges TCP or stdin → CDC **OUT** (host → device) via `usb_cdc_rx_push()`

RP2350 USB registers: `0x50100000` (DPRAM) / `0x50110000` (controller) — already mapped.

## Troubleshooting

| Symptom | Check |
|---------|--------|
| No `[USB] CDC active` | `-trace-usb`; guest may not have called `stdio_usb_init()` yet |
| TCP connects but no menu | Missing **`-usb-stdio`** while firmware uses UART for early prints |
| Never enters `UserTerminal` | Release + Apple connected; use Debug UF2 or adjust stub |
| Garbled text | Wrong port or duplicate input (`-stdin` + `nc` both feeding RX) |

## UART vs USB (MegaFlash)

| Channel | Firmware API | Bramble flag |
|---------|--------------|--------------|
| UART cable / debug serial | `stdio_uart_init()` | `-uart-console <port>` |
| USB serial (FT232 / built-in USB) | `stdio_usb_init()` | `-usb-console <port>` + `-usb-stdio` |

## Windows

Run Bramble on **macOS, Linux, or WSL2**. Connect with **PuTTY** (raw TCP to `host:5555`) or **ncat** from Windows. Native Bramble on Windows still needs a Winsock port of the bridge code.

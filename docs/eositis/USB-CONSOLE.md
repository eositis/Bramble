# USB CDC console (MegaFlash diagnostic terminal)

MegaFlash on Pico W checks **`stdio_usb_connected()`** after boot. That maps to TinyUSB **`tud_cdc_n_connected()`**, which requires:

1. USB device stack running (`stdio_usb_init()` in firmware)
2. Bramble’s **USB host simulation** to enumerate the device and assert **DTR** (`SET_CONTROL_LINE_STATE`)
3. Firmware not taking the “Apple II connected” path that disables the USB menu (Release builds)

Bramble implements (1)–(2) in `src/usb.c` whenever the guest enables the USB controller.

## Quick start — virtual serial port (recommended for terminal apps)

Use a **PTY** so standard serial programs (`screen`, `cu`, Serial.app, etc.) can attach without TCP.

On **macOS**, `./scripts/run-megaflash-usb-console.sh` defaults to PTY mode (virtual serial). Set `USB_CONSOLE_TCP=1` to use the legacy TCP socket instead.

**Terminal 1 — emulator:**

```bash
./scripts/run-megaflash-usb-console.sh
# or:
./build/bramble ../MegaFlash/pico/pico2_debug/megaflash.uf2 \
  -arch m33 -clock 150 -cores 1 \
  -usb-serial -usb-stdio -timeout 120
```

Wait for:

```text
[USB] CDC console on serial port /dev/ttysNNN
[USB]   attach: screen /tmp/bramble-usb-console 115200
```

**Terminal 2 — menu / diagnostics:**

```bash
./scripts/connect-usb-console.sh
# or: screen /tmp/bramble-usb-console 115200
```

### macOS terminal and serial programs

| Program | How to connect |
|---------|----------------|
| **Terminal.app** (new window) | `./scripts/open-usb-console-macos.sh` |
| **screen** / **cu** | `./scripts/connect-usb-console.sh` or `screen /tmp/bramble-usb-console 115200` |
| **Serial.app**, **CoolTerm**, etc. | Pick `/dev/ttysNNN` from the port list (path printed in Bramble stderr), or open symlink `/tmp/bramble-usb-console` if the app allows custom paths. Baud: **115200** (ignored by CDC). |
| **iTerm2** | Same as Terminal: `screen /tmp/bramble-usb-console 115200` |

Bramble must stay running in Terminal 1 while you use the serial port. If connect says `PTY not found`, Bramble is not running or exited (e.g. timeout).

### XMODEM upload (menu item 2)

The PTY is a **binary-safe** serial pipe — suitable for XMODEM-CRC. Use a program that can **send a file on the same connection** you use for the menu:

**Recommended: minicom** (single session, built-in XMODEM send):

```bash
brew install minicom
# Terminal 1: ./scripts/run-megaflash-usb-console.sh
# Terminal 2:
minicom -D /tmp/bramble-usb-console -b 115200
```

In minicom: menu **2** (Upload) → unit **1** → type **CONFIRM** → when MegaFlash sends **`C`** repeatedly, press **Ctrl-A** then **S**, pick your `.po`/`.hdv` file, choose **xmodem** or **xmodem-crc**.

**Alternative: lrzsz** (must own the port — disconnect screen/minicom first):

```bash
brew install lrzsz
# After MegaFlash shows "Please start upload..." and sends C, quit other clients, then:
sz -y /tmp/bramble-usb-console /path/to/disk.po
```

Only **one** program may open the serial port at a time.

**Emulator note:** Bramble now exposes one flash unit for upload (`GetTotalUnitCount` / mapping stubs). Full SPI flash programming during XMODEM is still partial under emulation — you may reach the XMODEM handshake before writes complete or verify. TCP mode works the same way but is harder to use with standard serial/XMODEM tools.

| Flag | Purpose |
|------|---------|
| `-usb-serial [path]` | PTY with optional symlink (default `/tmp/bramble-usb-console`) |
| `-usb-console pty[:path]` | Same as `-usb-serial` |
| `-usb-console <port>` | Legacy TCP mode (`nc localhost <port>`) |

Baud rate is ignored by USB CDC but terminal programs often require one (use `115200`).

## Quick start — TCP session

Same pattern as [UART-CONSOLE.md](UART-CONSOLE.md), but traffic goes to **USB CDC**, not UART0.

**Do not use `megaflash-bus.stub` or `run-megaflash-stub.sh` for USB menu testing.** The stub’s `a2phi` event makes `IsAppleConnected()` return true. Firmware then enters **`core0Loop()`** (Apple bus / network) and **does not** run **`UserTerminal()`** (USB diagnostic menu). Bramble also ignores `a2phi` while `-usb-console` is active, but the no-stub runner is the supported path.

**Terminal 1 — emulator (no Apple stub):**

```bash
./scripts/run-megaflash-usb-console.sh
# or:
./build/bramble ../MegaFlash/pico/pico2_debug/megaflash.uf2 \
  -arch m33 -clock 150 -cores 2 \
  -usb-console 5555 -usb-stdio -timeout 120
```

Wait for:

```text
[USB] CDC console listening on TCP port 5555
[USB] CDC active — host may call stdio_usb_connected() (DTR asserted)
```

The `CDC active` line appears automatically once the guest USB stack is up (no `-trace-usb` required). Connect `nc` early so DTR is mirrored into guest RAM.

**Terminal 2 — menu / diagnostics:**

```bash
./scripts/connect-usb-console.sh 5555
```

You should see the DEBUG boot banner, ASCII art, and `UserTerminal()` main menu (`Device Information`, upload/download ProDOS image, etc.).

**Verified (2026-06-02):** `pico2_debug/megaflash.uf2` with `-cores 1`, no Apple-bus stub — TCP receives the full banner plus menu prompt `Please Select:`. Sending `1` echoes `1` and runs menu item 1 (Device Information).

**Input path:** TCP bytes are pushed into the guest TinyUSB CDC RX fifo; `stdio_usb` driver RAM is seeded because `stdio_usb_init` is skipped under emulation.

### Flags

| Flag | Purpose |
|------|---------|
| `-usb-console <port>` | Bidirectional USB CDC ↔ TCP (`nc localhost <port>`) |
| `-usb-console pty[:path]` / `-usb-serial` | USB CDC on a host PTY (virtual serial port) |
| `-usb-stdio` | Route host input to **USB CDC first** (needed when firmware also prints on UART0) |
| `-stdin` | Bridge terminal to guest console (UART or USB); implies USB stdout when CDC is active |
| `-trace-usb` | Log enumeration steps to stderr (debug “connected at boot”) |

**Do not** combine `-stdin` on Bramble with a second `nc` feeding the same port — use **either** interactive Bramble stdin **or** TCP `nc`, not both.

### Apple bus stub (slot I/O only — not USB menu)

Use when exercising **`core0Loop`**, `a2read` / `a2write`, and core1 — **not** when you want the USB terminal:

```bash
./scripts/run-megaflash-stub.sh -timeout 120
```

If you set `USB_CONSOLE_PORT` on the stub runner, it exits with a hint to use `run-megaflash-usb-console.sh` instead.

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
| No `[USB] CDC active` | Rebuild `bramble` after pulling USB fixes; guest may not have reached `stdio_usb_init()` yet |
| TCP connects but no menu | Missing **`-usb-stdio`**; or core0 still in TinyUSB init (see PC ~`0x1000F5C0`) — connect `nc` early, wait for `CDC active` |
| PC stuck ~`0x1000F5C6` | USB control-transfer stall in `stdio_usb_init`; ensure fresh build and `-usb-console` |
| Never enters `UserTerminal` | **Apple connected** (`IsAppleConnected()` after stub `a2phi`) — use `run-megaflash-usb-console.sh` **without** `-script`; Release also requires `!IsAppleConnected()` |
| Garbled text | Wrong port or duplicate input (`-stdin` + `nc` both feeding RX) |

## UART vs USB (MegaFlash)

| Channel | Firmware API | Bramble flag |
|---------|--------------|--------------|
| UART cable / debug serial | `stdio_uart_init()` | `-uart-console <port>` |
| USB serial (FT232 / built-in USB) | `stdio_usb_init()` | `-usb-console <port>` + `-usb-stdio` |

## Windows

Run Bramble on **macOS, Linux, or WSL2**. Connect with **PuTTY** (raw TCP to `host:5555`) or **ncat** from Windows. Native Bramble on Windows still needs a Winsock port of the bridge code.

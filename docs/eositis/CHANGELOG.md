# Change log (eositis fork)

Baseline: **clone from `origin/main`** (upstream Bramble v0.45.0, RP2350 M33/RISC-V emulator).  
Scope: local commits on `main` after clone.

---

## Unreleased

| Change | Reason |
|--------|--------|
| Move MAME plugin / launcher / stub / probe / MAME-BRIDGE body to sibling `megaflash-vm` | Split attributions: outside GPIO = megaflash-vm; pin face stays in Bramble |
| `docs/eositis/MAME-BRIDGE.md` → pointer | Operators find the integration project |

---

## 2026-07-30 — `091febc` — revert host-clock stub expansion

| Change | Reason |
|--------|--------|
| Revert DoGetTimeString / ProDOS / full `aon_timer_*` host stubs | User typo (“not” vs “now” updating); keep TBH fix + synthetic NTP seed only |

---

## 2026-07-30 — `ae349e5` — TBH decode + DoGetTimeString veneer

| Change | Reason |
|--------|--------|
| Decode TBB/TBH before LDRD bit6 heuristic | Control-Reset → `_svfprintf_r` TBH misread as LDRD Rt=PC → HardFault `0x00B103D0` |
| a2bus: run RTC hooks before picoram veneer rewrite | SRAM `DoGetTimeString` veneer skipped stub → firmware `sprintf` + stuck clock |

---

## 2026-07-30 — `4f3d7cd` — host-clock MegaFlash RTC

| Change | Reason |
|--------|--------|
| a2bus: stub `DoGetTimeString` / ProDOS timestamps / full `aon_timer_*` from host clock | POWMAN AON not on M33 membus → real aon_timer returned epoch 0 (stuck 12:00) |

---

## 2026-07-30 — `1a2b834` — synthetic NTP, quiet u2macraw, exit with MAME

| Change | Reason |
|--------|--------|
| a2bus: synthetic `GetNetworkTime` seeds `rtcRunning` + host calendar | NTP under MAME never ran (cyw43 stubbed); CP/OS clock stuck at 12:00 |
| a2bus: suppress `[u2macraw]` printf spam (`BRAMBLE_A2BUS_U2MACRAW=1` to show) | `U2_Net_Poll` flooded logs every ~5 s with zero counters |
| Launcher: do not `exec` MAME; bridge exits after MAME READ/WRITE disconnect | Bramble stayed running after MAME quit (`exec` dropped EXIT trap) |

---

## 2026-07-30 — `40e284c` — restore a2bus boot (stub cyw43_arch_init)

| Change | Reason |
|--------|--------|
| a2bus: stub `cyw43_arch_init` / `cyw43_arch_gpio_put` / `InitCyw43` / `ConnectWifi` by default | Real InitPicoLed gSPI HardFaults BusLoop → MegaFlash not found / no boot |
| Restore synthetic configured-SSID TestWifi; `BRAMBLE_A2BUS_REAL_WIFI=1` for real gSPI | Keep CP usable; opt-in FEEDBEAD path while core1 fault is WIP |
| Docs: MAME-BRIDGE notes REAL_WIFI + boot regression | Operators know default is BusLoop-safe |

---

## 2026-07-29 — `26fece6` — RP2350 DMA + real CYW43 gSPI test-pattern

## 2026-07-29 — `c36e139` — Test Wifi CP strings + TAP/NAT note

| Change | Reason |
|--------|--------|
| a2bus configured TestWifi writes four C strings into `dataBuffer` (+ BE param IPs) | CP showed netmask `4.2` / blank gw/DNS — guest `FormatIPAddr`/`ip4addr_ntoa` after synthetic fill was wrong |

---

## 2026-07-29 — `2a27b9e` — configured-SSID Test Wifi under a2bus

| Change | Reason |
|--------|--------|
| a2bus: jump `stdio_usb_init` skip to `bl InitPicoLed` (not past it) | Wrong PC skipped `cyw43_arch_init`; TestWifi asserted in `cyw43_ensure_up` |
| LDAEXB free-range includes `_sw_spin_locks` @ `0x2000b794` | Stale `0x20005e34` left `sleep_until` spinning during wifi init |
| a2bus: stub `sleep_ms`/`sleep_us`/`sleep_until` (advance TIMER0) | Dual-core WFE starved guest time during `InitPicoLed` |
| a2bus: unlock `malloc_mutex`; nop mutex enter/exit veneers | Zero owner deadlocked `wrap_malloc` before InitCyw43 |
| a2bus: empty SSID → `SSIDNOTSET`; configured SSID → synthetic `NETERR_NONE` + `192.168.4.2` | CP Test Wifi usable; real gSPI test-pattern JOIN still WIP |
| Optional `BRAMBLE_A2BUS_SEED_WIFI=1` seeds BrambleNet creds | Bring-up without CP Save Settings |

---

## 2026-07-29 — `0c04ab9` — stop Test Wifi hang on empty SSID


| Change | Reason |
|--------|--------|
| Bootrom MSP at ROM+0 nonzero; drain handshake FIFO on script core1 launch | Core1-reset FIFO ack `0` was read as IPC ptr; `*(uint32_t*)0==0` looked like TestWifi |
| Emulate word `LDA`/`STL`; fix `CLZ`/`RBIT` Rm mask | libstdc++ atomics + demangle during `__cxa_throw` hung/HardFaulted |
| a2bus empty-SSID fast-fail on `DoTestWifi` / `GetNetworkTime` | Empty SSID throws; EH still broken — CP Test Wifi hung on “Testing…” |

---

## 2026-07-29 — `406f0d9` — fix cyw43_arch_init HardFault on a2bus

| Change | Reason |
|--------|--------|
| ARMv8-M `TT` insn; STREXB `Rd` in bits[3:0]; ROM `SR`/`SS` + nop-on-miss | Fix core0 lockup before/during `cyw43_arch_init` (PC→`.data`, `PC=0xFFFFFFFF`) |
| Early `core1launch` in `megaflash-mame.stub`; CORE0 HardFault diagnostics | BusLoop survives wifi bring-up; easier next faults |

---

## 2026-07-29 — `040f44d` — route CP Test Wifi through Bramble `-wifi` CYW43

| Change | Reason |
|--------|--------|
| Remove a2bus `DoTestWifi` stub; launcher passes `-wifi` | Test Wifi is Bramble CYW43, not a fake SSID error |
| a2bus: skip `stdio_usb_init`, host printf, `hw_claim`/`check_alloc`, multicore skip; force `CheckPicoW` when `-wifi` | Let core0 reach `InitPicoLed`/`cyw43_arch_init` and IPC |
| Docs note: `cyw43_arch_init` HardFault still blocks full TESTWIFI | Next gap after bring-up stubs |

---

## 2026-07-29 — `0b6d0b5` — stub CP Test Wifi under a2bus

| Change | Reason |
|--------|--------|
| a2bus stub `DoTestWifi` (veneer + flash entry); return `SSIDNOTSET` / `WIFINOTCONN` | CP **Test Wifi** → **Unexpected Error.** — no CYW43/core0 IPC under MAME bridge |
| Probe `CMD_TESTWIFI` in `a2bus-probe-settings.py`; note in `MAME-BRIDGE.md` | Host-side check without MAME |

---

## 2026-07-29 — `c11f059` — stub CP Save under a2bus

| Change | Reason |
|--------|--------|
| a2bus stub `SaveUserSettings` / security-register SPI; persist to `flash/megaflash-user-config.bin` | CP Save hung on **Saving...** — `EncryptWriteConfigToFlash` never finished under a2bus |
| Reject WriteBlock for logical unit > flash unit count | Log showed writes to unit 5 with no backing (only 4 flash units) |

---

## 2026-07-29 — `e8a125a` — seed config when core0 lags BusLoop

| Change | Reason |
|--------|--------|
| a2bus hooks always run when bridge active; late-seed `configBuffer` on `GetUserSettings`; skip `InitSpi`/`U2_Init` | Core1 served the bus before core0 finished `LoadAllConfigs` → GETUSERSETTINGS had `timezoneidver=0` → CP **Unexpected Error:0** |
| Delay `core1launch` to 2s in `megaflash-mame.stub` | Give core0 more wall time past crt0 before BusLoop |
| `scripts/a2bus-probe-settings.py` | Host-side GETUSERSETTINGS / READBLOCK check without MAME |

---

## 2026-07-29 — `7569508` — linear DATA mode + flash unit map

| Change | Reason |
|--------|--------|
| `host_fast_read`/`write` use `mem_read8(dataBufferTransferMode)` | 32-bit load saw adjacent BSS → LINEAR treated as interleaved → CP option 7 **Unexpected Error:0** (`ERR_CONFIG_INVALID`) |
| a2bus `LoadAllConfigs` keeps `DEFCFGBYTE1` (AUTOBOOT only) | Forced `ROMDISKFLAG` added an extra SmartPort unit; not needed for flash boot |
| ReadBlock/WriteBlock/GetVolumeInfo honor `FLASH_UNIT_MAP` | Match firmware `MapFlashUnitNum` so logical units hit the right medium |

---

## 2026-07-28 — `4e5a21f` — restore boot after unsafe F85F decode

| Change | Reason |
|--------|--------|
| Revert Thumb-2 global `F85F` literal handler; keep a2bus rewrite of `__CheckWriteEnableKey_veneer` only | Broad LDR-literal decode parked MegaFlash in bootrom (~50 steps / no MAME); option 7 still needs the targeted veneer rewrite |

---

## 2026-07-28 — `c0e80a1` — NSC mute + option 7 veneer HardFault

| Change | Reason |
|--------|--------|
| a2bus rewrite `__CheckWriteEnableKey_veneer` (not global Thumb LDR-literal) | Option 7 / DriveMapping HardFaulted at `0x5F200004` when `r0=1`; global F85F decode broke boot |
| Mute MAME DS1216E NSC on `$C100–$CFFF` in `megaflash_bridge` | Built-in no-slot clock must not own ProDOS time; MegaFlash supplies clock |
| Seed MegaFlash `rtcRunning` + stub `aon_timer_get_time_calendar` on a2bus | No NTP under MAME; `CMD_GETTIMESTR` otherwise returns spaces |

---

## 2026-07-28 — `c76ea09` — COLDSTART BUSY pump + AUTOBOOT config

| Change | Reason |
|--------|--------|
| a2bus pump waits for BUSY assert→clear (no 512-step early exit) | Premature CMD return → Apple read `configbyte1=0`, skipped slot 4 boot; option 7 LOAD_CPANEL raced the same way |
| Stub `GetConfigByte1`/`GetConfigByte2` on a2bus; ensure AUTOBOOT default | Belt-and-suspenders if configBuffer was left zero |

---

## 2026-07-28 — `229d66f` — TranslateUnitNum veneer for A2.DESKTOP boot

| Change | Reason |
|--------|--------|
| Rewrite `__TranslateUnitNum_veneer` (flash→SRAM) on a2bus | `GETVOLINFO`/`GetMediumType` HardFaulted; volume name / SmartPort boot from flash unit 1 broken |
| Document flash volume 1 = `A2.DESKTOP` boot source (no MAME HD) | MAME must boot via MegaFlash SmartPort, not a separate disk image |

---

## 2026-07-28 — `233ebe4` — host-side DATA/PARAM for LOAD_CPANEL

| Change | Reason |
|--------|--------|
| Host-side idle DATA/PARAM/ID/STATUS RPC path (gated on ID `$96`/`$69`) | Control panel hung: 58×256 `$C0C2` reads each did inject+pump |
| Adaptive pump: stop when BUSY clears (or early if never busy) | CMD RPCs need enough steps without glacial idle pumps |
| Document fast path in `MAME-BRIDGE.md` | Operators need to know BSS is pico2_debug-tied |

---

## 2026-07-28 — `3f26e1a` — a2bus ReadBlock + lower RPC pump

| Change | Reason |
|--------|--------|
| Stub RAM `ReadBlock`/`WriteBlock` (DoReadBlock calls RAM, not veneer) | PR#4 hung on `CMD_READBLOCK` BUSY then HardFault at `_exit` |
| Lower a2bus `pump_steps` 500000→65536 | Busy-wait RPCs made MAME nearly unkillable |

---

## 2026-07-28 — `132d61b` — SmartPort units + LOAD_CPANEL copy

| Change | Reason |
|--------|--------|
| Stub RAM `IsValidUnitNum` / `GetBlockCount` on a2bus | GETDEVSTATUS showed 4 units but GETUNITSTATUS returned `$45` (inlined/RAM path) |
| Intercept `CopyMemoryAligned` veneer; copy via `mem_read8`/`mem_write8` | Control panel LOAD hung/empty — DMA no-op; `cpu.ram` write missed RP2350 SRAM |

---

## 2026-07-28 — `79f0ca3` — a2bus SPI flash stubs for MAME

| Change | Reason |
|--------|--------|
| a2bus path stubs InitFlash / configs / GetTotalUnitCount / ReadBlock against `flash/spi-flash*.bin` | Detect worked but GETDEVSTATUS returned `MFERR_NOFLASH` ($41); PR#4 / control panel saw MegaFlash as missing |

---

## 2026-07-28 — `afd9efb` — keep a2bus bridge alive for MAME

| Change | Reason |
|--------|--------|
| Skip 1B instruction safety exit when `-a2bus-bridge` active | Bramble exited mid-MAME session; MegaFlash looked missing |
| Plugin retries tap install from periodic callback (v0.1.5) | Reset notifier can miss first reset; log showed connect without taps |

---

## 2026-07-28 — `6caa058` — MAME bridge TCP client

| Change | Reason |
|--------|--------|
| MAME plugin opens TCP as client-only (no CREATE); retry connect | CREATE made MAME listen on 19765 → EADDRINUSE; taps ran with sock=nil |
| Launcher keeps stock MAME plugins on pluginspath and passes `-plugins` | Replacing pluginspath dropped `boot.lua`; plugin never started |
| Bridge drops disconnected clients (MSG_PEEK EOF) | Stale `client_fd` blocked accept after launcher probe |

---

## 2026-07-28 — `aec4569` — a2bus READ pre-cycle byte

| Change | Reason |
|--------|--------|
| a2bus READ RPC returns pre-cycle register shadow | Post-pump peek skipped SIGNATURE1 (`$88`); `chkmegaflashex` / control panel reported MegaFlash not found |

---

## 2026-07-28 — `661b49e` — MegaFlash detect over a2bus bridge

| Change | Reason |
|--------|--------|
| Inject full 13-bit bus words (`0x1FFF`); inject-only (no nDEVSEL double-push); `pio_drain_rx` before inject | 5-bit mask made every CMD look like RESET; dual FIFO words left leftovers |
| Do not decode unmatched `EE40`/`EE60` as DP shifted-reg | `gpio_clr_mask` in `DoCommand` was misread as `ORN` and set `r0=0xFFFFFFFF` → `MFERR_UNKNOWNCMD` |
| a2bus veneer hook for `ldr.w pc,[pc]`; core1-only pump; early `core1launch`; SPI flash flags on MAME launcher | Keep BusLoop alive long enough for MAGIC → `CMD_GETDEVINFO` |

---

## 2026-07-28 — `61993d3` — force MegaFlash maincpu in MAME

| Change | Reason |
|--------|--------|
| Stage MegaFlash `iic.bin` as sole `3410445b.256`; keep Ample off rompath | MAME preferred CRC-matching stock ROM, so MegaFlash never ran |

---

## 2026-07-27 — `98be297` — fix MAME rompath / iic overlay

| Change | Reason |
|--------|--------|
| Launcher uses Ample rompath + Lua `iic.bin` overlay (no on-disk CRC replace) | Missing CHR/keyboard/`sc01a`; wrong `3410445b.256` CRC aborted MAME |

---

## 2026-07-27 — `05a0fc0` — MAME MegaFlash bus bridge

| Change | Reason |
|--------|--------|
| Add `-a2bus-bridge` TCP server + register peek/inject path (`a2bus_bridge.c`) | Let MAME forward `$C0C0–$C0C3` to MegaFlash under Bramble |
| MAME Lua plugin `megaflash_bridge`, `run-megaflash-mame.sh`, `MAME-BRIDGE.md` | Integrated apple2c4 + `iic.bin` launch; keep default 128K RAM |
| Gitignore `iic.bin` / `roms/` | Apple ROM IP stays local |

---

## 2026-07-27 — `124252e` — add tio console and XMODEM upload guide

| Change | Reason |
|--------|--------|
| Add `docs/eositis/TIO-CONSOLE.md`; link from README and USB-CONSOLE | Document tio attach, menu XMODEM (`Ctrl-T x`), and quit (`Ctrl-T q`) |

---

## 2026-07-27 — `2f20b08` — retarget MegaFlash USB hooks + macOS PTY TX

| Change | Reason |
|--------|--------|
| Retarget MegaFlash USB guest hooks to current `pico2_debug/megaflash.elf` symbols/BSS | Rebuilt UF2 shifted code/RAM; old PCs left console stuck before UserTerminal |
| Close openpty slave FD after symlink; keep only master | Holding slave open on macOS blocked tio/external readers from seeing master TX |

---

## 2026-06-08 — `d2901fd` — speed XMODEM ACK path for tio's 1s timeout

| Change | Reason |
|--------|--------|
| In-packet CPU turbo + aggressive PTY poll during XMODEM | tio/minicom wait ~1s per ACK; emulated PacketReceived was too slow wall-clock |
| PTY TX pending queue + `tcdrain` on ACK | ACK bytes must reach tio before its 1s timeout |
| Raw termios on PTY slave; `-usb-stdio` opt-in via `USB_CONSOLE_STDIO=1` | Avoid cooked slave + stray stdin on Terminal 1 during tio upload |

---

## 2026-06-08 — `56f230d` — decouple XMODEM read-ahead from host-RX throttle

| Change | Reason |
|--------|--------|
| Decouple 8KB XMODEM read-ahead from 90% host-RX throttle flag | Read-ahead cap at 8192 B wrongly set `throttled`; release needed ~209KB drain, blocking PTY RX after a few flash blocks |

---

## 2026-06-08 — `ac96692` — sustain 32MB XMODEM uploads

| Change | Reason |
|--------|--------|
| 256KB host RX buffer + 8KB XMODEM read-ahead cap | Long uploads prefetched dozens of packets while guest wrote flash, refilling buffer to overflow |
| Windowed mmap msync (2MB) during flash writes | Full 64MB msync every 256KB stalled guest and backed up PTY |
| `scripts/test-32mb-xmodem.sh`; `XMODEM_ACK_TIMEOUT=300` in test driver | Supervised full-image regression; guest 1s packet timeout needs generous host ACK wait |
| Verify against actual `.po` size (33553920 B for `A2OSX.STABLE.32MB.po`) | Image is not exactly 32 MiB |

---

## 2026-06-08 — `9196826` — throttle host RX at 90% during XMODEM

| Change | Reason |
|--------|--------|
| Host RX throttle 90%/80% hysteresis on PTY reads | 64KB host RX buffer overflow dropped XMODEM data when guest lagged behind the sender |
| `usb_console_tcp_poll_rx(force_rx)` for active packet reads | Background reads must not starve in-flight XMODEM block assembly |
| Removed flash-write PTY drain that prefilled host RX | Prefetch during slow flash writes caused overflow before throttle helped |

---

## 2026-06-08 — `b0af78f` — XMODEM flash upload reliability and mmap-backed SPI I/O

| Change | Reason |
|--------|--------|
| `-spi-flash1` / `-spi-flash2` with optional path per chip | Two SPI chips match MegaFlash hardware layout |
| Default paths `flash/spi-flash1.bin`, `flash/spi-flash2.bin` | Auto-create `flash/` when no path given |
| `-spi-flash1-size` / `-spi-flash2-size` (MB, multiple of 32) | Configurable chip capacity (default 64MB each) |
| Run script enables both chips with defaults | Persistent storage without extra flags |
| `WriteBlockForImageTransfer` stub → SPI backing files | XMODEM upload bypasses WriteBlock veneer; was hitting unemulated SPI program |
| Host-polling `usb_getraw_timeout` / larger `stdio_usb_in` reads | XMODEM-1K (1028-byte) packets need bulk CDC RX |
| XMODEM flash flush hooks + 64KB host RX buffer | Full upload was NAK/serial-fail when PTY backed up during slow emulated flash writes |
| 30s host RX timeout | Guest xmodemrx 1s packet timeout was too tight vs emulated flash write latency |
| Default `TIMEOUT=7200`, `CORES=1` in USB run script | 120s timeout killed long uploads; dual-core added avoidable load during XMODEM |
| SPI baud stub 75 MHz + Winbond W25Q512JV JEDEC (`0xEF4020`) | Boot/device info showed placeholder 1 MHz; SPI `0x9F` veneer had wrong byte order |
| mmap flash backing + sequential I/O + lighter XMODEM host poll | XMODEM flash writes were ~19s/256KB due to per-instruction PTY poll and fseek/fwrite/fflush per 512 B block |
| `scripts/test-xmodem-upload.py` | Automated menu + XMODEM driver for regression |

---

## 2026-06-06 — `efc5d1c` — enable USB flash unit path for XMODEM upload

| Change | Reason |
|--------|--------|
| Flash unit stubs + keep mapping for USB XMODEM upload path | Menu item 2 no longer stops at "Error: no flash" |
| PTY raw mode + XMODEM workflow in `USB-CONSOLE.md` | Binary XMODEM via minicom / lrzsz on virtual serial |

---

## 2026-06-06 — `ce991a0` — default USB PTY on macOS

| Change | Reason |
|--------|--------|
| macOS: PTY default in run script; `open-usb-console-macos.sh` | Terminal.app / screen without `USB_CONSOLE_PTY=1` |

---

## 2026-06-05 — `4e33ca1` — USB CDC on virtual serial port (PTY)

| Change | Reason |
|--------|--------|
| `-usb-console pty[:path]` / `-usb-serial` → host PTY + optional symlink | Use `screen`, `cu`, Serial.app instead of TCP/`nc` |
| `USB_CONSOLE_PTY=1` in run/connect scripts | Default symlink `/tmp/bramble-usb-console` |
| TCP mode unchanged (`-usb-console <port>`) | Backward compatible with existing workflows |

---

## 2026-06-05 — `e98c27f` — report 64MB per flash chip in USB device info

| Change | Reason |
|--------|--------|
| Device-info / partition stubs: 64MB per chip (128MB total) | Match emulated MegaFlash external flash sizing |

---

## 2026-06-05 — `2053c69` — MegaFlash USB menu Device Information on key 1

| Change | Reason |
|--------|--------|
| Remove `DeviceInfo` → `PrintBanner` redirect @ `0x10005AEC` | Key `1` re-sent full menu instead of device info |
| Host-built multiline `GetDeviceInfoString` stub @ `0x10005058` | Native path uses `sprintf(%f)` after VFP; spins under emu |
| Stub `PrintAllPartitions` @ `0x100057A0` for USB console | Flash unit mapping disabled in `UserTerminal`; no partition lines |

---

## 2026-06-04 — `d7f10d9` — USB CDC TCP input via guest RX fifo

| Change | Reason |
|--------|--------|
| Seed `stdio_usb` driver @ `0x200047D4` + chain @ `0x20007854` | `stdio_usb_init` skipped; `usb_getchar` called NULL `in_chars` |
| TCP RX → guest CDC RX `tu_fifo` (not host DPRAM only) | TinyUSB stack not running; guest reads guest RAM fifo |
| Hook `stdio_usb_in_chars`, `tud_cdc_n_available`, `tud_cdc_n_read` | Bypass mutex/`tud_task` spin; direct fifo pop |

---

## 2026-06-04 — `37c38a5` — MegaFlash USB CDC UserTerminal over TCP

| Change | Reason |
|--------|--------|
| USB TCP pending TX buffer (4 KiB) | Early guest CDC output lost before `nc` attaches |
| USB mode: host `__wrap_printf` / `stdio_put_string` → CDC TCP | Same `_vfprintf_r` hang as UART; stub-only printf sent nothing |
| `IsAppleConnected` → 0 (call sites + `0x10004D80`); skip `core0Loop` veneer | Apple II must be offline for USB diagnostic menu |
| `CheckPicoW` → 1 in USB mode; seed `0x2005BC82` | Enter PicoW `stdio_usb_init` / USB wait path |
| Skip `stdio_usb_init`, USB wait loops → `UserTerminal` | TinyUSB init spin/fault under partial USB emu; CDC already bridged in Bramble |

---

## 2026-06-02 — `92d1179` — MegaFlash UART TCP bridge

### MegaFlash UART console — full banner (2026-06-02)

| Change | Reason |
|--------|--------|
| UART path: host `__wrap_printf` / `__wrap_vprintf` (`%s` `%d` `%u` `%lu` `%x` `%c`) | `main` calls `__wrap_printf` not `vprintf`; `_vfprintf_r` locale spin blocked banners |
| Skip `U2_MonInit` / `u2_reset` at call sites in `U2_Init` | `ldaexb` spin in `U2_MonInit` cost ~200M steps before `main` banners |
| Chained `main` call-site stubs (`LoadAllConfigs` → `0x10000310`, `multicore` → `0x10000320`) | `cpu_step` runs the insn at the new PC in the same step — `+4` alone still entered next `bl` |
| Stub `clock_get_hz` / `spi_get_baudrate` / `CheckPicoW` | Banner showed `0MHz`; PicoW branch printed misleading “WIFI Supported = Yes” |

### Initial bridge

| Change | Reason |
|--------|--------|
| `uart_match`: RP2350 UART0/UART1 @ `0x40070000` / `0x40078000` | `stdio_uart_init` uses relocated PL011; guest writes never reached emulator |
| Guest bring-up hooks active for `-uart-console` (not only `-usb-console`) | Locale/SPI/U2 stubs needed without USB CDC path |
| UART mode: `__wrap_puts` / `uart_putc` → `net_bridge_uart_tx` | Early `U2_Init` text reaches TCP |
| Skip `multicore_launch_core1` at `main` `0x10000318` | Core1 FIFO wait blocked boot on `-cores 1` |
| `netbridge`: 4 KiB TX pending buffer until TCP client connects | Early guest output not lost before `nc` attaches |
| `scripts/run-megaflash-uart-console.sh` | No Apple-bus stub; mirror + port 4444 |

## 2026-06-03 — `5840a76` — IRQ ldmdb and aligned PC fetch

| Change | Reason |
|--------|--------|
| `t32_ldst_multiple`: correct P/U/W/L; route `E912`/`Rt==Rt2` as LDM/STM not LDRD | `ldmdb r2,{r0,r1,r2}` in `irq_add_shared_handler` corrupted handler table |
| `cpu_step`: `pc = cpu.r[15] & ~1` before fetch | Misaligned PC left guest in IRQ path executing 16-bit halves of 32-bit insns @ `0x1000ADE5` |
| `t32_bl`: branch target `& ~1` | Consistent even-PC convention |
| `test_m33_thumb2_ldmdb` | Regression for `E912 0007` |

---

## 2026-06-03 — `aaf9ee3` — RP2350 bootrom sys_info and USB guest stdio hooks

### U2 / SPI / multicore

| Change | Reason |
|--------|--------|
| Guest hooks: skip `U2_Init`, `U2_Net_Init`, `u2_mon_push`, `U2_Net_Poll`, `U2_MonPollFlush` | U2 monitor stack fault @ `U2_MonReset` return; Apple II bus not needed for USB menu test |
| Bootstrap `0x20005A774` crit section + `0x200047A4` alarm pool lock → `spin_lock_hw` | Uninitialized lock ptr broke `ldaexb` in `u2_mon_push` / alarm pool |
| `alarm_pool_get_default` hook | Avoid assert @ line 101 when pool singleton unset |
| SPI: idle `0xFF` MISO, auto-clock on empty `SSPDR` read; `__spi_read_blocking_veneer` fill | Guest spun in RAM `spi_read_blocking` @ `0x20002C12` (no RX) |

### stdio hooks + RP2350 bring-up

| Change | Reason |
|--------|--------|
| `cpu_step`: run `usb_console_guest_stdio_hook()` before reading `pc` | Hooks that set `cpu.r[15]` were ignored; guest kept executing stubbed insns |
| `clocks_bus_match` / RP2350 `RESETS` @ `0x40020000`; `pads_qspi` @ `0x40040000` | `spi_unreset` polled wrong peripheral (PADS stub); infinite spin @ `0x1001192A` |
| `thumb32_pico_gpioc`: decode `EE40`/`EE60` `.inst` GPIO helpers | Mis-decoded as ALU → corrupt PC (`0xDF00325C`) in flash init |
| `spi_match` RP2350 bases `0x40080000` / `0x40088000` | Flash driver uses relocated SPI0 |
| Guest hooks: `_vfprintf_r`, `__ascii_mbtowc`, `check_alloc`, flash/SPI veneers, `panic` skip | Past locale spin, JEDEC read, and `BKPT` `_exit` after `hw_claim` panic |
| `rom_get_sys_info` + RP2350 ROM header (`rom_apply_rp2350_header`) + lookup intercept @ `0x0200` | Pre-main `unique_id.c` assert (`rc == 4`) blocked all boot before USB menu |
| Guest stdio hooks: skip `__wrap_printf` by LR, bypass `__wrap_puts` to TCP, force `stdio_usb_connected` when CDC synced | newlib `_vfprintf_r` locale loop hangs under partial VFP |
| RP2350 ADC @ `0x400A0000`; ADC ch0–3 → not Pico W when USB console active | Wrong Pico W detect path |
| Minimal VFP in `thumb32.c` | Float formats in `GetDeviceInfoString` / printf |
| `a2bus`: no-op `a2phi` while USB console TCP active | Stub must not fake Apple online during USB menu test |
| `scripts/run-megaflash-usb-console.sh`, `USB-CONSOLE.md` updates | Document no-stub path for USB menu |

---

## 2026-06-02 — `096f2b3` — MegaFlash M33 bring-up

MegaFlash / RP2350 M33 bring-up. ~+1896 / −243 lines across 21 tracked files + new `a2bus` and `scripts/`. Removed temporary agent debug logging to `~/Documents/junk/` before commit.

### CPU / dual-core (`src/cpu.c`, `include/emulator.h`)

| Change | Reason |
|--------|--------|
| Guest accel for MegaFlash crt0 BSS zero and `.data` copy | Avoid millions of emulated stores at `0x10000182` / `0x1000019a` |
| Guest `memset` fast path for flash `0x10027F40` / word loop at `0x10027F98` | Core0 was stuck in newlib BSS clear |
| `cpu_set_active_ram_for_exec`, 520 KB M33 SRAM on bind/step | Core0 flash-only path masked wrong RAM size |
| `cpu_fetch16_fast` → `mem_read16` for RAM | Core1 executed zeroed 264 KB mirror → NOP slide to HardFault |
| `sio_force_core1_launch`, `sio_core1_guest_ready` (post `U2_Init` at `PC >= 0x10000300`) | Script launch + valid U2 socket table before core1 |
| Core1 IT state clear, icache invalidate on launch | Thumb IT / stale icache after RAM writes |
| HardFault logging for core1 | Diagnose early faults (~404 steps) |

### Memory bus (`src/membus.c`)

| Change | Reason |
|--------|--------|
| `mem_guest_memset` / `mem_guest_memcpy` / word memset | Bulk RAM init with icache invalidation |
| Fix `get_ram()` use before definition | Accel hooks were dropped from binary (build fix) |

### Thumb-32 (`src/thumb32.c`, `tests/test_suite.c`)

| Change | Reason |
|--------|--------|
| Decode `0xEA`/`0xEB` as data-processing, not LDRD | `add.w` in `u2_push_rx_macraw` corrupted `r3` |
| `ldah` / `stlh` (suffix `0x9F`) | U2 ring buffer uses acquire/release halfword ops |
| `test_m33_thumb2_addw_ldah` | Regression for U2 opcodes |

### NVIC / timer / USB / clocks (`src/nvic.c`, `include/nvic.h`, `src/timer.c`, `src/usb.c`, `src/clocks.c`)

| Change | Reason |
|--------|--------|
| Per-core NVIC state, dual-core IRQ delivery tweaks | Core1 exceptions and timer routing for M33 dual-core |
| USB / clock minor updates | RP2350 paths used by firmware init |

### Instructions / PIO / devtools / main (`src/instructions.c`, `src/pio.c`, `src/devtools.c`, `src/main.c`, `include/pio.h`)

| Change | Reason |
|--------|--------|
| Instruction dispatch fixes (M33 paths) | Match hardware ops used by firmware |
| `pio_inject_rx()` | Feed listener SM from stub without real Apple PHI |
| Script timing from `max(timer, core0_steps/…)` | `core1launch` at 800 ms wall time before core0 ready |
| `core1launch` devtools command with retry | Script-driven core1 when firmware skips `multicore_launch_core1` |

### Apple II bus (new `src/a2bus.c`, `include/a2bus.h`, `scripts/`)

| Change | Reason |
|--------|--------|
| Stub bus: `a2phi`, `a2read`, `pioburst`, `core1launch` | Emulate Apple slot cycles and PHI without hardware |
| `megaflash-bus.stub`, `run-megaflash-stub.sh` | Timed events; reorder so early `a2phi` does not skip core1 launch |
| `a2bus_notify_listener()` after cycles | PIO listener sees bus activity |

### RP2350 RV peripherals (`src/rp2350_rv/rp2350_periph.c`, `include/rp2350_rv/rp2350_periph.h`)

| Change | Reason |
|--------|--------|
| Expanded peripheral behavior | RP2350 register accesses during MegaFlash init |

### Build (`CMakeLists.txt`)

| Change | Reason |
|--------|--------|
| Add `a2bus.c` to target | Link new bus stub module |

---

## 2026-05-24 — `9642572` — dual_core_init fix

| File | Change | Reason |
|------|--------|--------|
| `src/corepool.c`, `src/main.c`, `include/corepool.h` | Honor `-cores 2` after `dual_core_init` | Second core thread never started when CLI requested dual-core |
| `.gitignore` | Ignore local build artifacts | Keep working tree clean |

---

## Baseline — `origin/main` (upstream at clone)

No local changes in this section. Upstream includes through v0.45.0: virtual networking, W5500/vnet, Hazard3/Zbb/Zbkb, BOOTRAM, dual-core scaffolding, 320+ tests, etc. See upstream `docs/ROADMAP.md` and git history before `9642572`.

---

## 2026-06-02 — `b7c543a` — UART TCP console aliases

| Change | Reason |
|--------|--------|
| `-uart-console` / `-uart-console1` CLI aliases | Same as `-net-uart0`; easier to find for debugging |
| `-uart-console-mirror` | Optional guest TX copy to host stderr |
| `docs/eositis/UART-CONSOLE.md`, `scripts/connect-uart-console.sh` | Usage for nc/socat; Windows via WSL/PuTTY |
| `UART_CONSOLE_PORT` in `run-megaflash-stub.sh` | Optional TCP console when running MegaFlash stub |

---

## 2026-06-02 — `0c410ae` — USB CDC TCP console

### USB CDC console (`-usb-console`, `-usb-stdio`)

| Change | Reason |
|--------|--------|
| TCP bridge for USB CDC bulk IN/OUT | MegaFlash `UserTerminal()` over USB like physical serial |
| `-usb-stdio` prefers CDC for stdin | Firmware DEBUG output on UART0 was stealing `-stdin` routing |
| `docs/eositis/USB-CONSOLE.md`, `connect-usb-console.sh` | Document boot detection, Release vs Apple gate |

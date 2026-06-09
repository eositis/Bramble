# Session activity log

Append-only log of agent sessions on this Bramble fork (MegaFlash / RP2350 M33).  
Transcript reference: [megaflash dual-core work](c4c672a1-a61d-45a7-8c50-b3eefb78c27b).

---

## 2026-05-24 — Local commit: dual-core init

| Field | Detail |
|-------|--------|
| **Request** | (Local) Fix `-cores 2` not honored after init |
| **Actions** | `corepool.c`, `main.c`, `corepool.h`, `.gitignore` |
| **Outcome** | Commit `9642572` — `dual_core_init fix` (branch `main`, 1 commit ahead of `origin/main`) |

---

## 2026-06-02 — Session: MegaFlash status review

| Field | Detail |
|-------|--------|
| **Request** | Review project status and what remains for `megaflash.uf2` |
| **Actions** | Ran Bramble on firmware; read MegaFlash docs/disassembly; inspected uncommitted dual-core diff |
| **Findings** | Core0 stuck in flash `memset` (~355 KB BSS); core1 at 0 steps / no FIFO launch; firmware needs PIO, dual-core, Apple bus, SPI, Pico W stack |
| **Outcome** | Written status report; tests ~318/319 pass (RV timer peripheral) |
| **Firmware path** | `../MegaFlash/pico/pico2_debug/megaflash.uf2` |

---

## 2026-06-02 — Session: GPIO / bus stub (proceed as proposed)

| Field | Detail |
|-------|--------|
| **Request** | Short-term GPIO/bus emulation via script instead of Apple II hardware |
| **Actions** | Designed stub timeline (`scripts/megaflash-bus.stub`), devtools script commands, Apple II bus / PIO hooks (in progress in tree) |
| **Outcome** | Stub runner and `a2phi` / `a2read` / `core1launch` style events; integration with listener PIO |

---

## 2026-06-02 — Session: Core0 memset / crt0

| Field | Detail |
|-------|--------|
| **Request** | Fix core0 stuck at `PC=0x10027F98` in newlib `memset` |
| **Root cause** | Millions of emulated byte stores; accel hooks not in binary (`membus.c` compile order); wrong RAM for M33 520 KB |
| **Actions** | `guest_megaflash_crt0_accel`, `guest_megaflash_memset_accel`, `mem_guest_*` bulk helpers, icache invalidate on RAM writes, `cpu_set_active_ram_for_exec` |
| **Outcome** | Core0 reaches `_malloc_r` / past BSS instead of spinning in `memset` |

---

## 2026-06-02 — Session: Core1 launch and RAM fetch

| Field | Detail |
|-------|--------|
| **Request** | Proceed with core1 |
| **Symptoms** | Launch at `0x20000120`, then PC walk to `0x20042000`, HardFault ~135k steps (zeroed RAM as NOPs) |
| **Root cause** | `cpu_fetch16_fast()` used `cpu.ram` (264 KB) not `m33_sram` (520 KB) |
| **Actions** | RAM fetch via `mem_read16`; `sio_force_core1_launch`, `sio_core1_guest_ready`, script timing from core0 steps, `core1launch` retry, PIO RX inject, stub reorder (no early `a2phi`) |
| **Outcome** | Core1 runs real RAM code; new fault in U2 at ~404 steps |

---

## 2026-06-02 — Session: U2 HardFault (`u2_push_rx_macraw`)

| Field | Detail |
|-------|--------|
| **Request** | Fix U2 fault at `PC=0x10006039` |
| **Root cause** | Thumb-32 decoder: `add.w` (`0xEBxx`) mis-decoded as LDRD (corrupts `r3` before `ldrh`); `ldah`/`stlh` (`0x9F` suffix) mis-decoded as LDRD |
| **Actions** | Route `0xEA`/`0xEB` to data-processing; add `ldah`/`stlh` handlers; defer core1 until core0 `PC >= 0x10000300` (post `U2_Init`); test `test_m33_thumb2_addw_ldah` |
| **Outcome** | 321/321 unit tests pass; MegaFlash re-run pending on user machine |

---

## 2026-06-03 — Background: SIO literal pool search

| Field | Detail |
|-------|--------|
| **Request** | (Automated) Search firmware for SIO base literals |
| **Actions** | Scanned `/tmp/megaflash.bin` for `0xD0000000`, `0xD0000054`, `0x50000000` |
| **Outcome** | SIO base `0xD0000000`: 9 hits; FIFO offset literal: 0; GPIO `0x50000000`: 21 hits |

---

## 2026-06-02 — Session: Project base rules (this task)

| Field | Detail |
|-------|--------|
| **Request** | Create `docs/eositis/`: session log, changelog, commit-on-task rule |
| **Actions** | Added SESSION-LOG, CHANGELOG, PROJECT-RULES, README; `.cursor/rules/eositis-project.mdc` |
| **Outcome** | Process docs committed; agents must append log and commit per task |
| **Commit** | `61a4e5c` — docs(eositis): session log, changelog, task workflow rules |

---

## 2026-06-02 — Session: Commit MegaFlash bring-up WIP

| Field | Detail |
|-------|--------|
| **Request** | Commit current state |
| **Actions** | Staged all MegaFlash emulator changes (`cpu`, `membus`, `thumb32`, `nvic`, `a2bus`, `scripts`, tests); stripped agent debug log blocks to `~/Documents/junk/`; verified `321/321` tests |
| **Outcome** | Full working-tree snapshot on `main` |
| **Commit** | `096f2b3` — feat(megaflash): M33 dual-core bring-up, bus stub, U2 Thumb-32 |

---

## 2026-06-02 — Session: UART TCP console aliases and docs

| Field | Detail |
|-------|--------|
| **Request** | Bidirectional UART debug session via socket (mac/linux, Windows notes) |
| **Finding** | Already implemented as `-net-uart0` / `netbridge.c`; added discoverable aliases and docs |
| **Actions** | `-uart-console`, `-uart-console-mirror`, `scripts/connect-uart-console.sh`, `docs/eositis/UART-CONSOLE.md` |
| **Outcome** | Use `nc localhost 4444` with `-uart-console 4444`; Windows via WSL or remote TCP client |
| **Commit** | `b7c543a` — feat(uart): -uart-console aliases and docs |

---

## 2026-06-02 — Session: MegaFlash USB CDC console in Bramble

| Field | Detail |
|-------|--------|
| **Request** | Enable USB serial console / diagnostic module at boot in emulator |
| **Finding** | USB host enum + DTR already in `usb.c`; MegaFlash uses `stdio_usb_connected()` → `tud_cdc_n_connected()` |
| **Actions** | `-usb-console` TCP bridge, `-usb-stdio` stdin priority, `USB-CONSOLE.md`, stub env `USB_CONSOLE_PORT` |
| **Outcome** | Use `-usb-console 5555 -usb-stdio` + `connect-usb-console.sh`; avoid Release+Apple stub for USB menu |
| **Commit** | `0c410ae` — feat(usb): USB CDC TCP console |

---

## 2026-06-03 — Session: MegaFlash USB stdio (no Apple stub)

| Field | Detail |
|-------|--------|
| **Request** | Investigate USB/TCP console stdio — run without Apple emulation |
| **Actions** | Traced guest hang: `__assert_func` → `_vfprintf_r` from `unique_id.c` (`rc == 4`); fixed RP2350 ARM bootrom (`rom_get_sys_info`, header layout at `0x16`/`0x18`, lookup intercept); guest stdio hooks in `usb.c` (skip `printf` by LR, bypass `puts` to TCP); RP2350 ADC map, VFP stubs, `a2bus` PHI guard during USB console; `run-megaflash-usb-console.sh` |
| **Tests** | `make -C build bramble bramble_tests`; `./build/bramble_tests` → 321/321 |
| **Run** | `./scripts/run-megaflash-usb-console.sh` + `nc localhost 5555` (no `-script megaflash-bus.stub`) |
| **Outcome** | **Partial / blocked.** Past unique-id assert; guest now spins in `irq_add_shared_handler` @ `PC=0x1000ADE5` — never reaches `UserTerminal()`. TCP still 0 bytes. |
| **Blockers** | IRQ vector / exclusive-monitor emulation; incomplete VFP for any non-skipped `printf` |
| **Transcript** | [USB stdio session](c4c672a1-a61d-45a7-8c50-b3eefb78c27b) |
| **Commit** | `aaf9ee3` — fix(megaflash): RP2350 bootrom sys_info and USB guest stdio hooks |

---

## 2026-06-03 — Session: vfprintf / flash init / panic skip (USB console)

| Field | Detail |
|-------|--------|
| **Request** | Continue proposed stdio path after IRQ fix — reach `UserTerminal`, TCP menu |
| **Actions** | Fixed stdio hook order in `cpu_step`; RP2350 `RESETS` memmap; Pico `gpioc` Thumb-32 decode; RP2350 SPI bases; guest hooks (`_vfprintf_r`, `__ascii_mbtowc`, `check_alloc`, `tsReadJEDECID`, SPI/mutex veneers, `panic` return); HardFault prev-PC log |
| **Tests** | `make -C build bramble bramble_tests`; `./build/bramble_tests` → 322/322 |
| **Run** | `./scripts/run-megaflash-usb-console.sh` (90s): **168M steps**, `PC≈0x1000A9D6` (`hw_claim`), no `*** PANIC ***` after skip; was ~108M @ `spi_unreset`, then ~7k @ flash fault |
| **Outcome** | **Partial.** Past IRQ, `spi_unreset`, flash JEDEC, and `panic`→`_exit` BKPT; still spinning in `hw_claim`. TCP menu not confirmed this run |
| **Transcript** | [vfprintf session](c4c672a1-a61d-45a7-8c50-b3eefb78c27b) |

---

## 2026-06-03 — Session: IRQ spin (`irq_add_shared_handler`)

| Field | Detail |
|-------|--------|
| **Request** | Fix guest spin @ `PC=0x1000ADE5` in `irq_add_shared_handler` |
| **Root cause** | (1) `ldmdb`/`E912` mis-routed as LDRD and wrong L bit (`>>7` vs `>>4`); (2) `cpu_step` fetched at unstripped Thumb PC → 32-bit insns in IRQ path never decoded |
| **Actions** | `t32_ldst_multiple` P/U/W/L + LDM/STM vs LDRD when Rt==Rt2; `cpu.r[15] & ~1` in `cpu_step`; `t32_bl` target aligned; `test_m33_thumb2_ldmdb` |
| **Tests** | `make -C build bramble bramble_tests`; `./build/bramble_tests` → 322/322 |
| **Outcome** | Guest passes IRQ registration; next blocker `_vfprintf_r` @ ~`0x1002F312` and early `*** PANIC ***` |
| **Commit** | `5840a76` — fix(m33): irq_add_shared_handler ldmdb and aligned PC fetch |

---

## 2026-06-02 — Session: MegaFlash U2/SPI/alarm bring-up toward UserTerminal

| Field | Detail |
|-------|--------|
| **Request** | Continue MegaFlash USB CDC TCP console run to `UserTerminal` (`0x10005AD0`); proceed per eositis rules |
| **Actions** | Guest hooks: skip `U2_Init` / `U2_Net_Init` / `u2_mon_push` / U2 poll; bootstrap U2 crit section + alarm pool lock ptr; `alarm_pool_get_default` stub; SPI `0xFF` MISO + SSPDR auto-clock + `__spi_read_blocking_veneer` fill; UserTerminal path one-shot log; cast fix for `mem_write32` SRAM addrs |
| **Files** | `src/usb.c`, `src/spi.c`, `src/thumb32.c`, `docs/eositis/*` |
| **Tests** | `make -C build bramble bramble_tests` → 322/322 |
| **Run** | `./build/bramble …/megaflash.uf2 -arch m33 -clock 150 -cores 2 -usb-console 5555 -usb-stdio -timeout 90` |
| **Outcome** | Past U2 `U2_MonReset` HardFault (~`0x10006DC4`) and `spi_read_blocking` RAM spin (`0x20002C12`); 90s runs ~157M steps, core1 launches; final PC still invalid (`0x00000024`–`0x0000017E`) — **UserTerminal log not seen**, TCP menu unverified |
| **Blockers** | Late boot memory/PC corruption or multicore FIFO / alarm pool interaction; need hit on `0x10000464` / `0x10005AD0` |
| **Transcript** | `c4c672a1-a61d-45a7-8c50-b3eefb78c27b` |

---

## 2026-06-02 — Session: MegaFlash UART console over TCP

| Field | Detail |
|-------|--------|
| **Request** | Try UART `-uart-console` path for `megaflash.uf2` debug build instead of USB CDC |
| **Actions** | RP2350 `uart_match` @ `0x40070000`; guest bring-up hooks when `-uart-console` active; `__wrap_puts`/`uart_putc` → TCP; skip `multicore_launch` at `main` call site `0x10000318`; netbridge TX pending buffer (4096 B) until client connects; `scripts/run-megaflash-uart-console.sh` |
| **Run** | `./scripts/run-megaflash-uart-console.sh` + `nc localhost 4444` |
| **Outcome** | **Verified:** TCP receives `[u2] init` (9 bytes) from `U2_Init`; mirror shows same on stderr. Guest still high-step after early puts (`__wrap_printf` / `_vfprintf_r` path); full banner/menu not yet on UART |
| **Tests** | 322/322 pass |

---

## 2026-06-02 — Session: UART main banner over TCP

| Field | Detail |
|-------|--------|
| **Request** | Proceed with UART printf bring-up so full MegaFlash banner reaches TCP (not only `[u2] init`) |
| **Actions** | Host `__wrap_printf` / `__wrap_vprintf` formatter on UART path; skip `U2_MonInit` / `u2_reset` call sites; fix chained `main` call-site stubs (same `cpu_step` executes insn at new PC); stub `clock_get_hz` / `spi_get_baudrate` / `CheckPicoW` |
| **Files** | `src/usb.c`, `docs/eositis/UART-CONSOLE.md`, `docs/eositis/SESSION-LOG.md`, `docs/eositis/CHANGELOG.md` |
| **Run** | `./scripts/run-megaflash-uart-console.sh` + `nc localhost 4444` |
| **Outcome** | **Verified:** 323 B TCP output — U2 init + full DEBUG banner (`Megaflash DEBUG Firmware Version 33`, build date, 150 MHz clocks, heap). Guest still faults later (`PC≈0x1E`) before `UserTerminal` |
| **Tests** | 322/322 pass |
| **Transcript** | `c4c672a1-a61d-45a7-8c50-b3eefb78c27b` |

---

## 2026-06-02 — Session: MegaFlash USB CDC console / UserTerminal

| Field | Detail |
|-------|--------|
| **Request** | Continue USB console bring-up; Apple II must stay disconnected for `UserTerminal` |
| **Actions** | USB TCP pending TX buffer; host `__wrap_printf` → CDC TCP; `CheckPicoW=1` + `IsAppleConnected=0` stubs (call site + fn); skip `core0Loop` / `EnableAppleResetInterrupt`; skip `stdio_usb_init` / USB wait loops → `UserTerminal`; reuse UART init shortcuts |
| **Run** | `./scripts/run-megaflash-usb-console.sh -cores 1` + `nc localhost 5555` (no `megaflash-bus.stub`) |
| **Outcome** | **Verified:** 962 B TCP — boot banner + ASCII art + `Main Menu` / `Please Select:`; log `guest reached UserTerminal path @ 0x10005AD0` |
| **Tests** | 322/322 pass; UART banner path still works |
| **Transcript** | `c4c672a1-a61d-45a7-8c50-b3eefb78c27b` |

---

## 2026-06-02 — Session: USB console input fix

| Field | Detail |
|-------|--------|
| **Request** | Diagnose USB CDC TCP input not working at `UserTerminal` menu |
| **Root cause** | Skipping `stdio_usb_init` left `stdio_usb` driver @ `0x200047D4` zeroed; TCP RX went to host DPRAM only, not guest CDC RX fifo |
| **Fix** | Seed stdio USB driver + active driver pointer; push TCP RX into guest `tu_fifo` RX; hook `stdio_usb_in_chars` / `tud_cdc_n_read` to pop fifo |
| **Verified** | Python client: menu 962 B, send `1` → echo `1\n\n` + menu refresh (option 1) |
| **Tests** | 322/322 pass |

---

## 2026-06-02 — Session: USB menu Device Information (key 1)

| Field | Detail |
|-------|--------|
| **Request** | Pressing `1` at `UserTerminal` should show multiline firmware/flash info, not re-send the menu |
| **Root cause** | Hook @ `0x10005AEC` redirected `DeviceInfo` → `PrintBanner`; prior `GetDeviceInfoString` stub was one line only |
| **Fix** | Remove redirect; host-build full device-info buffer (matches `.rodata` layout); stub `PrintAllPartitions` for partition header + emu row |
| **Verified** | TCP `1` → device info, JEDEC lines, partition table, `Press any key to continue...` (no menu repeat) |
| **Tests** | 322/322 pass |
| **Commit** | `2053c69` — fix(console): MegaFlash USB menu Device Information on key 1 |

---

## 2026-06-02 — Session: USB device-info flash size 64MB/chip

| Field | Detail |
|-------|--------|
| **Request** | Increase emulated flash size to 64MB each in USB menu device info |
| **Fix** | `USB_GUEST_EMU_FLASH_CHIP_MB` = 64; total 128MB (2 chips); partition row 64MB |
| **Tests** | 322/322 pass |
| **Commit** | `1823f07` — fix(console): report 64MB per flash chip in USB device info |

---

## 2026-06-02 — Session: USB console virtual serial port (PTY)

| Field | Detail |
|-------|--------|
| **Request** | Expose USB CDC console as virtual serial port for standard terminal/COM programs |
| **Fix** | `openpty` bridge in `usb.c`; `-usb-serial` / `-usb-console pty[:path]`; script env `USB_CONSOLE_PTY=1` |
| **Verified** | Symlink `/tmp/bramble-usb-console` → menu + device info over PTY |
| **Tests** | 322/322 pass |
| **Commit** | `7f9d8f2` — feat(console): USB CDC on virtual serial port (PTY) |

---

## 2026-06-02 — Session: persistent MegaFlash external flash

| Field | Detail |
|-------|--------|
| **Request** | Flash devices should persist between runs; user-configurable mapping |
| **Fix** | `-spi-flash1` / `-spi-flash2` with optional paths; `-spi-flashN-size`; defaults in `flash/` |
| **Note** | Separate from `-flash` (RP2350 on-chip 2MB); this is MegaFlash external SPI |

---

## 2026-06-02 — Session: macOS default PTY for USB console

| Field | Detail |
|-------|--------|
| **Request** | Make USB console usable from macOS terminal/serial programs |
| **Fix** | PTY default on Darwin in run script; connect auto-detects symlink; `open-usb-console-macos.sh` |
| **Commit** | `ce991a0` — fix(console): default USB PTY on macOS for terminal apps |

---

## 2026-06-02 — Session: USB XMODEM upload via macOS serial

| Field | Detail |
|-------|--------|
| **Request** | macOS serial for MegaFlash menu + XMODEM file upload |
| **Fix** | PTY (default on macOS); flash unit stubs; DisableFlashUnitMapping no-op; XMODEM doc (minicom/lrzsz) |
| **Limit** | External SPI flash write during XMODEM still partial in emulator |
| **Commit** | `efc5d1c` — fix(console): enable USB flash unit path for XMODEM upload |

## 2026-06-02 — XMODEM 32MB upload reliability

| Field | Detail |
|-------|--------|
| **Request** | 32MB `.po` XMODEM upload fails mid-transfer (`N????`, Write packet to serial failed, PTY gone) |
| **Actions** | Fixed flash write fall-through (blocks 8+ went to hash not file); clamp copy at 4×128; flush on buffer full; bulk mem read/write; 64KB host RX + PTY drain while XMODEM active; guest hooks on all cores; `TIMEOUT=7200`, `CORES=1` defaults; docs |
| **Outcome** | 262144-byte automated test passes; 32MB needs `TIMEOUT=7200`, `CORES=1`, exclusive PTY |
| **Commit** | `b0af78f` — fix(console): XMODEM flash upload reliability and mmap-backed SPI I/O |

## 2026-06-07 — SPI stubs + flash I/O performance

| Field | Detail |
|-------|--------|
| **Request** | Why SPI shows 1 MHz; JEDEC should match Winbond W25Q*; flash r/w performance awful slow |
| **Actions** | Stub `spi_get_baudrate` → 75 MHz (`SPI_SPEED_FINAL`); JEDEC `0xEF4020` (W25Q512JV) + fix `0x9F` SPI read byte order; mmap-backed flash writes, sequential seek skip, reduced XMODEM PTY poll/drain, bulk block stubs |
| **Tests** | `make -C build bramble bramble_tests`; `./build/bramble_tests` 322/322; 256KB XMODEM ~1s (was ~19s) |
| **Outcome** | Device info and boot banner show 75 MHz / EF4020h; upload path verified |
| **Commit** | `b0af78f` — fix(console): XMODEM flash upload reliability and mmap-backed SPI I/O |

## 2026-06-08 — Host RX throttle for XMODEM

| Field | Detail |
|-------|--------|
| **Request** | XMODEM still breaks with host RX buffer full / dropped data; need 90%/80% transfer throttling |
| **Actions** | Hysteresis throttle on PTY reads (90% on, 80% off); `usb_console_tcp_poll_rx(force)` for active packet reads; removed flash-write PTY drain that prefilled buffer |
| **Tests** | `XMODEM_TEST_BYTES=262144` passes; 256KB verified |
| **Commit** | `9196826` — fix(console): throttle host RX at 90% during XMODEM uploads |

## 2026-06-08 — Full 32MB XMODEM upload

| Field | Detail |
|-------|--------|
| **Request** | 32MB `A2OSX.STABLE.32MB.po` still fails well after 256KB tests |
| **Actions** | 256KB RX buffer; 8KB read-ahead cap; windowed msync; 300s ACK timeout; `scripts/test-32mb-xmodem.sh`; verify uses actual file size (33553920 B) |
| **Tests** | Full image upload ~76s, `65535 blocks received`, `VERIFY OK` vs `flash/spi-flash1.bin` |
| **Commit** | `ac96692` — fix(console): sustain 32MB XMODEM uploads with RX read-ahead cap |

## 2026-06-08 — Apply PROJECT-RULES.md

| Field | Detail |
|-------|--------|
| **Request** | Apply `docs/eositis/PROJECT-RULES.md` (session log, changelog, commit, build/test) |
| **Actions** | Moved Unreleased CHANGELOG bullets into dated commit sections (`37c38a5`–`ac96692`, `5840a76`, `aaf9ee3`); documented `test-32mb-xmodem.sh` in `USB-CONSOLE.md`; fixed session log template comment |
| **Tests** | `make -C build bramble bramble_tests`; `./build/bramble_tests` |
| **Commit** | `8b6f6de` — docs(eositis): apply PROJECT-RULES changelog and session housekeeping |

<!--

| Field | Detail |
|-------|--------|
| **Request** | |
| **Actions** | |
| **Outcome** | |
| **Commit** | `<hash>` — <subject> |

-->

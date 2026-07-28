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

## 2026-07-27 — Apple IIc emulator candidates for MegaFlash link

| Field | Detail |
|-------|--------|
| **Request** | Assess retrotech71 web-a2e for IIc/MegaFlash linking; suggest alternatives if not adaptable |
| **Actions** | Reviewed MegaFlash (IIc memexp/Slinky connector + ROM replace); web-a2e (IIe Enhanced only, MIT, WASM); AppleWin (no IIc); MAME (`apple2c`/`apple2cp` + memexp); Virtual ][ (IIc but limited hooks) |
| **Outcome** | web-a2e not a good fit for MegaFlash; recommend MAME as primary open integration target |
| **Commit** | _(none — advisory only)_ |

## 2026-07-27 — tio console / XMODEM how-to doc

| Field | Detail |
|-------|--------|
| **Request** | Markdown doc for tio console use, triggering XMODEM upload, and exiting tio |
| **Actions** | Added `docs/eositis/TIO-CONSOLE.md`; linked from `README.md` and `USB-CONSOLE.md` |
| **Outcome** | Step-by-step: start Bramble, `tio /tmp/bramble-usb-console`, menu 2/CONFIRM, Ctrl-T x, Ctrl-T q |
| **Commit** | `124252e` — docs(eositis): add tio console and XMODEM upload guide |

## 2026-07-27 — blank tio after MegaFlash UF2 rebuild

| Field | Detail |
|-------|--------|
| **Request** | No output on tio USB console; boot log stopped after CDC/BSS init |
| **Actions** | Rebuilt `megaflash.uf2` shifted symbols (e.g. UserTerminal `0x10005ad0`→`0x10005b00`, BSS size 378036). Updated Bramble-only hook addresses in `src/usb.c`. Fixed macOS PTY: close slave FD after openpty so external clients can read master TX. Did not modify MegaFlash sources |
| **Tests** | `bramble_tests` 322/322; PTY shows `Please Select:`; 8KB XMODEM upload OK |
| **Commit** | `2f20b08` — fix(console): retarget MegaFlash USB hooks and fix macOS PTY TX |

## 2026-06-08 — XMODEM broke after 8KB read-ahead throttle

| Field | Detail |
|-------|--------|
| **Request** | Manual minicom upload dies after block 3 with `host RX throttle ON (8192/262143 bytes)` |
| **Actions** | 8KB read-ahead cap was setting the same `throttled` flag as 90% high-water; release required ~209KB consumption. Only set `throttled` at ≥90% fill; 8KB cap now pauses background PTY reads without the flag |
| **Tests** | `bramble_tests` 322/322; automated full 32MB XMODEM `VERIFY OK` (~65s) |
| **Commit** | `56f230d` — fix(console): do not latch throttle on 8KB XMODEM read-ahead |

## 2026-06-08 — tio XMODEM failures (~1s ACK timeout)

| Field | Detail |
|-------|--------|
| **Request** | tio XMODEM-CRC fails in ~15s; XMODEM-1K runs longer then fails |
| **Actions** | Root cause: tio waits ~1s/ACK vs Python test 120s+; emulator packet handling too slow. Added in-packet CPU turbo (4096 steps), per-step PTY poll during XMODEM, ACK `tcdrain`, PTY TX pending queue, raw mode on slave PTY; removed `-usb-stdio` default from run script; documented tio limits |
| **Tests** | `bramble_tests` 322/322; 256KB + full 32MB automated XMODEM `VERIFY OK` |
| **Commit** | `d2901fd` — fix(console): speed XMODEM ACK path for tio's 1s timeout |

## 2026-07-27 — MAME + Bramble MegaFlash bridge

| Field | Detail |
|-------|--------|
| **Request** | Plan A: MAME apple2c4 + Bramble MegaFlash over TCP; integrated launcher; use local `iic.bin` |
| **Actions** | `-a2bus-bridge` TCP RPC; Lua plugin taps `$C0C0–$C0C3`; `run-megaflash-mame.sh`; docs; gitignore ROM |
| **Tests** | `bramble_tests` 322/322; bridge smoke: Slinky `registers[2]=0xf0`, activation → ID `$96` |
| **Outcome** | Bramble↔MegaFlash bus bridge ready; MAME not installed on this host (`brew install mame` + CHR/keyboard dumps needed for full GUI run) |
| **Commit** | `05a0fc0` — feat(a2bus): bridge MegaFlash slot I/O to MAME over TCP |

## 2026-07-27 — Fix MAME rompath / iic.bin overlay

| Field | Detail |
|-------|--------|
| **Request** | Launcher failed: missing CHR/keyboard/`sc01a`, wrong checksum on staged `iic.bin` |
| **Actions** | Default rompath to Ample (`apple2c.zip` + `votrsc01a.zip`); `;` separator on macOS; Lua overlays `iic.bin`; preflight `-verifyroms` |
| **Outcome** | `apple2c4` romset verifies good against Ample path |
| **Commit** | `98be297` — fix(mame): use Ample romset and overlay iic.bin in Lua |

## 2026-07-28 — MegaFlash ROM not active in MAME

| Field | Detail |
|-------|--------|
| **Request** | PR#4 “unable to start from memory card”; Closed-Apple+Ctrl+Reset no control panel |
| **Cause** | Rompath included Ample; MAME selected CRC-correct stock `3410445b.256` over staged `iic.bin` |
| **Actions** | Stage self-contained `./roms` (CHR/kbd/speech from Ample + `iic.bin` as maincpu); rompath local-only; expect CRC warn |
| **Commit** | `61993d3` — fix(mame): load MegaFlash iic.bin instead of stock ROM4 |

## 2026-07-28 — MegaFlash not found (PR#4 / control panel)

| Field | Detail |
|-------|--------|
| **Request** | Correct ROM boots but MegaFlash not available / not found |
| **Causes** | (1) inject mask `0x1F` dropped CMD data; (2) GPIO+inject dual FIFO; (3) `EE60` ActLed in `DoCommand` misdecoded as `ORN` → `r0=0xFF..` → `MFERR_UNKNOWNCMD` |
| **Actions** | 13-bit inject-only + drain; stop EE60→DP fallthrough; veneer hook; core1-only pump; early core1launch; SPI on launcher |
| **Validation** | MAGIC→ID xor `$FF`; `CMD_GETDEVINFO` status `0`; param signature `0x88 0x74` |
| **Commit** | `661b49e` — fix(megaflash): make a2bus CMD_GETDEVINFO and detect work |

## 2026-07-28 — MegaFlash still not found (read-byte off-by-one)

| Field | Detail |
|-------|--------|
| **Request** | PR#4 / control panel still report MegaFlash not found after `661b49e` |
| **Cause** | Bridge READ returned post-BusLoop shadow; PARAM auto-advance made first `$C0C1` read return `$74` instead of `$88` |
| **Actions** | Peek register before inject+pump on READ; validate `chkmegaflashex`-style sig `0x88 0x74` |
| **Commit** | `aec4569` — fix(megaflash): return pre-cycle bus byte on a2bus READ |

## 2026-07-28 — MegaFlash still not found (MAME socket)

| Field | Detail |
|-------|--------|
| **Request** | Still not found after READ-byte fix |
| **Cause** | Plugin used `emu.file` flags 7 (CREATE); connect fallback listened on 19765 → EADDRINUSE; taps had no socket. Also pluginspath replaced stock plugins. |
| **Actions** | Client-only open + retry; append stock plugins; drop dead TCP clients; validated MAGIC→GETDEVINFO→`$88 $74` under MAME |
| **Commit** | `6caa058` — fix(mame): connect MegaFlash bridge as TCP client |

## 2026-07-28 — MegaFlash still not found (1B instr exit)

| Field | Detail |
|-------|--------|
| **Request** | Interactive MAME still reports MegaFlash not found; log shows connect then `Instruction limit reached (1B)` |
| **Cause** | Without stdin/GDB, Bramble exits after 1B guest steps and tears down the a2bus bridge while MAME is still running |
| **Actions** | Skip 1B limit when `a2bus_bridge_active()`; plugin v0.1.5 installs taps via periodic if reset missed; validated MAGIC→`$88 $74` and 20s post-BusLoop survival |
| **Commit** | `afd9efb` — fix(megaflash): keep a2bus bridge running past 1B steps |

## 2026-07-28 — MegaFlash found by signature but NOFLASH / hang

| Field | Detail |
|-------|--------|
| **Request** | PR#4 “MegaFlash Not Found”; control panel blank hang; tap log shows `$88 $74` then status `$41`/`$45` |
| **Cause** | `$41`=`MFERR_NOFLASH`, `$45`=`MFERR_INVALIDUNIT` — SPI flash/config stubs only ran under `-usb-console`, so unit count was 0 after COLDSTART |
| **Actions** | Surgical a2bus flash stubs (InitFlash/configs/blocks/JEDEC) without full USB hook set; validated GETDEVSTATUS → 4 units, no error |
| **Commit** | `79f0ca3` — fix(mame): stub SPI flash units on a2bus bridge path |

## 2026-07-28 — PR#4 not available / option 7 never starts

| Field | Detail |
|-------|--------|
| **Request** | After flash stubs: GETDEVSTATUS units=4 but GETUNITSTATUS `$45`; PR#4 not available; option 7 never starts |
| **Cause** | (1) RAM `IsValidUnitNum` saw native unit count 0; (2) `CopyMemoryAligned` DMA no-op / wrote `cpu.ram` instead of RP2350 SRAM so LOAD_CPANEL data stayed empty |
| **Actions** | Stub IsValidUnitNum/GetBlockCount; intercept CopyMemoryAligned veneer; guest copy via mem_read8/write8; validated unit `$FFFF` blocks + cpanel data `$A2…` |
| **Commit** | `132d61b` — fix(mame): SmartPort unit status and LOAD_CPANEL copy |

## 2026-07-28 — PR#4 lockup on READBLOCK

| Field | Detail |
|-------|--------|
| **Request** | PR#4 locks for hours; hard to kill; log shows READBLOCK status `$80` then HardFault at `0x1000DF80` (`_exit`) |
| **Cause** | `DoReadBlock` BL to RAM `ReadBlock`; veneer-only stub missed; native SPI/DMA hung BUSY then aborted |
| **Actions** | Stub RAM ReadBlock/WriteBlock; cut pump_steps to 65536; validated READBLOCK status 0 + ProDOS block `01 38…` |
| **Commit** | `3f26e1a` — fix(mame): stub RAM ReadBlock so PR#4 does not hang BUSY |

## 2026-07-28 — Control panel hang (LOAD_CPANEL byte storm)

| Field | Detail |
|-------|--------|
| **Request** | Still hangs when running control panel; tap log reaches GETTIMESTR `$19` then endless `$C0C1` `$A0` / stalls |
| **Cause** | LOAD_CPANEL ≈58 pages × 256 DATA reads; each RPC inject+pumped core1 — glacial under MAME. Ungated host fast path also broke Slinky MAGIC detect |
| **Actions** | Host-side DATA/PARAM/ID/STATUS mirror when ID `$96`/`$69` and not BUSY; adaptive BUSY-clear pump; gate fast path off during Slinky |
| **Validate** | Detect `$88 $74`; 58-page LOAD_CPANEL in ~1.5s (~10 KB/s); `bramble_tests` 322/322 |
| **Commit** | `233ebe4` — fix(mame): host-side DATA/PARAM path so LOAD_CPANEL is interactive |

## 2026-07-28 — Boot from flash volume 1 (A2.DESKTOP)

| Field | Detail |
|-------|--------|
| **Request** | MAME should boot A2Desktop from MegaFlash flash volume 1; still likely broken |
| **Cause** | `flash/spi-flash1.bin` unit 1 already has `A2.DESKTOP`; `GETVOLINFO`/`GetMediumType` hit flash `__TranslateUnitNum_veneer` → Thumb HardFault (`PC=0x5F200010`), leaving BUSY and breaking SmartPort boot naming/status |
| **Actions** | Rewrite only that veneer to SRAM `TranslateUnitNum`; validated GETVOLINFO name `A2.DESKTOP`, GETDIB, 32+ READBLOCK match flash; document no MAME HD — boot via SmartPort |
| **Commit** | `229d66f` — fix(mame): rewrite TranslateUnitNum veneer so flash volume 1 can boot |

## 2026-07-28 — Slot 4 not booting / option 7 stalls

| Field | Detail |
|-------|--------|
| **Request** | MegaFlash not booting (slot 4 default); boot menu option 7 stalls on control panel |
| **Cause** | a2bus pump early-exited before core1 set BUSY on CMD writes; Apple `BIT status / BMI` saw idle and read PARAM while WE_KEY already cleared → `configbyte1=0` → skip slot 4. Same race starved LOAD_CPANEL under MAME |
| **Actions** | Pump until BUSY seen then cleared; stub GetConfigByte1/2 with AUTOBOOT\|ROMDISK defaults; validated COLDSTART `cfg1=0x44`, 58-page cpanel, A2.DESKTOP vol + block0 |
| **Commit** | `c76ea09` — fix(mame): wait for CMD BUSY cycle so COLDSTART keeps slot-4 autoboot |

## 2026-07-28 — Disable MAME NSC; fix option 7 HardFault

| Field | Detail |
|-------|--------|
| **Request** | Disable MAME no-slot clock (time from MegaFlash); control panel option 7 still hangs |
| **Cause** | (1) `apple2c4` hardwires DS1216E; (2) `__CheckWriteEnableKey_veneer` `ldr.w pc,[pc]` misdecoded → `PC=0x5F200004` when `CheckWriteEnableKey(1)` during DriveMapping |
| **Actions** | Fix Thumb-2 LDR literal; rewrite all flash→SRAM F85F veneers; mute NSC on `$C100–$CFFF` in Lua; seed MegaFlash RTC from host; removed local `nvram/apple2c4/nsc` |
| **Validate** | `bramble_tests` 323/323 (incl. LDR-literal veneer) |
| **Commit** | `c0e80a1` — fix(mame): mute NSC and repair ldr.w veneers for option 7 |

<!--

| Field | Detail |
|-------|--------|
| **Request** | |
| **Actions** | |
| **Outcome** | |
| **Commit** | `<hash>` — <subject> |

-->

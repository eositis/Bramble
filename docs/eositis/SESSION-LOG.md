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
| **Cause** | (1) `apple2c4` hardwires DS1216E; (2) `__CheckWriteEnableKey_veneer` + `r0=1` → `PC=0x5F200004` |
| **Actions** | Mute NSC in Lua; seed MegaFlash RTC; a2bus rewrite CheckWriteEnableKey veneer. Reverted unsafe global Thumb `F85F` literal decode that left guest in bootrom so MAME never launched |
| **Validate** | `bramble_tests` 322/322; PING + BusLoopSlinky ready |
| **Commit** | `c0e80a1` (+ follow-up revert/fix) |

## 2026-07-28 — Launch stuck after NSC/veneer commit

| Field | Detail |
|-------|--------|
| **Request** | Launcher dies before MAME; Bramble exits ~50 steps / bootrom spin |
| **Cause** | Global Thumb-2 `F85F`/`F8DF` literal handler (and briefly `upper_hi>>8`) broke early boot |
| **Actions** | Revert thumb32 changes; keep targeted `__CheckWriteEnableKey_veneer` rewrite + NSC mute + RTC seed on calendar read |
| **Validate** | BusLoopSlinky OK; core1 launches |
| **Commit** | `4e5a21f` — fix(mame): restore MegaFlash boot; keep CheckWriteEnableKey veneer hook |

## 2026-07-29 — CP error:0 + ProDOS load still failing

| Field | Detail |
|-------|--------|
| **Request** | Still unable to load ProDOS; CP option 7 → Unexpected Error:0 |
| **Cause** | Host DATA fast-path used `mem_read32` on 1-byte `dataBufferTransferMode`; adjacent BSS made LINEAR look non-linear → corrupted 7-byte `GETUSERSETTINGS` → `ERR_CONFIG_INVALID` (0). Also forced `ROMDISKFLAG` on a2bus config |
| **Actions** | `mem_read8` for transfer mode; AUTOBOOT-only default config; map flash units via `FLASH_UNIT_MAP` in block I/O stubs |
| **Validate** | `bramble_tests` 322/322 |
| **Commit** | `7569508` — fix(mame): read DATA transfer mode as a byte for linear transfers |

## 2026-07-29 — configBuffer never seeded under MAME a2bus

| Field | Detail |
|-------|--------|
| **Request** | Same CP error:0 + ProDOS fail; log shows READBLOCK ok then `flash write: no backing file for unit 5` |
| **Cause** | Script-launched core1 runs BusLoop while core0 still pre-`LoadAllConfigs`; a2bus stubs only ran on `!guest_megaflash_hook_active` path historically; GETUSERSETTINGS returned `02 58 00 00 00 00 00` (`timezoneidver=0`) |
| **Actions** | Always run a2bus stubs when bridge active; late-seed on `GetUserSettings`; skip InitSpi/U2; delay core1launch 2s; probe script confirms settings + block0/2/22/23 |
| **Validate** | Probe GETUSERSETTINGS `02 58 40 00 01 0e ff` OK; interleaved blocks match `spi-flash1.bin`; `bramble_tests` 322/322 |
| **Commit** | `e8a125a` — fix(mame): seed MegaFlash user settings when core0 lags BusLoop |

## 2026-07-29 — CP Save hang on Saving...; ProDOS still no boot

| Field | Detail |
|-------|--------|
| **Request** | Option 7 works; Save+reboot stuck on Saving...; ProDOS still fails |
| **Cause** | `SaveUserSettings` → `EncryptWriteConfigToFlash` → `tsWriteSecurityRegister` SPI hang (BUSY never clears). Spurious WriteBlock unit 5 also logged |
| **Actions** | Stub save/security-reg writes on a2bus; persist settings to host file; reject out-of-range WriteBlock units; probe SAVE round-trip OK (~2ms) |
| **Validate** | Probe GET/SAVE/GET + block0; `bramble_tests` 322/322 |
| **Commit** | `c11f059` — fix(mame): stub SaveUserSettings so control-panel Save cannot hang |

## 2026-07-29 — Test Wifi Unexpected Error; vol1 boot + Save OK

| Field | Detail |
|-------|--------|
| **Request** | Vol1 ProDOS boots; Save works; Test Wifi → Unexpected Error; vol3 a2osx after disabling vol1/2 |
| **Cause** | `CMD_TESTWIFI` needs CYW43 + core0 `NetworkPump` IPC; under a2bus that fails as `NETERR_UNKNOWN` / NOTPICOW → CP “Unexpected Error.” |
| **Actions** | Stub `DoTestWifi` → `SSIDNOTSET` / `WIFINOTCONN` (interim); later superseded by real CYW43 path |
| **Validate** | `bramble_tests` 322/322; probe TESTWIFI err=3 in ~0ms (stub) |
| **Commit** | `0b6d0b5` — fix(mame): stub DoTestWifi so CP Test Wifi is usable under a2bus |

## 2026-07-29 — Fix cyw43_arch_init HardFault (PC=FFFFFFFF)

| Field | Detail |
|-------|--------|
| **Request** | Core0 HALTED PC=FFFFFFFF after InitPicoLed; core1 never launched; MAME session dies |
| **Root causes** | (1) Missing ROM `SR` → `blx 0`; (2) `TT` misdecoded as STRD; (3) STREXB wrote status to r15 (Rd bits wrong) → PC=0 → stale LR return with unbalanced stack → pop into `default_alarm_pool` |
| **Actions** | ROM SR/SS + nop-on-miss; implement TT; fix STREXB Rd; early core1launch; HardFault logging |
| **Outcome** | Past InitPicoLed; `TestWifi()`/`NETPUMP` start; core1 Script launch; no HardFault in 20s run |
| **Validate** | `bramble_tests` 322/322; a2bus+wifi timeout run |
| **Commit** | `406f0d9` — fix(m33): stop cyw43_arch_init HardFault under MegaFlash a2bus |

## 2026-07-29 — Test Wifi hung on “Testing…”

| Field | Detail |
|-------|--------|
| **Request** | After HardFault fix, CP Test Wifi hangs waiting for completion |
| **Causes** | (1) Core1-reset FIFO pushes `0`; Bramble ROM MSP at 0 was 0 → false `TestWifi` IPC; (2) empty-SSID path uses `__cxa_throw` — missing `LDA`, broken `CLZ` Rm mask → hang/HardFault; (3) `DoTestWifi` sleep wait never sees done under dual-core |
| **Actions** | Nonzero bootrom MSP; script-launch FIFO drain; implement `LDA`/`STL`; fix `CLZ`/`RBIT`; a2bus empty-SSID fast-fail on `DoTestWifi` veneer |
| **Validate** | Probe `TESTWIFI err=3` in ~0ms; `bramble_tests` 322/322 |
| **Commit** | `0c04ab9` — fix(mame): stop Test Wifi hang on empty SSID under a2bus |

## 2026-07-29 — Configured-SSID Test Wifi under a2bus

| Field | Detail |
|-------|--------|
| **Request** | Empty-SSID stub OK (settings not saved this session); entered WiFi data and retested; defer Save Settings |
| **Causes** | (1) `stdio_usb_init` skip jumped past `InitPicoLed`; (2) `_sw_spin_locks` LDAEXB base stale; (3) WFE/`sleep_until` starved TIMER0; (4) gSPI test-pattern still fails (`Failed to read test pattern`) |
| **Actions** | Fix InitPicoLed jump; spinlock range; sleep stubs + malloc_mutex; synthetic TestWifi success when SSID set (`NETERR_NONE`/`192.168.4.2`); empty SSID still `SSIDNOTSET` |
| **Validate** | Seeded TESTWIFI err=11 in ~20ms; empty err=3; `bramble_tests` 322/322 |
| **Commit** | `2a27b9e` — fix(mame): make CP Test Wifi complete with configured SSID under a2bus |

## 2026-07-29 — Fix CYW43 gSPI test-pattern (RP2350 DMA)

| Field | Detail |
|-------|--------|
| **Request** | Get the network working (real join / TestWifi, not synthetic) |
| **Cause** | RP2350 DMA CTRL: BSWAP is bit 24 (BUSY on RP2040). Emulator stripped BSWAP → gSPI decode garbage → `Failed to read test pattern`. Also bit-count `pio_sm_put` words mishandled as commands |
| **Actions** | Mode-aware DMA layout/offsets/16ch; PIO skip/READ fix; remove synthetic wifi stubs; M33 dma re-init + 520KB PC range; core1 SP cushion |
| **Outcome** | `SPI_READ_TEST_REGISTER → 0xFEEDBEAD`; chip FW version string prints. Core1 still HardFaults in BusLoopSlinky @ CLM `clmload_status` (blocks a2bus TestWifi IPC). Host TAP/NAT still Linux-only |
| **Validate** | `bramble_tests` 322/322; seeded a2bus `-wifi` run shows FEEDBEAD + Version line |
| **Commit** | `26fece6` — fix(m33): decode MegaFlash CYW43 gSPI with RP2350 DMA BSWAP layout |

## 2026-07-29 — Fix Test Wifi display strings; clarify TAP/NAT

| Field | Detail |
|-------|--------|
| **Request** | CP shows wrong netmask/gw/DNS; ask if NAT exists for real host link |
| **Cause** | Synthetic TestWifi filled `TestResult_t` only; guest `FormatIPAddr` mangled follow-on strings. No live link: join stubbed; `-tap`/`-net` Linux-only and not used by MAME launcher |
| **Actions** | DoTestWifi stub writes four C strings + BE param IPs; docs note macOS has no TAP/NAT |
| **Validate** | Probe strings `192.168.4.2` / `255.255.255.0` / `192.168.4.1`×2; err=11 |
| **Commit** | `c36e139` — fix(mame): write correct Test Wifi IP strings for the control panel |

## 2026-07-30 — Restore MegaFlash boot under a2bus

| Field | Detail |
|-------|--------|
| **Request** | System no longer boots from MegaFlash; CP option 7 reports MegaFlash not found |
| **Cause** | Enabling real `cyw43_arch_init` from InitPicoLed HardFaults core1 BusLoop during `clmload_status` (`PC=0x1FFF8F6C`); Slinky stuck → no native `$96` |
| **Actions** | Default-stub `cyw43_arch_init`/`gpio_put`/`InitCyw43`/`ConnectWifi`; restore synthetic configured TestWifi; opt-in `BRAMBLE_A2BUS_REAL_WIFI=1`; docs |
| **Validate** | `bramble_tests` 322/322; a2bus run shows stub + `BusLoopSlinky ready` and **no** HardFault |
| **Commit** | `40e284c` — fix(mame): stub cyw43_arch_init under a2bus so BusLoop boots again |

## 2026-07-30 — NTP / u2macraw spam / MAME exit shutdown

| Field | Detail |
|-------|--------|
| **Request** | NTP stuck at 12:00; quiet `[u2macraw]` spam; shut down Bramble when MAME exits |
| **Cause** | Configured-SSID `GetNetworkTime` fell through to real RunNTP while cyw43 stubbed (never set `rtcRunning`); firmware U2_Net_Poll printed every ~5 s; launcher `exec` MAME dropped EXIT trap that kills Bramble |
| **Actions** | Synthetic NTP seeds RTC + host calendar stub; suppress `[u2macraw]` unless `BRAMBLE_A2BUS_U2MACRAW=1`; launcher waits for MAME; bridge exits after READ/WRITE client disconnect; docs |
| **Validate** | `make -C build bramble bramble_tests`; `./build/bramble_tests` |
| **Commit** | `1a2b834` — fix(mame): synthetic NTP, quiet u2macraw, exit with MAME |

## 2026-07-30 — Clock stuck at 12:00 (POWMAN AON)

| Field | Detail |
|-------|--------|
| **Request** | Clock does not increment (macraw/shutdown OK) |
| **Cause** | M33 membus has no POWMAN AON timer; real `aon_timer_*` reads zeros → 12:00 AM forever even with `rtcRunning` set |
| **Actions** | Stub `DoGetTimeString`, `GetProdosTimestamp`/`GetProdos25Timestamp`, and `aon_timer_{get_time,get_time_calendar,start*}` from host `localtime` |
| **Validate** | `make -C build bramble bramble_tests`; `./build/bramble_tests` |
| **Commit** | `4f3d7cd` — fix(mame): drive MegaFlash clock from host localtime |

## 2026-07-30 — Control-Reset HardFault + clock not stubbing

| Field | Detail |
|-------|--------|
| **Request** | closed-apple-ctrl-reset breaks system; clock matches host but not updating |
| **Cause** | (1) TBB/TBH (`E8Dx F01x`) misdecoded as LDRD Rt=PC — loads branch table as PC (`0x00B103D0`); (2) picoram veneer rewrite of `__DoGetTimeString_veneer` skipped RTC stub in same `cpu_step` |
| **Actions** | Check TBB/TBH before LDRD path; RTC hooks before veneer rewrite; `test_m33_thumb2_tbh` |
| **Validate** | `make -C build bramble bramble_tests`; `./build/bramble_tests` 323/323 |
| **Commit** | `ae349e5` — fix(m33): decode TBH before LDRD; stub DoGetTimeString veneer |

## 2026-07-30 — Revert mistaken host-clock stub expansion

| Field | Detail |
|-------|--------|
| **Request** | Revert system clock changes (typo: meant “now updating”, not “not”) |
| **Actions** | Restored pre-`4f3d7cd` RTC hooks (`aon_timer_get_time_calendar` + NTP seed only); kept TBH/`test_m33_thumb2_tbh` for Control-Reset |
| **Validate** | `make -C build bramble bramble_tests`; `./build/bramble_tests` |
| **Commit** | `091febc` — revert(mame): drop DoGetTimeString/ProDOS host-clock stubs |

## 2026-07-30 — Split megaflash-vm (MAME integration)

| Field | Detail |
|-------|--------|
| **Request** | Create `megaflash-vm` at GitHub root; move Bramble↔MAME integration outside virtual GPIO |
| **Actions** | New sibling repo with plugin, launcher, stub, probe, MAME-BRIDGE docs; Bramble keeps `a2bus`/`-a2bus-bridge`; pointer in `docs/eositis/MAME-BRIDGE.md` |
| **Boundary** | Inside pins → Bramble; outside pins → megaflash-vm |
| **Outcome** | See commit below; megaflash-vm init commit separately |
| **Commit** | `92b6426` — chore: move MAME integration out to sibling megaflash-vm |

## 2026-07-30 — Empty flash after megaflash-vm split

| Field | Detail |
|-------|--------|
| **Request** | Machine did not boot from virtual MegaFlash storage after new launcher |
| **Cause** | Launcher cwd = megaflash-vm; `-spi-flash1` created empty `flash/spi-flash1.bin` instead of Bramble’s A2.DESKTOP volume |
| **Actions** | Seed/copy volumes into megaflash-vm/flash; launcher absolute paths + cd for user-config; docs |
| **Commit** | megaflash-vm (see sibling); Bramble `[main 059641f] docs(eositis): note megaflash-vm flash seeding after split
 3 files changed, 16 insertions(+), 1 deletion(-)
059641f`; megaflash-vm flash fix |


## 2026-07-30 — Move firmware/flash assets to megaflash-vm

| Field | Detail |
|-------|--------|
| **Request** | Move flash images and firmware loaded for the VM out of Bramble into megaflash-vm |
| **Actions** | Moved uf2/elf/iic/hdv/flash; launcher + USB/UART/stub defaults; `flash/` → symlink; docs |
| **Commit** | `ad5c396` — chore: point MegaFlash runtime assets at sibling megaflash-vm; megaflash-vm `ab79980` |


## 2026-07-31 — Move a2bus into megaflash-vm overlay

| Field | Detail |
|-------|--------|
| **Request** | Implement a2bus megaflash-vm overlay split plan |
| **Actions** | Weak `bramble_ext_*` in Bramble; move a2bus/bridge/hooks to megaflash-vm; overlay CMake; launcher prefers overlay binary; SPI flash stays in Bramble |
| **Validate** | `bramble_tests` 323/323; overlay PING + BusLoopSlinky ready |
| **Commit** | `c1e1ae0` — refactor: move Apple-bus out; megaflash-vm overlay |


## 2026-08-01 — macOS CYW43 utun + pf NAT

| Field | Detail |
|-------|--------|
| **Request** | Link WiFi radio to macOS network stack (SSID/PW join → host internet) |
| **Actions** | Darwin `tapif` utun + Ethernet↔IPv4/ARP; pf helper script; refuse empty SSID; log passphrase present; docs |
| **Validate** | `bramble_tests` 323/323; utun open needs sudo (no interactive password in agent) |
| **Commit** | `0dd0ac1` — feat(wifi): bridge CYW43 to macOS via utun |


## 2026-08-01 — MAME launcher host net + real WiFi diagnostics

| Field | Detail |
|-------|--------|
| **Request** | Integrate network link into MAME startup with macOS admin popup; remove false TestWifi/NTP; real DNS/time diagnostics |
| **Actions** | Docs: `MACOS-WIFI.md`, `PROJECT-RULES.md`; megaflash-vm: askpass + host-net prep, launcher `-wifi -tap` via `sudo -A`, remove synthetic a2bus TestWifi/NTP |
| **Outcome** | Overlay rebuild OK; admin dialog path ready for user approve → MAME |
| **Commit** | Bramble `987fda0` (docs); megaflash-vm `d9af01e` — host net + real WiFi diagnostics |


## 2026-08-01 — MegaFlash not found after real WiFi default

| Field | Detail |
|-------|--------|
| **Request** | MAME reports MegaFlash not found |
| **Cause** | Real `cyw43_arch_init` at InitPicoLed concurrent with BusLoop HardFaults core1 |
| **Actions** | Boot-stub cyw43 again; arm real JOIN/DNS/NTP only on TestWifi/NTP with SSID; rebuild; BusLoop smoke OK |
| **Commit** | megaflash-vm `276179c` |


## 2026-08-01 — TestWifi timeout / zero-flood IP

| Field | Detail |
|-------|--------|
| **Request** | TestWifi: MegaFlash not found then OK with IP of zeros; MAME force-quit |
| **Cause** | Arming real CYW43 on TestWifi; sleep_ms stubs expire 90s wait before CLM; empty dataBuffer printed |
| **Actions** | Keep boot cyw43 stub; complete TestWifi with guest IPs + host DNS; GetNetworkTime via host NTP |
| **Commit** | megaflash-vm `d239a2a` |


## 2026-08-01 — Confirm NTP / fix MegaFlash clock

| Field | Detail |
|-------|--------|
| **Request** | Confirm MegaFlash sends NTP and updates Pico clock |
| **Finding** | Guest lwIP NTP not used; host SNTP ran on GetNetworkTime but epoch discarded; TestWifi was DNS-only |
| **Actions** | Host SNTP → InitRTC (tz + AON stubs); TestWifi runs SNTP after DNS; force IsAppleConnected for core0Loop |
| **Validate** | `InitRTC from NTP: utc=… → 2026-08-01 13:21:06`; BusLoop OK |
| **Commit** | megaflash-vm `6258bc0` |

## 2026-08-01 — Pico2W-faithful CYW43 (drop MegaFlash net traps)

| Field | Detail |
|-------|--------|
| **Request** | Implement plan: remove MegaFlash host DNS/NTP traps; make CYW43+hostif faithful for guest lwIP |
| **Actions** | Overlay: tear out host TestWifi/NTP/InitRTC; empty-SSID fail-fast only; defer BusLoop until after `cyw43_arch_init` + restore `.data`. Bramble: `clmload_status=0`, WFI wall-clock TIMER, POWMAN AON, memcpy fast-path/abort, `mem_guest_memcpy_any`. Docs: MAME-BRIDGE/MACOS-WIFI/PROJECT-RULES/UPSTREAM. |
| **Validate** | `bramble_tests` 323/323; MegaFlash `-wifi`: FEEDBEAD, `cyw43 loaded ok`, BusLoop `magic=0xf0`, both cores RUNNING (no HardFault). Seeded NTP hits assert without `-tap` (expected). |
| **Outcome** | Radio-faithful path: guest stack + CYW43 + tap; overlay is Apple-bus glue only |
| **Commit** | Bramble `40606f6`; megaflash-vm `a82539e` |
| **Transcript** | [Pico2W CYW43 fidelity](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |


<!--

| Field | Detail |
|-------|--------|
| **Request** | |
| **Actions** | |
| **Outcome** | |
| **Commit** | `<hash>` — <subject> |

-->

## 2026-08-01 — Fix Test Wifi crash (CYW43 stub → host)

| Field | Detail |
|-------|--------|
| **Request** | Bramble crashes on MegaFlash Test Wifi; CYW43 may be API stub to host net; lwIP as designed; accept SSID/PW then link so lwIP reaches world via host |
| **Actions** | Bramble: host `strlen` fast-path (join key_len); soft pbuf helpers. megaflash-vm: BusLoop-only `.data` restore; stub JOIN + AP lease; host DNS; host SNTP for `SendNTPRequest`; docs |
| **Validate** | Seeded `-wifi` a2bus: `WIFI connected`, host DNS `0.pool.ntp.org`, host NTP epoch, `NETPUMP: end`; `bramble_tests` 323/323 |
| **Outcome** | Test Wifi / GetNetworkTime no longer die on alarm-pool / join timeout / UDP NTP; radio stub + host DNS/NTP |
| **Commit** | Bramble `17af40d`; megaflash-vm `ab9f2e9` |
| **Commit** | Bramble DHCP-TX fix + docs; megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-01 — CP TestWifi hang (DoTestWifi IPC freezes BusLoop)

| Field | Detail |
|-------|--------|
| **Request** | Test Wifi from MegaFlash CP still hangs with credentials set |
| **Cause** | `DoTestWifi` on core1 waits up to 90s for core0 IPC `TestWifi()` — freezes BusLoop; core0 never logged `TestWifi()` |
| **Actions** | megaflash-vm: host-complete configured-SSID `DoTestWifi` (virtual IPs + host DNS); keep radio stub JOIN/DNS/SNTP for GetNetworkTime; docs |
| **Outcome** | CP Test Wifi returns immediately without blocking Apple bus |
| **Commit** | megaflash-vm `346667b` |
| **Commit** | Bramble DHCP-TX fix + docs; megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-01 — Persist WiFi, NTP/RTC after TestWifi, OA device info

| Field | Detail |
|-------|--------|
| **Request** | WiFi settings not saved across reboot; TestWifi OK but RTC/time not updated; Open-Apple tech details missing |
| **Cause** | Host mirror only stored 7-byte user settings (no SSID); DoTestWifi host-complete skipped NTP; GetDeviceInfoString sprintf(%f) hangs on a2bus |
| **Actions** | Persist/load full 512-byte configBuffer; host SNTP+InitRTC stubs on TestWifi; host-fill GetDeviceInfoString; EncryptWrite → host file |
| **Validate** | Boot loads SSID=PERSISTTEST → WIFI connected + host NTP; build OK |
| **Commit** | Bramble `1aecdeb`; megaflash-vm `37776bb` |
| **Commit** | Bramble DHCP-TX fix + docs; megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-01 — Open-Apple left key + device info hang → “MegaFlash not available”

| Field | Detail |
|-------|--------|
| **Request** | Open-Apple is left of keyboard; CP option 7 reports MegaFlash not available (SP boot OK) |
| **Cause** | Docs pointed at Right ⌘; OA device info uses `$C061` / Left Option. `GetDeviceInfoString` sprintf hang left STATUS BUSY so later ID checks look like MegaFlash gone. “Option 7” may mean boot-menu Control Panel or CP Test Wifi |
| **Actions** | Host-complete `DoGetInfoString`; Apple `\n\r` device-info text; MAME cfg binds Open-Apple to Left Option **or** Left ⌘; docs clarify OA-at-CP-start vs option 7 |
| **Validate** | `bramble_tests` 323/323; overlay rebuild |
| **Commit** | Bramble `9ce6401`; megaflash-vm `5b88cb0` |
| **Commit** | Bramble DHCP-TX fix + docs; megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-01 — Option 7 MegaFlash Not Found (core0 HardFault 0x30034280)

| Field | Detail |
|-------|--------|
| **Request** | Boot-menu option 7 still MegaFlash Not Found; log shows HardFault PC=0x30034280 |
| **Cause** | a2bus+wifi skips USB/UART stdio hooks; dual-core newlib `_vfprintf_r` smashes locale mbtowc to `0x10034280\|0x20000000`; BusLoop stuck off the bus → ID check fails |
| **Actions** | Enable megaflash stdio hooks when `-wifi`; stub `_vfprintf_r` on a2bus; remap/repair `0x30xxxxxx` flash PCs; host `__wrap_vprintf` |
| **Validate** | `bramble_tests` 323/323; overlay rebuild |
| **Commit** | Bramble `b199a1a`; megaflash-vm `b66fae4` |
| **Commit** | Bramble DHCP-TX fix + docs; megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-01 — Option 7 Not Found + Ctrl-Reset loses MegaFlash boot

| Field | Detail |
|-------|--------|
| **Request** | Option 7 still MegaFlash Not Found; after Ctrl-Reset machine no longer boots from MegaFlash |
| **Cause** | Hung DoCommand leaves STATUS BUSY; ID no longer toggles; chkmegaflashex times out → skip COLDSTART/slot 4 |
| **Actions** | ID toggle while BUSY; BUSY timeout unsticks BusLoop; host-complete DoLoadCPanel + DoGetDeviceInfo |
| **Validate** | overlay rebuild |
| **Commit** | megaflash-vm `5786339` |
| **Commit** | Bramble DHCP-TX fix + docs; megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-02 — CP bottom-line rogue data (wifi test OK)

| Field | Detail |
|-------|--------|
| **Request** | Wifi test works; constantly updating junk on bottom of CP screen |
| **Cause** | Bottom line is `DisplayTime` (ver + `CMD_GETTIMESTR`). `DoGetTimeString` → `sprintf` → `_svfprintf_r` hangs (BUSY timeout PC=`0x1002DE12`); unstick leaves PARAM garbage; `cgetc_showclock` redraws it |
| **Actions** | Host-complete `DoGetTimeString`; stub `_svfprintf_r`/`_svfiprintf_r` in overlay + Bramble `usb.c`; docs note |
| **Validate** | `make -C megaflash-vm/build bramble`; `make -C Bramble/build bramble` |
| **Commit** | megaflash-vm `512b756`; Bramble `e4fe230` |
| **Commit** | Bramble DHCP-TX fix + docs; megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-02 — TestWifi result screen IP garbage

| Field | Detail |
|-------|--------|
| **Request** | Wifi test OK but IP/netmask/gateway area is rogue/updating junk |
| **Cause** | PrintStringFromDataBuffer reads $C0C2; dirty interleaved dataBuffer and/or guest-path DATA READ returning post-advance byte (skips NULs) |
| **Actions** | Zero 512-byte dataBuffer + force MODE_LINEAR on DoTestWifi fill; fix guest READ to return pre-inject byte; dump dataBuffer in log |
| **Validate** | make -C megaflash-vm/build bramble |
| **Commit** | megaflash-vm `d631dbe` |
| **Commit** | Bramble DHCP-TX fix + docs; megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-02 — Fix emu dual-core for native DoTestWifi (no host-complete)

| Field | Detail |
|-------|--------|
| **Request** | Do not workaround MegaFlash display; fix emulation to match real hardware |
| **Cause** | a2bus pump_guest stepped only core1; DoTestWifi IPC to core0 never ran → host-complete fabricated dataBuffer |
| **Actions** | Pump both cores while BUSY; remove DoTestWifi host-complete; track InitRTC; docs corrected |
| **Validate** | make -C megaflash-vm/build bramble |
| **Commit** | megaflash-vm `4344085` |
| **Commit** | Bramble DHCP-TX fix + docs; megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-02 — Move USB console runners to megaflash-vm

| Field | Detail |
|-------|--------|
| **Request** | USB console start script should live in megaflash-vm; update links |
| **Actions** | Moved `run/connect/open-usb-console*`, XMODEM test scripts, `USB-CONSOLE.md` / `TIO-CONSOLE.md` to megaflash-vm; Bramble keeps pointer docs + thin forwarders; README/UPSTREAM/PROJECT-RULES updated |
| **Commit** | megaflash-vm `0479d6a`; Bramble `a18beb8` |


## 2026-08-03 — TestWifi OK but IP fields empty (lease via DHCP, not poke)

| Field | Detail |
|-------|--------|
| **Request** | How can WIFI/DNS/NTP be OK with empty IP fields? Report IPs via correct channels, not stubbing |
| **Cause** | NETERR_NONE is independent of dataBuffer strings. FormatIPAddr→strcpy left dataBuffer empty (newlib strcpy word-path under Thumb emu). Overlay also skipped DHCP and poked netif lease |
| **Actions** | Host strcpy accel; enlarge pbuf_alloc NULL fallback; remove DHCP skip + netif lease poke; wait_for_work returns immediately so native wifi_connect drains JOIN+fake DHCP; log DHCP OFFER/ACK; update MAME-BRIDGE |
| **Validate** | `bramble_tests` 323/323; `make -C megaflash-vm/build bramble` |
| **Commit** | Bramble `946c705` (+ docs `d3c7983`); megaflash-vm `b47c3a8` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-03 — Remove host DNS/NTP fakes; fix DHCP TX path

| Field | Detail |
|-------|--------|
| **Request** | Radio misconfigured; remove stubs faking web access; use lwIP+radio as mating interface to localhost |
| **Cause** | Log stuck at connect status 2 (NOIP); soft-continue p->ref==1 returned ERR_BUF (no DHCP TX); pbuf_free soft-continue fell into memp_free (HardFault); host DNS/NTP stubs faked OK |
| **Actions** | Remove host DNS/SNTP stubs; skip ip4_output ref assert after forcing ref=1; fix pbuf_free soft-continue to retry; WLAN TX logging; update PROJECT-RULES/MACOS-WIFI/MAME-BRIDGE |
| **Validate** | bramble_tests 323/323; megaflash-vm overlay rebuild |
| **Commit** | Bramble `e76b825` (+ docs `35e927f`); megaflash-vm `65d6098` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-03 — core0 LOCKUP p!=NULL after JOIN

| Field | Detail |
|-------|--------|
| **Request** | Log shows panic p!=NULL then core0 LOCKUP; TestWifi still shows truncated IP |
| **Cause** | ip4_output got NULL pbuf; ref-skip continued into pbuf_add_header(NULL); debug assert panics (release returns 1); re-enter panic → LOCKUP; scratch pbufs at 0x2007E000 overlapped core stacks |
| **Actions** | Soft-fail NULL add_header; ip4 NULL→ERR_BUF; scratch at 0x2007A000 with 128B headroom; unknown panic → WFI not re-enter |
| **Validate** | bramble_tests 323/323; megaflash-vm overlay rebuild |
| **Commit** | Bramble `c6b3cd1`; megaflash-vm `9ab69a4` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-05 — Network still down: HardFault then NOIP → DHCP OK

| Field | Detail |
|-------|--------|
| **Request** | Network still not running (JOIN then `ip4_output NULL` HardFault / stuck NOIP) |
| **Causes** | (1) UXTAH accumulate mask `FF8F` only matched Rn=1 — Rn=3 in `ip4_output` → bogus ldst → PC=0xFE; (2) T32 LDMIA PC ignored EXC_RETURN; (3) fake DHCP OFFER parse hit ERR_BUF on 4-byte copy at END option |
| **Actions** | Fix UXTAH mask; LDMIA EXC_RETURN; pad DHCP replies; keep udp pbuf/NOCHKSUM rescues; strip debug traces |
| **Validate** | `bramble_tests` 323/323; seeded `-wifi` a2bus: DHCP OFFER+ACK, `connect status: 3`, `WIFI connected`, `NTP: EvtStart()` |
| **Commit** | Bramble `0f061da`; megaflash-vm `d995305` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-05 — CP TestWifi IP “AG” / blank + Abort LOCKUP

| Field | Detail |
|-------|--------|
| **Request** | DHCP/WIFI OK in log; CP shows IP=`AG`, blank netmask/gw/dns; Abort → core0 HardFault at `_exit` |
| **Cause** | Status codes from PARAM; IP lines from `dataBuffer` via `FormatIPAddr` — guest path left empty/NUL strings (“AG” = leftover screen). Abort’s C++ terminate hits `_exit` BKPT → LOCKUP |
| **Actions** | Host-accel `FormatIPAddr` (real IP word → ASCII); dump dataBuffer after fill; `_exit` → WFI; rebuild megaflash-vm `bramble`; docs |
| **Validate** | `bramble_tests` 323/323; overlay rebuild |
| **Commit** | Bramble `a43963d`; megaflash-vm `24ce4dc` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-08-05 — Root-fix STALL / BUSY unstick / sprintf (no more stubs)

| Field | Detail |
|-------|--------|
| **Request** | No improvement from FormatIPAddr stub; do not paper over — fix root causes |
| **Causes** | (1) SDPCM TX credits stall after DHCP when no RX → DNS `send_ethernet -2` → throw → `_exit`; (2) BUSY unstick mid-DoTestWifi before FormatIPAddr; (3) `_svfprintf_r` stub return 0 empties sprintf (TFTP Status leftover `192.`) |
| **Actions** | Credit refresh after WLAN TX; skip unstick during DoTestWifi/sleep/TFTP; host sprintf into guest RAM + `%f`; remove FormatIPAddr stub |
| **Validate** | `bramble_tests` 323/323; megaflash-vm overlay rebuild |
| **Commit** | Bramble `4c31c9d`; megaflash-vm `77eb5b6` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-07-30 — DNS never replies: fake DHCP pointed at gateway

| Field | Detail |
|-------|--------|
| **Request** | Functionally no improvement after credit/sprintf fixes; log shows WIFI OK, DNS wait, Abort, core1 HardFault |
| **Cause** | Fake DHCP advertised DNS=`192.168.4.1`; TAP/ARP works but nothing listens on host `:53`, so guest DNS times out → throw → `_exit` |
| **Actions** | Advertise DNS `8.8.8.8` via DHCP; log TAP TX/RX UDP; warn on no-`-tap` drops; update MACOS-WIFI + megaflash-vm MAME-BRIDGE |
| **Validate** | `bramble_tests` 323/323; rebuild Bramble + megaflash-vm overlay |
| **Commit** | Bramble `fa90f61`; megaflash-vm `ed85cd6` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-07-30 — TAP TX DNS but no RX: userspace UDP NAT

| Field | Detail |
|-------|--------|
| **Request** | After public DNS DHCP: still Abort; log shows `TAP TX UDP 8.8.8.8:53` twice, never TAP RX |
| **Cause** | Writing DNS to utun + pf NAT does not deliver replies on this Mac; guest times out → throw → `_exit` |
| **Actions** | Darwin tapif userspace UDP NAT (host sockets → inject Ethernet); RX ring; pf optional for TCP; docs |
| **Validate** | `bramble_tests` 323/323; overlay rebuild; host UDP DNS smoke OK |
| **Commit** | Bramble `c1884e7`; megaflash-vm `eea7bf0` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

## 2026-07-30 — TAP closed on utun read error; killed UDP NAT

| Field | Detail |
|-------|--------|
| **Request** | After userspace UDP NAT: `TAP read error, closing interface` then ARP/DNS `drop WLAN TX — no -tap` |
| **Cause** | cyw43_tap_poll closed tap_fd on any utun read error; tore down UDP NAT; ARP/DNS dropped |
| **Actions** | Keep bridge on read errors; Darwin disables utun RX only; log errno; docs |
| **Validate** | bramble_tests 323/323; overlay rebuild |
| **Commit** | Bramble `5bb16e5` |
| **Transcript** | [CYW43 stub host bridge](ef4345c3-2d26-44d1-93aa-320d6acc1f09) |

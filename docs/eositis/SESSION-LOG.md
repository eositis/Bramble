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

## YYYY-MM-DD — Session: <title>

| Field | Detail |
|-------|--------|
| **Request** | |
| **Actions** | |
| **Outcome** | |
| **Commit** | `<hash>` — <subject> |

-->

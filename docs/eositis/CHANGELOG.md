# Change log (eositis fork)

Baseline: **clone from `origin/main`** (upstream Bramble v0.45.0, RP2350 M33/RISC-V emulator).  
Scope: local commits on `main` after clone.

---

## Unreleased

### IRQ / Thumb-2 (2026-06-03)

| Change | Reason |
|--------|--------|
| `t32_ldst_multiple`: correct P/U/W/L; route `E912`/`Rt==Rt2` as LDM/STM not LDRD | `ldmdb r2,{r0,r1,r2}` in `irq_add_shared_handler` corrupted handler table |
| `cpu_step`: `pc = cpu.r[15] & ~1` before fetch | Misaligned PC left guest in IRQ path executing 16-bit halves of 32-bit insns @ `0x1000ADE5` |
| `t32_bl`: branch target `& ~1` | Consistent even-PC convention |
| `test_m33_thumb2_ldmdb` | Regression for `E912 0007` |

**Known blocker:** `_vfprintf_r` locale loop @ ~`0x1002F312` after IRQ fix; early `*** PANIC ***` still on stderr.

### USB stdio / RP2350 bootrom (2026-06-03 session)

| Change | Reason |
|--------|--------|
| `rom_get_sys_info` + RP2350 ROM header (`rom_apply_rp2350_header`) + lookup intercept @ `0x0200` | Pre-main `unique_id.c` assert (`rc == 4`) blocked all boot before USB menu |
| Guest stdio hooks: skip `__wrap_printf` by LR, bypass `__wrap_puts` to TCP, force `stdio_usb_connected` when CDC synced | newlib `_vfprintf_r` locale loop hangs under partial VFP |
| RP2350 ADC @ `0x400A0000`; ADC ch0–3 → not Pico W when USB console active | Wrong Pico W detect path |
| Minimal VFP in `thumb32.c` | Float formats in `GetDeviceInfoString` / printf |
| `a2bus`: no-op `a2phi` while USB console TCP active | Stub must not fake Apple online during USB menu test |
| `scripts/run-megaflash-usb-console.sh`, `USB-CONSOLE.md` updates | Document no-stub path for USB menu |

**Commit:** `aaf9ee3` — fix(megaflash): RP2350 bootrom sys_info and USB guest stdio hooks

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

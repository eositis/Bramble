# Change log (eositis fork)

Baseline: **clone from `origin/main`** (upstream Bramble v0.45.0, RP2350 M33/RISC-V emulator).  
Scope: local commits on `main` after clone.

---

## 2026-06-02 — MegaFlash M33 bring-up (see git log for hash)

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

## Unreleased

*(none)*

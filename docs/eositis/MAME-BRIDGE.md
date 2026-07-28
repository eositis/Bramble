# MAME + Bramble MegaFlash bridge

Link MAME’s Apple //c (rev 4, `apple2c4`) to MegaFlash running under Bramble. Slot‑4 soft‑switches `$C0C0–$C0C3` are forwarded over TCP to Bramble’s Apple-bus injector.

## Prerequisites

1. **Build Bramble:** `cmake -B build && make -C build bramble bramble_tests`
2. **MegaFlash UF2/ELF:** default `../MegaFlash/pico/pico2_debug/megaflash.{uf2,elf}`
3. **MegaFlash IIc ROM:** place `iic.bin` (32 KiB) at the repo root (gitignored)
4. **MAME:** `brew install mame` (or set `MAME=/path/to/mame`)
5. **Stock companion dumps** (CHR, keyboard, Votrax `sc01a.bin`). On macOS with Ample, the launcher copies them from
   `~/Library/Application Support/Ample/roms` into `./roms/` and stages **`iic.bin` as `3410445b.256`**.

**Important:** rompath is **local `./roms` only**. If Ample stays on the rompath, MAME prefers the CRC-matching stock `3410445b.256` and silently ignores MegaFlash’s ROM — you get stock Slinky (“unable to start from memory card”) and no MegaFlash boot menu.

Expect a **WRONG CHECKSUM** warning for `3410445b.256`; that means MegaFlash ROM is loaded.

## Run (integrated)

```bash
./scripts/run-megaflash-mame.sh
```

What it does:

1. Starts Bramble with `-arch m33 -clock 150 -cores 2 -a2bus-bridge 19765` and `scripts/megaflash-mame.stub` (PHI0 + core1 launch).
2. Waits for a TCP `PING` on `127.0.0.1:19765`.
3. Starts `mame apple2c4` with `-plugin megaflash_bridge` (Lua taps on `$C0C0–$C0C3`).

**Do not** pass `-ramsize` above the default **128K**. Extra RAM enables MAME’s built-in Slinky and conflicts with MegaFlash.

**Do not** combine with `-usb-console` on the Bramble side for this mode (Apple online suppresses the USB UserTerminal).

The launcher sets `-pluginspath` to `scripts/mame_plugins` **plus** the stock MAME plugins directory (needs `boot.lua`). It also passes `-plugins -plugin megaflash_bridge`.

The Lua plugin must open the bridge socket with **READ|WRITE only** (no CREATE). With CREATE, MAME tries to listen on the same port if connect fails and hits `Address already in use` while Bramble holds the port — MegaFlash then appears missing.

## Environment

| Variable | Default | Meaning |
|----------|---------|---------|
| `BRAMBLE_A2BUS_PORT` | `19765` | TCP port (Bramble listen + MAME plugin) |
| `MEGAFLASH_UF2` | `../MegaFlash/pico/pico2_debug/megaflash.uf2` | Guest firmware |
| `MEGAFLASH_ELF` | sibling `.elf` | Resolves `registers` BSS address |
| `IIC_BIN` | `./iic.bin` | MegaFlash-patched system ROM |
| `MAME_ROMPATH` | `./roms` (staged) | Must **not** include Ample if you want MegaFlash maincpu |
| `AMPLE_ROMS` | `~/Library/Application Support/Ample/roms` | Source for CHR/keyboard/speech staging only |
| `BRAMBLE_IIC_BIN` | set by launcher to `IIC_BIN` | Optional Lua re-overlay path |
| `MAME` | `mame` | Emulator binary |
| `TIMEOUT` | unset (none) | Bramble `-timeout` seconds |

## Protocol (Bramble `-a2bus-bridge`)

Client (MAME plugin) → server (Bramble):

| Op | Bytes | Meaning |
|----|-------|---------|
| `0x00` | op | PING |
| `0x01` | op | PHI0 pulse |
| `0x02` | op, nibble | Inject READ `$C0C0+nibble`; return **pre-cycle** register shadow (bus byte Apple sees) |
| `0x03` | op, nibble, data | Inject WRITE |
| `0x04` | op, nibble | PEEK register shadow (no bus inject) |

Reply: `status` (0 = ok), `data`.

Firmware boots in **Slinky** mode (`registers[2] == 0xf0`). MegaFlash ROM (or the activation read sequence `$C0C2,$C0C0,$C0C0,$C0C3,$C0C1`) switches to native mode; then `$C0C3` ID is **`$96`** (reads toggle with `~` per MegaFlash).

## Smoke test

1. Launcher reaches “BusLoopSlinky ready”.
2. In MAME with `iic.bin`, boot far enough for MegaFlash cold-start / activation.
3. `$C0C3` should show MegaFlash ID behavior (`$96` / `$69` alternating on successive reads).

CRC: expect **WRONG CHECKSUM** for `3410445b.256` when MegaFlash `iic.bin` is staged. If verifyroms is “good” with no warning, MAME is still using stock ROM4.

## Files

- [`src/a2bus_bridge.c`](../../src/a2bus_bridge.c) — TCP bridge
- [`scripts/mame_plugins/megaflash_bridge/`](../../scripts/mame_plugins/megaflash_bridge/) — MAME plugin
- [`scripts/run-megaflash-mame.sh`](../../scripts/run-megaflash-mame.sh) — launcher
- [`scripts/megaflash-mame.stub`](../../scripts/megaflash-mame.stub) — PHI / core1 bring-up

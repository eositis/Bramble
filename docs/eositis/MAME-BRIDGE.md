# MAME + Bramble MegaFlash bridge

**Moved.** The MAME plugin, launcher, bring-up stub, operator guide, **guest firmware**, and **SPI flash volumes** live in the sibling project:

**[megaflash-vm](../../../megaflash-vm/)** → [`docs/MAME-BRIDGE.md`](../../../megaflash-vm/docs/MAME-BRIDGE.md)

Boundary:

- **Bramble** — virtual GPIO / Apple-bus pins, `-a2bus-bridge` TCP endpoint, guest MegaFlash stubs
- **megaflash-vm** — everything outside those pins (MAME, Lua bridge, orchestration, `firmware/`, `flash/`, `iic.bin`)

`flash/` in this tree is a **symlink** to `../megaflash-vm/flash` so USB-console / relative user-config paths share the same volumes.

Quick start:

```bash
../megaflash-vm/scripts/run-megaflash-mame.sh
```

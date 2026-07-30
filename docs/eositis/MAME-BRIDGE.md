# MAME + Bramble MegaFlash bridge

**Moved.** The MAME plugin, launcher, bring-up stub, and operator guide now live in the sibling project:

**[megaflash-vm](../../../megaflash-vm/)** → [`docs/MAME-BRIDGE.md`](../../../megaflash-vm/docs/MAME-BRIDGE.md)

Boundary:

- **Bramble** — virtual GPIO / Apple-bus pins, `-a2bus-bridge` TCP endpoint, guest MegaFlash stubs
- **megaflash-vm** — everything outside those pins (MAME, Lua bridge, orchestration)

Quick start from that repo:

```bash
../megaflash-vm/scripts/run-megaflash-mame.sh
```

SPI volumes live under `megaflash-vm/flash/` (seeded from this tree’s `flash/` when empty).

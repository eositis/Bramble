# eositis project documentation

Personal working notes and process rules for the **Bramble** fork used to run **MegaFlash** (`megaflash.uf2`) on RP2350 M33.

| File | Purpose |
|------|---------|
| [SESSION-LOG.md](SESSION-LOG.md) | Chronological record of agent/user sessions and outcomes |
| [CHANGELOG.md](CHANGELOG.md) | Code changes since clone from `origin/main`, with rationale |
| [PROJECT-RULES.md](PROJECT-RULES.md) | Mandatory workflow rules (logging, changelog, commits) |
| [UART-CONSOLE.md](UART-CONSOLE.md) | Bidirectional UART debug via TCP (`-uart-console`) |
| [USB-CONSOLE.md](USB-CONSOLE.md) | Pointer → sibling **megaflash-vm** (USB CDC UserTerminal) |
| [TIO-CONSOLE.md](TIO-CONSOLE.md) | Pointer → megaflash-vm tio / XMODEM notes |
| [MACOS-WIFI.md](MACOS-WIFI.md) | CYW43 `-wifi -tap` on macOS (utun + pf NAT) |
| [MAME-BRIDGE.md](MAME-BRIDGE.md) | Pointer → sibling **megaflash-vm** (MAME + Bramble MegaFlash integration) |

Cursor agents should follow `.cursor/rules/eositis-project.mdc` on every task in this repo.

# macOS CYW43 host networking

Bramble’s `-wifi` emulates the Pico W CYW43 radio. With `-tap` / `-net` on macOS, WLAN Ethernet frames are bridged through a **utun** interface so guest IP traffic can reach the host stack and (with pf NAT) the internet.

## Guest radio semantics

| Step | Behavior |
|------|----------|
| SSID | `WLC_SET_SSID` — empty SSID refused (no link-up); non-empty joins synthetically |
| Password | `WLC_SET_WSEC_PMK` — accepted and logged as “provided”; not validated against a real AP |
| DHCP | In-emulator: guest `192.168.4.2/24`, gateway/DNS `192.168.4.1` |
| Host | Virtual gateway only — does **not** join your Mac’s Wi‑Fi with the guest SSID |

## Quick start

```bash
# Optional: enable NAT so 192.168.4.0/24 reaches the internet
sudo ./scripts/macos-cyw43-pf-nat.sh enable

# Run Pico W firmware (or MegaFlash when a2bus real WiFi is safe)
./bramble firmware.uf2 -wifi -tap bramble0 -arch m33   # -arch as needed
```

`-tap` on macOS opens the next `utunN` (the name argument is ignored for allocation). Bramble configures `192.168.4.1` ↔ peer `192.168.4.2` via `ifconfig` (may require the same sudo session as `-tap` privilege escalation).

Disable NAT later:

```bash
sudo ./scripts/macos-cyw43-pf-nat.sh disable
```

## How it works

```
Guest lwIP ──SDPCM/Ethernet──► cyw43.c (DHCP local; data → tapif_write)
                                      │
                         Ethernet↔IPv4 + ARP for 192.168.4.1
                                      ▼
                                   utunN @ 192.168.4.1
                                      ▼
                              pf NAT → real iface → internet
```

Linux still uses a real TAP + iptables/nft; the CYW43 call sites are unchanged.

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `utun connect failed` / `ifconfig failed` | Run under sudo (Bramble auto-escalates for `-tap`) |
| Guest gets DHCP but no internet | `sudo ./scripts/macos-cyw43-pf-nat.sh status` — enable if empty |
| MegaFlash MAME Test Wifi still synthetic | Overlay stubs `cyw43_arch_init` until BusLoop-safe; use stock `-wifi -tap` to test hostif |

## Slirp fallback

If pf is undesirable, a userspace NAT/slirp path can replace utun+pf later behind the same `tapif_*` API. Not implemented yet.

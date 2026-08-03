# macOS CYW43 host networking

Bramble’s `-wifi` emulates the Pico W CYW43 radio. With `-tap` / `-net` on macOS, WLAN Ethernet frames are bridged through a **utun** interface so guest IP traffic can reach the host stack and (with pf NAT) the internet.

## Model (radio stub → host)

```
Guest lwIP / cyw43_arch  ──gSPI──►  Bramble cyw43.c (fake radio)  ──frames──►  tapif (utun)
                                                                              │
                                                                     pf NAT → host DNS/NTP/Internet
```

CYW43 is an API-shaped stub: accept SSID/password, synthesize association, and bridge Ethernet so guest lwIP talks to the world through the host. Full chip fidelity (CLM quirks, advanced radio features) can be filled in later.

| Step | Behavior |
|------|----------|
| SSID | `WLC_SET_SSID` — empty refused; non-empty joins synthetically |
| Password | `WLC_SET_WSEC_PMK` — accepted/logged; not validated against a real AP |
| Addressing | Guest `192.168.4.2/24`, gateway/DNS `192.168.4.1` via Bramble fake DHCP on the WLAN data path (guest `dhcp_start` → gSPI TX → OFFER/ACK RX) |
| Host | Virtual gateway only — does **not** join your Mac’s Wi‑Fi with the guest SSID |
| a2bus DNS/NTP/TFTP | Same path as DHCP: guest lwIP UDP/TCP through CYW43 to TAP/utun. No host-side DNS/SNTP completion inside the Pico. |

## Quick start (stock Bramble)

```bash
# Optional: enable NAT so 192.168.4.0/24 reaches the internet
sudo ./scripts/macos-cyw43-pf-nat.sh enable

# Run Pico W firmware
./bramble firmware.uf2 -wifi -tap bramble0 -arch m33   # -arch as needed
```

`-tap` on macOS opens the next `utunN` (the name argument is ignored for allocation). Bramble configures `192.168.4.1` ↔ peer `192.168.4.2` via `ifconfig` (requires the same elevated session as `-tap`).

## MegaFlash + MAME

From **megaflash-vm**, `scripts/run-megaflash-mame.sh` integrates host net into startup (`-wifi -tap` + askpass). Guest Test Wifi / NTP go through CYW43 + this bridge after `cyw43_arch_init` completes and BusLoop is launched. See megaflash-vm `docs/MAME-BRIDGE.md`.

Disable NAT later:

```bash
sudo ./scripts/macos-cyw43-pf-nat.sh disable
```

## Dual-core note

`cyw43_arch_init` on core0 while BusLoop runs on core1 can HardFault the bus loop. MegaFlash overlay defers BusLoop until radio init finishes and restores **only BusLoop RAM code** from flash (not all of `.data` — a full reload zeros `default_alarm_pool` and crashes NETPUMP/TestWifi). Stock dual-core Pico W firmware should keep long CYW43 bring-up off the same PIO/RAM as a critical peer loop, or rely on Bramble’s WFI wall-clock TIMER so sleeps complete.

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `utun connect failed` / `ifconfig failed` | Approve the admin dialog; Bramble must run elevated for `-tap` |
| Guest gets DHCP but no internet | `sudo ./scripts/macos-cyw43-pf-nat.sh status` |
| BusLoop dies / MegaFlash not found | Rebuild overlay; ensure BusLoop launches **after** `cyw43 loaded ok`. Emergency: `BRAMBLE_A2BUS_STUB_WIFI=1` |
| Admin dialog cancelled | Re-run launcher; or `NO_HOST_NET=1` for radio-only |

## Slirp fallback

If pf is undesirable, a userspace NAT/slirp path can replace utun+pf later behind the same `tapif_*` API. Not implemented yet.

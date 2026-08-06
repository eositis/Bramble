# macOS CYW43 host networking

Bramble’s `-wifi` emulates the Pico W CYW43 radio. With `-tap` / `-net` on macOS, WLAN Ethernet frames are bridged so guest IP traffic can reach the internet through the host.

## Model (radio stub → host)

```
Guest lwIP / cyw43_arch  ──gSPI──►  Bramble cyw43.c (fake radio)
                                         │
                    ┌────────────────────┴────────────────────┐
                    │ UDP (DNS/NTP/TFTP): userspace NAT       │
                    │   host UDP sockets ↔ Ethernet inject    │
                    │ TCP/ICMP (optional): utun + pf NAT      │
                    └─────────────────────────────────────────┘
```

CYW43 is an API-shaped stub: accept SSID/password, synthesize association, and bridge Ethernet so guest lwIP talks to the world through the host. Full chip fidelity (CLM quirks, advanced radio features) can be filled in later.

| Step | Behavior |
|------|----------|
| SSID | `WLC_SET_SSID` — empty refused; non-empty joins synthetically |
| Password | `WLC_SET_WSEC_PMK` — accepted/logged; not validated against a real AP |
| Addressing | Guest `192.168.4.2/24`, gateway `192.168.4.1`, DNS **`8.8.8.8`** via Bramble fake DHCP |
| UDP path | **Userspace NAT** in `tapif.c` — guest UDP payloads are sent on host sockets; replies are injected as WLAN RX. Does **not** require pf. |
| TCP/ICMP | Best-effort via utun + optional `scripts/macos-cyw43-pf-nat.sh` |
| Host | Virtual gateway only — does **not** join your Mac’s Wi‑Fi with the guest SSID |
| a2bus DNS/NTP/TFTP | Guest lwIP owns the protocols; Bramble only forwards packets. No host-side DNS/SNTP completion inside the Pico. |

## Quick start (stock Bramble)

```bash
# UDP DNS/NTP work without pf. Optional for TCP:
# sudo ./scripts/macos-cyw43-pf-nat.sh enable

./bramble firmware.uf2 -wifi -tap bramble0 -arch m33
```

`-tap` on macOS opens the next `utunN` (the name argument is ignored for allocation). Bramble configures `192.168.4.1` ↔ peer `192.168.4.2` via `ifconfig` when elevated (UDP NAT still works if ifconfig fails).

## MegaFlash + MAME

From **megaflash-vm**, `scripts/run-megaflash-mame.sh` integrates host net into startup (`-wifi -tap` + askpass). Guest Test Wifi / NTP go through CYW43 + this bridge after `cyw43_arch_init` completes and BusLoop is launched. See megaflash-vm `docs/MAME-BRIDGE.md`.

## Dual-core note

`cyw43_arch_init` on core0 while BusLoop runs on core1 can HardFault the bus loop. MegaFlash overlay defers BusLoop until radio init finishes and restores **only BusLoop RAM code** from flash (not all of `.data` — a full reload zeros `default_alarm_pool` and crashes NETPUMP/TestWifi). Stock dual-core Pico W firmware should keep long CYW43 bring-up off the same PIO/RAM as a critical peer loop, or rely on Bramble’s WFI wall-clock TIMER so sleeps complete.

## Troubleshooting

| Symptom | Check |
|---------|--------|
| `utun connect failed` | Approve the admin dialog if using elevated launcher |
| `TAP read error, closing interface` then `drop WLAN TX` | Fixed in current tree — do not close bridge on utun read errors (kills UDP NAT). Rebuild. |
| DHCP OK, ARP/DNS dropped | Expect `[TAP] UDP NAT →` / `←` and `[CYW43] TAP RX UDP`. At worst: `utun read: … continuing with UDP NAT only`. |
| DHCP still says `dns 192.168.4.1` | Rebuild — older builds advertised the gateway as DNS |
| BusLoop dies / MegaFlash not found | Rebuild overlay; ensure BusLoop launches **after** `cyw43 loaded ok`. Emergency: `BRAMBLE_A2BUS_STUB_WIFI=1` |
| Admin dialog cancelled | Re-run launcher; or `NO_HOST_NET=1` for radio-only |

## pf NAT (optional)

Kernel pf NAT is **optional** on macOS. UDP (DNS/NTP/TFTP) uses userspace sockets. Enable pf only if you need guest TCP through the tunnel:

```bash
sudo ./scripts/macos-cyw43-pf-nat.sh enable
sudo ./scripts/macos-cyw43-pf-nat.sh status
sudo ./scripts/macos-cyw43-pf-nat.sh disable
```

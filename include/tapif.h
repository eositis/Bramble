#ifndef TAPIF_H
#define TAPIF_H

#include <stdint.h>

/*
 * Host virtual interface for CYW43 WLAN Ethernet frames.
 *
 * Linux: TAP (L2).  macOS: utun (L3) with Ethernet↔IPv4 adaptation.
 * Subnet: guest 192.168.4.2 / host 192.168.4.1 (matches CYW43 DHCP).
 *
 * Usage: ./bramble firmware.uf2 -wifi -tap <ifname>
 * macOS: UDP (DNS/NTP/TFTP) uses userspace NAT; pf optional for TCP.
 */

/* Open host interface. Returns fd or -1 on error. */
int tapif_open(const char *name);

/* Close host interface */
void tapif_close(int fd);

/* Read an Ethernet frame (non-blocking). Returns bytes, 0 if none, -1 on error. */
int tapif_read(int fd, uint8_t *buf, int maxlen);

/* Write an Ethernet frame. Returns bytes written or -1 on error. */
int tapif_write(int fd, const uint8_t *buf, int len);

/* Service UDP NAT / TFTP host-ACK without requiring a TAP Ethernet read.
 * Safe to call often; used when guest radio is asleep but TFTP still runs. */
void tapif_service(int fd);

/*
 * Optional TFTP DATA fast-path (a2bus): if set, called after host-ACK for each
 * DATA/OACK. Return 1 if the payload was applied on the host (do not deliver
 * through guest CYW43); 0 to use the normal guest delivery path.
 */
typedef int (*tapif_tftp_data_apply_fn)(const uint8_t *payload, int len,
                                        uint16_t server_port);
void tapif_set_tftp_data_apply(tapif_tftp_data_apply_fn fn);

#endif /* TAPIF_H */

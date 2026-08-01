/*
 * Host network interface for CYW43 WiFi bridge.
 *
 * Linux: TAP (/dev/net/tun) carrying raw Ethernet frames + iptables/nft NAT.
 * macOS: utun (L3) with Ethernet↔IPv4 adaptation + optional pf NAT helper.
 *
 * Virtual subnet (matches CYW43 fake DHCP):
 *   Guest  192.168.4.2
 *   Host   192.168.4.1
 *
 * Usage: ./bramble firmware.uf2 -wifi -tap <ifname>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "tapif.h"

#define TAP_HOST_IP     "192.168.4.1"
#define TAP_PEER_IP     "192.168.4.2"
#define TAP_SUBNET      "192.168.4.0/24"
#define TAP_NETMASK     "255.255.255.0"

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

#ifdef __linux__
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

static char tap_ifname[IFNAMSIZ];
static int  tap_forwarding_was_enabled = 0;
static char tap_outgoing_iface[IFNAMSIZ];

static int detect_outgoing_iface(char *out, size_t out_sz) {
    FILE *f = popen("ip route show default 2>/dev/null | awk '/default/{print $5; exit}'", "r");
    if (!f) return -1;
    if (fgets(out, (int)out_sz, f) == NULL) {
        pclose(f);
        return -1;
    }
    pclose(f);
    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
    return (out[0] != '\0') ? 0 : -1;
}

static int get_ip_forwarding(void) {
    FILE *f = fopen("/proc/sys/net/ipv4/ip_forward", "r");
    if (!f) return 0;
    int val = 0;
    if (fscanf(f, "%d", &val) != 1) val = 0;
    fclose(f);
    return val;
}

static void set_ip_forwarding(int enable) {
    FILE *f = fopen("/proc/sys/net/ipv4/ip_forward", "w");
    if (f) {
        fprintf(f, "%d\n", enable ? 1 : 0);
        fclose(f);
    }
}

static void copy_ifname(char *dst, const char *src) {
    memset(dst, 0, IFNAMSIZ);
    size_t len = strlen(src);
    if (len >= IFNAMSIZ) len = IFNAMSIZ - 1;
    memcpy(dst, src, len);
}

static int configure_interface(const char *ifname) {
    struct ifreq ifr;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    memset(&ifr, 0, sizeof(ifr));
    copy_ifname(ifr.ifr_name, ifname);
    struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, TAP_HOST_IP, &addr->sin_addr);
    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        fprintf(stderr, "[TAP] Failed to set IP address: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    inet_pton(AF_INET, TAP_NETMASK, &addr->sin_addr);
    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) {
        fprintf(stderr, "[TAP] Failed to set netmask: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    copy_ifname(ifr.ifr_name, ifname);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        close(sock);
        return -1;
    }
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        fprintf(stderr, "[TAP] Failed to bring interface up: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
}

static int setup_nat(void) {
    if (detect_outgoing_iface(tap_outgoing_iface, sizeof(tap_outgoing_iface)) < 0) {
        fprintf(stderr, "[TAP] No default route found — NAT not configured\n");
        fprintf(stderr, "[TAP] Local subnet 192.168.4.0/24 will still work\n");
        return 0;
    }

    tap_forwarding_was_enabled = get_ip_forwarding();
    if (!tap_forwarding_was_enabled) {
        set_ip_forwarding(1);
        fprintf(stderr, "[TAP] Enabled IP forwarding\n");
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "iptables -t nat -A POSTROUTING -s %s -o %s -j MASQUERADE 2>/dev/null",
             TAP_SUBNET, tap_outgoing_iface);
    if (run_cmd(cmd) != 0) {
        snprintf(cmd, sizeof(cmd),
                 "nft add rule nat postrouting oifname \"%s\" ip saddr %s masquerade 2>/dev/null",
                 tap_outgoing_iface, TAP_SUBNET);
        if (run_cmd(cmd) != 0) {
            fprintf(stderr, "[TAP] NAT setup failed (no iptables or nft) — local only\n");
            return 0;
        }
    }

    fprintf(stderr, "[TAP] NAT: %s → %s (masquerade)\n", TAP_SUBNET, tap_outgoing_iface);
    return 0;
}

static void teardown_nat(void) {
    if (tap_outgoing_iface[0] == '\0') return;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "iptables -t nat -D POSTROUTING -s %s -o %s -j MASQUERADE 2>/dev/null",
             TAP_SUBNET, tap_outgoing_iface);
    run_cmd(cmd);

    if (!tap_forwarding_was_enabled) {
        set_ip_forwarding(0);
    }
}

int tapif_open(const char *name) {
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[TAP] Failed to open /dev/net/tun: %s\n", strerror(errno));
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (name && name[0])
        strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        fprintf(stderr, "[TAP] TUNSETIFF failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    copy_ifname(tap_ifname, ifr.ifr_name);
    fprintf(stderr, "[TAP] Interface '%s' created (fd=%d)\n", ifr.ifr_name, fd);

    if (configure_interface(ifr.ifr_name) == 0) {
        fprintf(stderr, "[TAP] Configured %s as %s/%s\n", ifr.ifr_name, TAP_HOST_IP, TAP_NETMASK);
    } else {
        fprintf(stderr, "[TAP] Auto-configuration failed. Manual setup:\n");
        fprintf(stderr, "[TAP]   sudo ip addr add %s/24 dev %s\n", TAP_HOST_IP, ifr.ifr_name);
        fprintf(stderr, "[TAP]   sudo ip link set %s up\n", ifr.ifr_name);
    }

    setup_nat();
    return fd;
}

void tapif_close(int fd) {
    if (fd >= 0) {
        teardown_nat();
        fprintf(stderr, "[TAP] Closed interface '%s'\n", tap_ifname);
        close(fd);
    }
}

int tapif_read(int fd, uint8_t *buf, int maxlen) {
    if (fd < 0) return -1;
    if (maxlen > 1518) maxlen = 1518;
    ssize_t n = read(fd, buf, (size_t)maxlen);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return (int)n;
}

int tapif_write(int fd, const uint8_t *buf, int len) {
    if (fd < 0) return -1;
    int total = 0;
    while (total < len) {
        ssize_t n = write(fd, buf + total, (size_t)(len - total));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return total > 0 ? total : 0;
            }
            return -1;
        }
        total += (int)n;
    }
    return total;
}

#elif defined(__APPLE__)

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#include <sys/wait.h>
#include <net/if.h>
#include <net/if_utun.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>

/* CYW43 guest default MAC / fake BSSID used as gateway L2 address */
static const uint8_t k_guest_mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
static const uint8_t k_gw_mac[6]    = {0x02, 0xCA, 0xFE, 0xBA, 0xBE, 0x01};
static const uint8_t k_host_ip[4]   = {192, 168, 4, 1};

static char utun_ifname[IFNAMSIZ];
static uint8_t pending_eth[1518];
static int pending_eth_len;
static int darwin_active;

static void queue_pending_eth(const uint8_t *frame, int len) {
    if (len <= 0 || len > (int)sizeof(pending_eth)) return;
    memcpy(pending_eth, frame, (size_t)len);
    pending_eth_len = len;
}

/* Answer ARP who-has 192.168.4.1 — return 1 if handled. */
static int darwin_handle_arp(const uint8_t *eth, int len) {
    if (len < 42) return 0;
    if (eth[12] != 0x08 || eth[13] != 0x06) return 0; /* not ARP */

    const uint8_t *arp = eth + 14;
    uint16_t op = ((uint16_t)arp[6] << 8) | arp[7];
    if (op != 1) return 0; /* not request */

    const uint8_t *tpa = arp + 24; /* target protocol address */
    if (memcmp(tpa, k_host_ip, 4) != 0) return 0;

    uint8_t reply[42];
    memset(reply, 0, sizeof(reply));
    /* Ethernet: dst=requester, src=gw */
    memcpy(reply, eth + 6, 6);
    memcpy(reply + 6, k_gw_mac, 6);
    reply[12] = 0x08;
    reply[13] = 0x06;
    /* ARP reply */
    reply[14] = 0x00; reply[15] = 0x01; /* HTYPE Ethernet */
    reply[16] = 0x08; reply[17] = 0x00; /* PTYPE IPv4 */
    reply[18] = 6;    reply[19] = 4;    /* HLEN PLEN */
    reply[20] = 0x00; reply[21] = 0x02; /* OPER reply */
    memcpy(reply + 22, k_gw_mac, 6);    /* SHA */
    memcpy(reply + 28, k_host_ip, 4);   /* SPA */
    memcpy(reply + 32, eth + 6, 6);     /* THA = requester MAC */
    memcpy(reply + 38, arp + 14, 4);    /* TPA = requester IP (SPA of req) */

    queue_pending_eth(reply, 42);
    return 1;
}

static int darwin_configure_utun(const char *ifname) {
    char cmd[256];
    /* Point-to-point: local 192.168.4.1, peer 192.168.4.2 */
    snprintf(cmd, sizeof(cmd),
             "ifconfig %s inet %s %s netmask %s up 2>/dev/null",
             ifname, TAP_HOST_IP, TAP_PEER_IP, TAP_NETMASK);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "[TAP] ifconfig failed (need root?). Manual:\n");
        fprintf(stderr, "[TAP]   sudo ifconfig %s inet %s %s netmask %s up\n",
                ifname, TAP_HOST_IP, TAP_PEER_IP, TAP_NETMASK);
        return -1;
    }
    fprintf(stderr, "[TAP] Configured %s as %s (peer %s)\n",
            ifname, TAP_HOST_IP, TAP_PEER_IP);
    return 0;
}

static void darwin_try_pf_nat(void) {
    /* Prefer project helper script if present beside cwd or via BRAMBLE_PF_NAT. */
    const char *script = getenv("BRAMBLE_PF_NAT");
    char local[512];
    if (!script || !script[0]) {
        /* Try relative to common layouts */
        static const char *candidates[] = {
            "scripts/macos-cyw43-pf-nat.sh",
            "../Bramble/scripts/macos-cyw43-pf-nat.sh",
            NULL
        };
        for (int i = 0; candidates[i]; i++) {
            if (access(candidates[i], X_OK) == 0 || access(candidates[i], R_OK) == 0) {
                script = candidates[i];
                break;
            }
        }
    }
    if (script && script[0]) {
        snprintf(local, sizeof(local), "sh '%s' enable 2>/dev/null", script);
        if (run_cmd(local) == 0) {
            fprintf(stderr, "[TAP] pf NAT enabled via %s\n", script);
            return;
        }
    }
    fprintf(stderr, "[TAP] pf NAT not auto-enabled. For internet access run:\n");
    fprintf(stderr, "[TAP]   sudo scripts/macos-cyw43-pf-nat.sh enable\n");
    fprintf(stderr, "[TAP] Guest can still reach host %s on the utun.\n", TAP_HOST_IP);
}

int tapif_open(const char *name) {
    (void)name; /* utun unit is assigned by the kernel */

    int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) {
        fprintf(stderr, "[TAP] utun socket failed: %s\n", strerror(errno));
        return -1;
    }

    struct ctl_info info;
    memset(&info, 0, sizeof(info));
    strncpy(info.ctl_name, UTUN_CONTROL_NAME, sizeof(info.ctl_name) - 1);
    if (ioctl(fd, CTLIOCGINFO, &info) < 0) {
        fprintf(stderr, "[TAP] CTLIOCGINFO failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    struct sockaddr_ctl addr;
    memset(&addr, 0, sizeof(addr));
    addr.sc_len = sizeof(addr);
    addr.sc_family = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    addr.sc_id = info.ctl_id;
    addr.sc_unit = 0; /* kernel picks next utunN */

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[TAP] utun connect failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    char ifname[IFNAMSIZ];
    socklen_t ifname_len = sizeof(ifname);
    if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname, &ifname_len) < 0) {
        fprintf(stderr, "[TAP] UTUN_OPT_IFNAME failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    memset(utun_ifname, 0, sizeof(utun_ifname));
    strncpy(utun_ifname, ifname, sizeof(utun_ifname) - 1);
    pending_eth_len = 0;
    darwin_active = 1;

    fprintf(stderr, "[TAP] macOS utun '%s' opened (fd=%d) — L3 bridge for CYW43\n",
            utun_ifname, fd);

    darwin_configure_utun(utun_ifname);
    darwin_try_pf_nat();
    return fd;
}

void tapif_close(int fd) {
    if (fd >= 0) {
        if (darwin_active && utun_ifname[0]) {
            fprintf(stderr, "[TAP] Closed utun '%s'\n", utun_ifname);
        }
        pending_eth_len = 0;
        darwin_active = 0;
        utun_ifname[0] = '\0';
        close(fd);
    }
}

int tapif_read(int fd, uint8_t *buf, int maxlen) {
    if (fd < 0) return -1;

    if (pending_eth_len > 0) {
        int n = pending_eth_len;
        if (n > maxlen) n = maxlen;
        memcpy(buf, pending_eth, (size_t)n);
        pending_eth_len = 0;
        return n;
    }

    /* utun: 4-byte AF family (network order) + IP packet */
    uint8_t raw[4 + 1500];
    ssize_t n = read(fd, raw, sizeof(raw));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    if (n <= 4) return 0;

    uint32_t af;
    memcpy(&af, raw, 4);
    af = ntohl(af);
    if (af != AF_INET) return 0; /* ignore IPv6 for now */

    int ip_len = (int)n - 4;
    if (14 + ip_len > maxlen) return 0;
    if (ip_len < 20) return 0;

    /* Build Ethernet frame for guest */
    memcpy(buf, k_guest_mac, 6);
    memcpy(buf + 6, k_gw_mac, 6);
    buf[12] = 0x08;
    buf[13] = 0x00;
    memcpy(buf + 14, raw + 4, (size_t)ip_len);
    return 14 + ip_len;
}

int tapif_write(int fd, const uint8_t *buf, int len) {
    if (fd < 0) return -1;
    if (len < 14) return -1;

    if (darwin_handle_arp(buf, len)) {
        return len; /* consumed; reply queued for tapif_read */
    }

    /* IPv4 Ethernet → utun */
    if (buf[12] != 0x08 || buf[13] != 0x00) {
        return len; /* drop non-IPv4 quietly */
    }

    int ip_len = len - 14;
    if (ip_len < 20) return -1;

    uint8_t raw[4 + 1500];
    if (ip_len > 1500) ip_len = 1500;
    uint32_t af = htonl(AF_INET);
    memcpy(raw, &af, 4);
    memcpy(raw + 4, buf + 14, (size_t)ip_len);

    int total = 0;
    int want = 4 + ip_len;
    while (total < want) {
        ssize_t n = write(fd, raw + total, (size_t)(want - total));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return total > 4 ? len : 0;
            }
            return -1;
        }
        total += (int)n;
    }
    return len;
}

#else /* !linux && !apple */

int tapif_open(const char *name) {
    (void)name;
    fprintf(stderr, "[TAP] Host network bridge not supported on this platform\n");
    return -1;
}

void tapif_close(int fd) {
    if (fd >= 0) close(fd);
}

int tapif_read(int fd, uint8_t *buf, int maxlen) {
    (void)fd; (void)buf; (void)maxlen;
    return -1;
}

int tapif_write(int fd, const uint8_t *buf, int len) {
    (void)fd; (void)buf; (void)len;
    return -1;
}

#endif

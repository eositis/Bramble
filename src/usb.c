/*
 * RP2040 USB Controller with Host Enumeration Simulation
 *
 * Simulates a USB host performing device enumeration:
 *   1. Bus reset
 *   2. GET_DEVICE_DESCRIPTOR (8 bytes)
 *   3. SET_ADDRESS
 *   4. GET_DEVICE_DESCRIPTOR (full)
 *   5. GET_CONFIGURATION_DESCRIPTOR
 *   6. SET_CONFIGURATION
 *   7. CDC SET_LINE_CODING + SET_CONTROL_LINE_STATE
 *
 * After enumeration, CDC bulk endpoints are bridged to stdout/stdin.
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "usb.h"
#include "nvic.h"
#include "devtools.h"

usb_state_t usb_state;
int usb_cdc_stdout_enabled = 0;
int usb_enum_trace_enabled = 0;
int usb_stdio_prefer_usb = 0;

/* ========================================================================
 * USB CDC ↔ TCP console (-usb-console)
 * ======================================================================== */

typedef struct {
    int listen_fd;
    int client_fd;
    int port;
} usb_tcp_bridge_t;

static usb_tcp_bridge_t usb_tcp;
static int usb_tcp_logged_ready;

static void usb_tcp_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static void usb_tcp_set_nodelay(int fd) {
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

void usb_console_tcp_set_port(int port) {
    usb_tcp.port = port;
}

int usb_console_tcp_active(void) {
    return usb_tcp.port > 0;
}

int usb_console_tcp_init(void) {
    usb_tcp.listen_fd = -1;
    usb_tcp.client_fd = -1;
    if (usb_tcp.port <= 0) {
        return 0;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[USB] TCP console: socket failed: %s\n", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)usb_tcp.port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 1) < 0) {
        fprintf(stderr, "[USB] TCP console: listen on %d failed: %s\n",
                usb_tcp.port, strerror(errno));
        close(fd);
        return -1;
    }

    usb_tcp_set_nonblock(fd);
    usb_tcp.listen_fd = fd;
    fprintf(stderr, "[USB] CDC console listening on TCP port %d (nc localhost %d)\n",
            usb_tcp.port, usb_tcp.port);
    return 0;
}

void usb_console_tcp_cleanup(void) {
    if (usb_tcp.client_fd >= 0) {
        close(usb_tcp.client_fd);
        usb_tcp.client_fd = -1;
    }
    if (usb_tcp.listen_fd >= 0) {
        close(usb_tcp.listen_fd);
        usb_tcp.listen_fd = -1;
    }
}

void usb_console_tcp_poll(void) {
    if (usb_tcp.port <= 0) {
        return;
    }

    if (usb_tcp.listen_fd >= 0 && usb_tcp.client_fd < 0) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int cfd = accept(usb_tcp.listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (cfd >= 0) {
            usb_tcp_set_nonblock(cfd);
            usb_tcp_set_nodelay(cfd);
            usb_tcp.client_fd = cfd;
            fprintf(stderr, "[USB] CDC console client connected from %s:%d\n",
                    inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        }
    }

    if (usb_tcp.client_fd < 0) {
        return;
    }

    struct pollfd pfd = { .fd = usb_tcp.client_fd, .events = POLLIN };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        uint8_t buf[64];
        ssize_t n = read(usb_tcp.client_fd, buf, sizeof(buf));
        if (n > 0) {
            for (ssize_t j = 0; j < n; j++) {
                (void)usb_cdc_rx_push(buf[j]);
            }
        } else if (n == 0) {
            fprintf(stderr, "[USB] CDC console client disconnected\n");
            close(usb_tcp.client_fd);
            usb_tcp.client_fd = -1;
        }
    }
}

static void usb_console_tcp_tx(const uint8_t *data, int len) {
    if (usb_tcp.client_fd < 0 || len <= 0) {
        return;
    }
    ssize_t off = 0;
    while (off < len) {
        ssize_t n = write(usb_tcp.client_fd, data + off, (size_t)(len - off));
        if (n > 0) {
            off += n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        fprintf(stderr, "[USB] CDC console write error, disconnecting\n");
        close(usb_tcp.client_fd);
        usb_tcp.client_fd = -1;
        break;
    }
}

static const char *usb_enum_state_name(usb_enum_state_t state) {
    switch (state) {
    case USB_ENUM_DISABLED: return "DISABLED";
    case USB_ENUM_WAIT_PULLUP: return "WAIT_PULLUP";
    case USB_ENUM_BUS_RESET: return "BUS_RESET";
    case USB_ENUM_GET_DESC_8: return "GET_DESC_8";
    case USB_ENUM_SET_ADDRESS: return "SET_ADDRESS";
    case USB_ENUM_GET_DESC_FULL: return "GET_DESC_FULL";
    case USB_ENUM_GET_CONFIG_SHORT: return "GET_CONFIG_SHORT";
    case USB_ENUM_GET_CONFIG_FULL: return "GET_CONFIG_FULL";
    case USB_ENUM_SET_CONFIG: return "SET_CONFIG";
    case USB_ENUM_CDC_SET_LINE_CODING: return "CDC_SET_LINE_CODING";
    case USB_ENUM_CDC_SET_CTRL_LINE: return "CDC_SET_CTRL_LINE";
    case USB_ENUM_ACTIVE: return "ACTIVE";
    default: return "?";
    }
}

static const char *usb_ctrl_state_name(usb_ctrl_state_t state) {
    switch (state) {
    case USB_CTRL_IDLE: return "IDLE";
    case USB_CTRL_SETUP_SENT: return "SETUP_SENT";
    case USB_CTRL_WAIT_DATA_IN: return "WAIT_DATA_IN";
    case USB_CTRL_WAIT_STATUS_OUT: return "WAIT_STATUS_OUT";
    case USB_CTRL_WAIT_DATA_OUT: return "WAIT_DATA_OUT";
    case USB_CTRL_WAIT_STATUS_IN: return "WAIT_STATUS_IN";
    case USB_CTRL_DONE: return "DONE";
    default: return "?";
    }
}

static void usb_trace_status(const char *note) {
    if (!usb_enum_trace_enabled) {
        return;
    }

    fprintf(stderr,
            "[USB-trace] %s enum=%s ctrl=%s delay=%d main_en=%d pullup=%d "
            "setup_rec=%d cdc_in=%d cdc_out=%d stdio_active=%d stdout_bridge=%d\n",
            note ? note : "status",
            usb_enum_state_name(usb_state.enum_state),
            usb_ctrl_state_name(usb_state.ctrl_state),
            usb_state.delay,
            (usb_state.main_ctrl & USB_MAIN_CTRL_EN) ? 1 : 0,
            (usb_state.sie_ctrl & USB_SIE_CTRL_PULLUP_EN) ? 1 : 0,
            (usb_state.sie_status & USB_SIE_SETUP_REC) ? 1 : 0,
            usb_state.cdc_in_ep,
            usb_state.cdc_out_ep,
            usb_cdc_stdio_active(),
            usb_cdc_stdout_enabled);
}

/* ========================================================================
 * DPRAM Helpers
 * ======================================================================== */

static uint32_t dpram_read32(uint32_t off) {
    uint32_t val;
    memcpy(&val, &usb_state.dpram[off], 4);
    return val;
}

static void dpram_write32(uint32_t off, uint32_t val) {
    memcpy(&usb_state.dpram[off], &val, 4);
}

/* ========================================================================
 * Interrupt Handling
 * ======================================================================== */

static uint32_t usb_compute_intr(void) {
    uint32_t intr = 0;
    if (usb_state.buff_status)
        intr |= USB_INTR_BUFF_STATUS;
    if (usb_state.sie_status & USB_SIE_BUS_RESET)
        intr |= USB_INTR_BUS_RESET;
    if (usb_state.sie_status & USB_SIE_SETUP_REC)
        intr |= USB_INTR_SETUP_REQ;
    if (usb_state.sie_status & USB_SIE_TRANS_COMPLETE)
        intr |= USB_INTR_TRANS_COMPLETE;
    return intr;
}

static void usb_fire_irq(void) {
    uint32_t intr = usb_compute_intr();
    uint32_t ints = (intr | usb_state.intf) & usb_state.inte;
    if (ints) {
        nvic_signal_irq(5);  /* USBCTRL_IRQ */
    }
}

/* ========================================================================
 * Setup Packet Injection
 * ======================================================================== */

static void usb_send_setup(uint8_t bmRequestType, uint8_t bRequest,
                           uint16_t wValue, uint16_t wIndex, uint16_t wLength) {
    usb_state.dpram[0] = bmRequestType;
    usb_state.dpram[1] = bRequest;
    usb_state.dpram[2] = wValue & 0xFF;
    usb_state.dpram[3] = (wValue >> 8) & 0xFF;
    usb_state.dpram[4] = wIndex & 0xFF;
    usb_state.dpram[5] = (wIndex >> 8) & 0xFF;
    usb_state.dpram[6] = wLength & 0xFF;
    usb_state.dpram[7] = (wLength >> 8) & 0xFF;

    usb_state.sie_status |= USB_SIE_SETUP_REC;
    usb_state.ctrl_state = USB_CTRL_SETUP_SENT;
    usb_fire_irq();
}

/* Complete an EP0 IN transfer (device sent data to us) */
static void usb_complete_ep0_in(void) {
    uint32_t buf_ctrl = dpram_read32(USB_DPRAM_BUF_CTRL);  /* EP0 IN at 0x080 */
    buf_ctrl &= ~(USB_BUF_CTRL_AVAILABLE | USB_BUF_CTRL_FULL);
    dpram_write32(USB_DPRAM_BUF_CTRL, buf_ctrl);
    usb_state.buff_status |= (1u << 0);  /* EP0_IN */
    usb_fire_irq();
}

/* Complete an EP0 OUT transfer (we sent data to device) */
static void usb_complete_ep0_out(uint8_t *data, int len) {
    /* Write data to EP0 buffer */
    if (data && len > 0) {
        memcpy(&usb_state.dpram[USB_DPRAM_EP0_BUF], data, len);
    }
    uint32_t buf_ctrl = dpram_read32(USB_DPRAM_BUF_CTRL + 4);  /* EP0 OUT at 0x084 */
    buf_ctrl &= ~USB_BUF_CTRL_AVAILABLE;
    buf_ctrl |= USB_BUF_CTRL_FULL;
    buf_ctrl = (buf_ctrl & ~USB_BUF_CTRL_LEN_MASK) | (len & USB_BUF_CTRL_LEN_MASK);
    dpram_write32(USB_DPRAM_BUF_CTRL + 4, buf_ctrl);
    usb_state.buff_status |= (1u << 1);  /* EP0_OUT */
    usb_fire_irq();
}

/* ========================================================================
 * Control Transfer State Machine
 * ======================================================================== */

static void usb_ctrl_step(void) {
    uint32_t buf_ctrl;
    switch (usb_state.ctrl_state) {
    case USB_CTRL_IDLE:
    case USB_CTRL_DONE:
        break;

    case USB_CTRL_SETUP_SENT:
        /* Setup just sent — firmware needs to process it.
         * Check if SETUP_REC was cleared by firmware (W1C). */
        if (!(usb_state.sie_status & USB_SIE_SETUP_REC)) {
            /* Firmware cleared it, now determine transfer type from setup packet */
            uint8_t bmRequestType = usb_state.dpram[0];
            uint16_t wLength = usb_state.dpram[6] | (usb_state.dpram[7] << 8);

            if (wLength == 0) {
                /* No data phase — wait for status IN (ZLP) */
                usb_state.ctrl_state = USB_CTRL_WAIT_STATUS_IN;
            } else if (bmRequestType & 0x80) {
                /* IN transfer (device → host) — track expected length for multi-packet */
                usb_state.in_accum_len = 0;
                usb_state.in_expected_len = wLength;
                usb_state.ctrl_state = USB_CTRL_WAIT_DATA_IN;
            } else {
                /* OUT transfer (host → device) */
                usb_state.ctrl_state = USB_CTRL_WAIT_DATA_OUT;
            }
        }
        break;

    case USB_CTRL_WAIT_DATA_IN:
        buf_ctrl = dpram_read32(USB_DPRAM_BUF_CTRL);  /* EP0 IN */
        if ((buf_ctrl & USB_BUF_CTRL_AVAILABLE) && (buf_ctrl & USB_BUF_CTRL_FULL)) {
            /* Firmware has data ready — accumulate into in_accum buffer */
            int pkt_len = buf_ctrl & USB_BUF_CTRL_LEN_MASK;
            if (pkt_len > 0 && usb_state.in_accum_len + pkt_len <= (int)sizeof(usb_state.in_accum)) {
                memcpy(&usb_state.in_accum[usb_state.in_accum_len],
                       &usb_state.dpram[USB_DPRAM_EP0_BUF], pkt_len);
                usb_state.in_accum_len += pkt_len;
            }
            usb_complete_ep0_in();

            /* Check if transfer is complete:
             * - received all expected bytes, OR
             * - short packet (less than max packet size = 64), OR
             * - zero-length packet */
            if (usb_state.in_accum_len >= usb_state.in_expected_len ||
                pkt_len < 64 || pkt_len == 0) {
                /* Copy accumulated data back to EP0 buffer for descriptor parsing */
                int copy_len = usb_state.in_accum_len;
                if (copy_len > (int)sizeof(usb_state.dpram) - USB_DPRAM_EP0_BUF)
                    copy_len = (int)sizeof(usb_state.dpram) - USB_DPRAM_EP0_BUF;
                memcpy(&usb_state.dpram[USB_DPRAM_EP0_BUF],
                       usb_state.in_accum, copy_len);
                /* Update buf_ctrl with total length for parsers */
                uint32_t final_bc = dpram_read32(USB_DPRAM_BUF_CTRL);
                final_bc = (final_bc & ~USB_BUF_CTRL_LEN_MASK) |
                           (copy_len & USB_BUF_CTRL_LEN_MASK);
                dpram_write32(USB_DPRAM_BUF_CTRL, final_bc);
                usb_state.ctrl_state = USB_CTRL_WAIT_STATUS_OUT;
            }
            /* else: wait for next packet */
        }
        break;

    case USB_CTRL_WAIT_STATUS_OUT:
        buf_ctrl = dpram_read32(USB_DPRAM_BUF_CTRL + 4);  /* EP0 OUT */
        if (buf_ctrl & USB_BUF_CTRL_AVAILABLE) {
            /* Firmware ready for status ZLP */
            usb_complete_ep0_out(NULL, 0);
            usb_state.ctrl_state = USB_CTRL_DONE;
        }
        break;

    case USB_CTRL_WAIT_DATA_OUT:
        buf_ctrl = dpram_read32(USB_DPRAM_BUF_CTRL + 4);  /* EP0 OUT */
        if (buf_ctrl & USB_BUF_CTRL_AVAILABLE) {
            /* Firmware ready to receive data */
            usb_complete_ep0_out(usb_state.out_data, usb_state.out_data_len);
            usb_state.ctrl_state = USB_CTRL_WAIT_STATUS_IN;
        }
        break;

    case USB_CTRL_WAIT_STATUS_IN:
        buf_ctrl = dpram_read32(USB_DPRAM_BUF_CTRL);  /* EP0 IN */
        if (buf_ctrl & USB_BUF_CTRL_AVAILABLE) {
            /* Firmware sending status ZLP */
            usb_complete_ep0_in();
            usb_state.ctrl_state = USB_CTRL_DONE;
        }
        break;
    }
}

/* ========================================================================
 * Enumeration State Machine
 * ======================================================================== */

/* Parse configuration descriptor to find CDC bulk endpoints */
static void usb_parse_config_desc(void) {
    uint8_t *buf = &usb_state.dpram[USB_DPRAM_EP0_BUF];
    uint32_t buf_ctrl = dpram_read32(USB_DPRAM_BUF_CTRL);
    int total_len = buf_ctrl & USB_BUF_CTRL_LEN_MASK;
    if (total_len < 9) return;

    int pos = 0;
    int in_cdc_data = 0;

    while (pos + 1 < total_len) {
        int desc_len = buf[pos];
        int desc_type = buf[pos + 1];
        if (desc_len < 2 || pos + desc_len > total_len) break;

        if (desc_type == 4 && desc_len >= 9) {
            /* Interface descriptor */
            int iface_class = buf[pos + 5];
            int iface_subclass = buf[pos + 6];
            if (iface_class == 0x0A) {
                /* CDC Data class — endpoints are here */
                in_cdc_data = 1;
            } else {
                if (iface_class == 0x02 && iface_subclass == 0x02) {
                    /* CDC ACM Communication interface — class requests go here */
                    usb_state.cdc_iface = buf[pos + 2];
                }
                in_cdc_data = 0;
            }
        }

        if (desc_type == 5 && desc_len >= 7 && in_cdc_data) {
            /* Endpoint descriptor in CDC Data interface */
            int ep_addr = buf[pos + 2];
            int ep_attr = buf[pos + 3] & 0x03;
            if (ep_attr == 2) {  /* Bulk */
                if (ep_addr & 0x80) {
                    usb_state.cdc_in_ep = ep_addr & 0x0F;
                } else {
                    usb_state.cdc_out_ep = ep_addr & 0x0F;
                }
            }
        }

        pos += desc_len;
    }
}

static void usb_enum_step(void) {
    if (usb_state.ctrl_state != USB_CTRL_IDLE &&
        usb_state.ctrl_state != USB_CTRL_DONE)
        return;  /* Transfer in progress */

    if (usb_state.delay > 0) {
        usb_state.delay--;
        return;
    }

    switch (usb_state.enum_state) {
    case USB_ENUM_DISABLED:
        break;

    case USB_ENUM_WAIT_PULLUP:
        if (usb_state.sie_ctrl & USB_SIE_CTRL_PULLUP_EN) {
            usb_state.enum_state = USB_ENUM_BUS_RESET;
            usb_state.delay = 500;
        }
        break;

    case USB_ENUM_BUS_RESET:
        usb_state.sie_status |= USB_SIE_BUS_RESET | USB_SIE_CONNECTED;
        usb_fire_irq();
        usb_state.enum_state = USB_ENUM_GET_DESC_8;
        usb_state.delay = 500;
        usb_state.ctrl_state = USB_CTRL_IDLE;
        break;

    case USB_ENUM_GET_DESC_8:
        /* GET_DEVICE_DESCRIPTOR, first 8 bytes */
        usb_send_setup(0x80, 6, 0x0100, 0, 8);
        usb_state.enum_state = USB_ENUM_SET_ADDRESS;
        break;

    case USB_ENUM_SET_ADDRESS:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            /* Assign address 1 */
            usb_send_setup(0x00, 5, 1, 0, 0);
            usb_state.enum_state = USB_ENUM_GET_DESC_FULL;
            usb_state.delay = 50;
        }
        break;

    case USB_ENUM_GET_DESC_FULL:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            usb_state.ctrl_state = USB_CTRL_IDLE;
            /* GET_DEVICE_DESCRIPTOR, full 18 bytes */
            usb_send_setup(0x80, 6, 0x0100, 0, 18);
            usb_state.enum_state = USB_ENUM_GET_CONFIG_SHORT;
        }
        break;

    case USB_ENUM_GET_CONFIG_SHORT:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            usb_state.ctrl_state = USB_CTRL_IDLE;
            /* GET_CONFIGURATION_DESCRIPTOR, first 9 bytes to get total length */
            usb_send_setup(0x80, 6, 0x0200, 0, 9);
            usb_state.enum_state = USB_ENUM_GET_CONFIG_FULL;
        }
        break;

    case USB_ENUM_GET_CONFIG_FULL:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            /* Read wTotalLength from config descriptor */
            uint8_t *buf = &usb_state.dpram[USB_DPRAM_EP0_BUF];
            usb_state.config_total_len = buf[2] | (buf[3] << 8);
            if (usb_state.config_total_len > 64) usb_state.config_total_len = 64;
            usb_state.ctrl_state = USB_CTRL_IDLE;
            /* GET_CONFIGURATION_DESCRIPTOR, full length */
            usb_send_setup(0x80, 6, 0x0200, 0, usb_state.config_total_len);
            usb_state.enum_state = USB_ENUM_SET_CONFIG;
        }
        break;

    case USB_ENUM_SET_CONFIG:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            /* Parse config descriptor for CDC endpoints */
            usb_parse_config_desc();
            /* If parsing didn't find CDC endpoints (truncated descriptor),
             * use standard CDC-ACM defaults: EP2 IN, EP2 OUT */
            if (usb_state.cdc_in_ep == 0) usb_state.cdc_in_ep = 2;
            if (usb_state.cdc_out_ep == 0) usb_state.cdc_out_ep = 2;
            usb_state.ctrl_state = USB_CTRL_IDLE;
            /* SET_CONFIGURATION 1 */
            usb_send_setup(0x00, 9, 1, 0, 0);
            /* Skip SET_LINE_CODING (causes hangs with some firmware),
             * go directly to SET_CONTROL_LINE_STATE for DTR/RTS.
             * Delay to let firmware complete USB stack init before DTR. */
            usb_state.enum_state = USB_ENUM_CDC_SET_CTRL_LINE;
            usb_state.delay = 500;
        }
        break;

    case USB_ENUM_CDC_SET_LINE_CODING:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            usb_state.ctrl_state = USB_CTRL_IDLE;
            /* SET_LINE_CODING: 115200 baud, 8N1 */
            usb_state.out_data[0] = 0x00; usb_state.out_data[1] = 0xC2;
            usb_state.out_data[2] = 0x01; usb_state.out_data[3] = 0x00; /* 115200 LE */
            usb_state.out_data[4] = 0x00; /* 1 stop bit */
            usb_state.out_data[5] = 0x00; /* No parity */
            usb_state.out_data[6] = 0x08; /* 8 data bits */
            usb_state.out_data_len = 7;
            usb_send_setup(0x21, 0x20, 0, usb_state.cdc_iface, 7);
            usb_state.enum_state = USB_ENUM_CDC_SET_CTRL_LINE;
        }
        break;

    case USB_ENUM_CDC_SET_CTRL_LINE:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            usb_state.ctrl_state = USB_CTRL_IDLE;
            /* SET_CONTROL_LINE_STATE: DTR=1, RTS=1 */
            usb_send_setup(0x21, 0x22, 0x0003, usb_state.cdc_iface, 0);
            usb_state.enum_state = USB_ENUM_ACTIVE;
        }
        break;

    case USB_ENUM_ACTIVE:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            usb_state.ctrl_state = USB_CTRL_IDLE;
        }
        break;
    }
}

/* ========================================================================
 * CDC Data Path
 * ======================================================================== */

static void usb_handle_cdc(void) {
    if (usb_state.cdc_in_ep == 0) return;

    /* Check CDC bulk IN endpoint for data from device */
    int ep = usb_state.cdc_in_ep;
    uint32_t buf_ctrl_off = USB_DPRAM_BUF_CTRL + ep * 8;  /* IN */
    uint32_t buf_ctrl = dpram_read32(buf_ctrl_off);

    if ((buf_ctrl & USB_BUF_CTRL_FULL) && (buf_ctrl & USB_BUF_CTRL_AVAILABLE)) {
        int len = buf_ctrl & USB_BUF_CTRL_LEN_MASK;

        /* Get buffer address from EP control register */
        uint32_t ep_ctrl_off = USB_DPRAM_EP_CTRL + (ep - 1) * 8;  /* IN control */
        uint32_t ep_ctrl = dpram_read32(ep_ctrl_off);
        uint32_t buf_addr = (ep_ctrl & 0xFFFF) & ~0x3F;  /* bits [15:6] << 0, already aligned */
        /* Actually bits [15:6] are the address divided by 64 ... no, bits [15:6] ARE the address with lower 6 bits zero */
        /* RP2040: BUFFER_ADDRESS is bits [15:6], shift not needed since they store byte address with 64-byte alignment */
        buf_addr = ep_ctrl & 0xFFC0;  /* bits [15:6] */

        if (buf_addr > 0 && buf_addr + len <= USBCTRL_DPRAM_SIZE) {
            const uint8_t *payload = &usb_state.dpram[buf_addr];
            if (usb_tcp.client_fd >= 0) {
                usb_console_tcp_tx(payload, len);
            }
            if (usb_cdc_stdout_enabled) {
                fwrite(payload, 1, (size_t)len, stdout);
                fflush(stdout);
                if (__builtin_expect(expect_enabled, 0)) {
                    expect_append((const char *)payload, len);
                }
            }
        }

        /* Complete the transfer */
        buf_ctrl &= ~(USB_BUF_CTRL_AVAILABLE | USB_BUF_CTRL_FULL);
        dpram_write32(buf_ctrl_off, buf_ctrl);
        usb_state.buff_status |= (1u << (ep * 2));  /* EPn_IN */
        usb_fire_irq();
    }
}

/* Push a byte into the CDC OUT endpoint (for stdin input) */
int usb_cdc_rx_push(uint8_t byte) {
    if (usb_state.enum_state != USB_ENUM_ACTIVE) return 0;
    if (usb_state.cdc_out_ep == 0) return 0;

    /* Queue into CDC RX FIFO */
    if (usb_state.cdc_rx_count < (int)sizeof(usb_state.cdc_rx_fifo)) {
        usb_state.cdc_rx_fifo[usb_state.cdc_rx_head] = byte;
        usb_state.cdc_rx_head = (usb_state.cdc_rx_head + 1) % (int)sizeof(usb_state.cdc_rx_fifo);
        usb_state.cdc_rx_count++;
        return 1;
    }

    return 0;
}

int usb_cdc_stdio_active(void) {
    return usb_state.enum_state == USB_ENUM_ACTIVE &&
           usb_state.cdc_in_ep != 0 &&
           usb_state.cdc_out_ep != 0;
}

/* Drain CDC RX FIFO into the OUT endpoint when firmware has buffer available */
static void usb_cdc_rx_drain(void) {
    if (usb_state.cdc_rx_count == 0) return;
    if (usb_state.cdc_out_ep == 0) return;

    int ep = usb_state.cdc_out_ep;
    uint32_t buf_ctrl_off = USB_DPRAM_BUF_CTRL + ep * 8 + 4;  /* OUT */
    uint32_t buf_ctrl = dpram_read32(buf_ctrl_off);

    if (buf_ctrl & USB_BUF_CTRL_AVAILABLE) {
        /* Get buffer address and max packet size */
        uint32_t ep_ctrl_off = USB_DPRAM_EP_CTRL + (ep - 1) * 8 + 4;  /* OUT control */
        uint32_t ep_ctrl = dpram_read32(ep_ctrl_off);
        uint32_t buf_addr = ep_ctrl & 0xFFC0;
        int max_pkt = buf_ctrl & USB_BUF_CTRL_LEN_MASK;
        if (max_pkt == 0) max_pkt = 64;

        if (buf_addr > 0 && buf_addr < USBCTRL_DPRAM_SIZE) {
            /* Copy as many bytes as possible from FIFO into the buffer */
            int len = 0;
            while (len < max_pkt && usb_state.cdc_rx_count > 0 &&
                   buf_addr + len < USBCTRL_DPRAM_SIZE) {
                usb_state.dpram[buf_addr + len] = usb_state.cdc_rx_fifo[usb_state.cdc_rx_tail];
                usb_state.cdc_rx_tail = (usb_state.cdc_rx_tail + 1) % (int)sizeof(usb_state.cdc_rx_fifo);
                usb_state.cdc_rx_count--;
                len++;
            }

            buf_ctrl &= ~USB_BUF_CTRL_AVAILABLE;
            buf_ctrl |= USB_BUF_CTRL_FULL;
            buf_ctrl = (buf_ctrl & ~USB_BUF_CTRL_LEN_MASK) | len;
            dpram_write32(buf_ctrl_off, buf_ctrl);
            usb_state.buff_status |= (1u << (ep * 2 + 1));  /* EPn_OUT */
            usb_fire_irq();
        }
    }
}

/* ========================================================================
 * USB Step (called from main loop)
 * ======================================================================== */

void usb_step(void) {
    static usb_enum_state_t trace_last_enum = USB_ENUM_DISABLED;
    static usb_ctrl_state_t trace_last_ctrl = USB_CTRL_IDLE;
    static int trace_logged_enable = 0;

    /* Only active when controller is enabled */
    if (!(usb_state.main_ctrl & USB_MAIN_CTRL_EN)) {
        return;
    }

    if (usb_enum_trace_enabled && !trace_logged_enable) {
        trace_logged_enable = 1;
        usb_trace_status("controller enabled");
    }

    /* First time enabled: start enumeration */
    if (usb_state.enum_state == USB_ENUM_DISABLED) {
        usb_state.enum_state = USB_ENUM_WAIT_PULLUP;
        usb_state.sie_status |= USB_SIE_VBUS_DETECTED;
        if (usb_enum_trace_enabled) {
            usb_trace_status("VBUS detected, waiting for pull-up");
        }
    }

    /* Advance control transfer */
    usb_ctrl_step();

    /* Advance enumeration */
    usb_enum_step();

    if (usb_enum_trace_enabled &&
        (usb_state.enum_state != trace_last_enum ||
         usb_state.ctrl_state != trace_last_ctrl)) {
        usb_trace_status("transition");
        trace_last_enum = usb_state.enum_state;
        trace_last_ctrl = usb_state.ctrl_state;
        if (usb_state.enum_state == USB_ENUM_ACTIVE) {
            usb_trace_status("enumeration complete");
            if (!usb_tcp_logged_ready && usb_tcp.port > 0) {
                usb_tcp_logged_ready = 1;
                fprintf(stderr,
                        "[USB] CDC active — host may call stdio_usb_connected() "
                        "(DTR asserted)\n");
            }
        }
    }

    /* Handle CDC data when active */
    if (usb_state.enum_state == USB_ENUM_ACTIVE) {
        usb_handle_cdc();
        usb_cdc_rx_drain();
    }

    usb_console_tcp_poll();
}

/* ========================================================================
 * Init / Match / Read / Write
 * ======================================================================== */

void usb_init(void) {
    memset(&usb_state, 0, sizeof(usb_state_t));
    usb_state.ep_abort_done = 0xFFFFFFFF;
}

int usb_match(uint32_t addr) {
    uint32_t base = addr & ~0x3000;
    if (base >= USBCTRL_DPRAM_BASE && base < USBCTRL_DPRAM_BASE + USBCTRL_DPRAM_SIZE)
        return 1;
    if (base >= USBCTRL_REGS_BASE && base < USBCTRL_REGS_BASE + USBCTRL_REGS_SIZE)
        return 1;
    return 0;
}

uint32_t usb_read32(uint32_t addr) {
    uint32_t base = addr & ~0x3000;

    /* DPRAM reads */
    if (base >= USBCTRL_DPRAM_BASE && base < USBCTRL_DPRAM_BASE + USBCTRL_DPRAM_SIZE) {
        uint32_t off = base - USBCTRL_DPRAM_BASE;
        uint32_t val;
        memcpy(&val, &usb_state.dpram[off], 4);
        return val;
    }

    /* Controller registers */
    uint32_t offset = base - USBCTRL_REGS_BASE;
    switch (offset) {
    case USB_ADDR_ENDP:
        return usb_state.addr_endp;
    case USB_MAIN_CTRL:
        return usb_state.main_ctrl;
    case USB_SOF_RD:
        return 0;
    case USB_SIE_CTRL:
        return usb_state.sie_ctrl;
    case USB_SIE_STATUS:
        return usb_state.sie_status;
    case USB_INT_EP_CTRL:
        return usb_state.int_ep_ctrl;
    case USB_BUFF_STATUS:
        return usb_state.buff_status;
    case USB_BUFF_CPU_SHOULD_HANDLE:
        return usb_state.buff_status;
    case USB_EP_ABORT:
        return usb_state.ep_abort;
    case USB_EP_ABORT_DONE:
        return usb_state.ep_abort_done;
    case USB_EP_STALL_ARM:
        return usb_state.ep_stall_arm;
    case USB_EP_STATUS_STALL_NAK:
        return usb_state.ep_status_stall_nak;
    case USB_USB_MUXING:
        return usb_state.usb_muxing;
    case USB_USB_PWR:
        return usb_state.usb_pwr;
    case USB_INTR:
        return usb_compute_intr();
    case USB_INTE:
        return usb_state.inte;
    case USB_INTF:
        return usb_state.intf;
    case USB_INTS:
        return (usb_compute_intr() | usb_state.intf) & usb_state.inte;
    case USB_NAK_POLL:
        return 0;
    default:
        return 0;
    }
}

void usb_write32(uint32_t addr, uint32_t val) {
    static int trace_write_budget = 40;
    uint32_t base = addr & ~0x3000;
    uint32_t alias = (addr >> 12) & 0x3;

    if (usb_enum_trace_enabled && trace_write_budget > 0) {
        trace_write_budget--;
        fprintf(stderr, "[USB-trace] write32 addr=0x%08X val=0x%08X base=0x%08X\n",
                addr, val, base);
    }

    /* DPRAM writes (no alias for DPRAM) */
    if (base >= USBCTRL_DPRAM_BASE && base < USBCTRL_DPRAM_BASE + USBCTRL_DPRAM_SIZE) {
        uint32_t off = base - USBCTRL_DPRAM_BASE;
        if (alias == 0) {
            memcpy(&usb_state.dpram[off], &val, 4);
        } else {
            uint32_t cur;
            memcpy(&cur, &usb_state.dpram[off], 4);
            switch (alias) {
                case 1: cur ^= val; break;
                case 2: cur |= val; break;
                case 3: cur &= ~val; break;
            }
            memcpy(&usb_state.dpram[off], &cur, 4);
        }
        return;
    }

    /* Controller registers */
    uint32_t offset = base - USBCTRL_REGS_BASE;

    #define ALIAS_APPLY(reg) do { \
        switch (alias) { \
            case 0: (reg) = val; break; \
            case 1: (reg) ^= val; break; \
            case 2: (reg) |= val; break; \
            case 3: (reg) &= ~val; break; \
        } \
    } while(0)

    switch (offset) {
    case USB_ADDR_ENDP:
        ALIAS_APPLY(usb_state.addr_endp);
        break;
    case USB_MAIN_CTRL:
        ALIAS_APPLY(usb_state.main_ctrl);
        if (usb_enum_trace_enabled) {
            usb_trace_status("write MAIN_CTRL");
        }
        break;
    case USB_SIE_CTRL:
        ALIAS_APPLY(usb_state.sie_ctrl);
        if (usb_enum_trace_enabled) {
            usb_trace_status("write SIE_CTRL");
        }
        break;
    case USB_SIE_STATUS:
        /* W1C for most bits */
        if (alias == 0 || alias == 3) {
            usb_state.sie_status &= ~val;
        } else {
            ALIAS_APPLY(usb_state.sie_status);
        }
        break;
    case USB_INT_EP_CTRL:
        ALIAS_APPLY(usb_state.int_ep_ctrl);
        break;
    case USB_BUFF_STATUS:
        /* W1C */
        if (alias == 0 || alias == 3) {
            usb_state.buff_status &= ~val;
        } else {
            ALIAS_APPLY(usb_state.buff_status);
        }
        break;
    case USB_EP_ABORT:
        ALIAS_APPLY(usb_state.ep_abort);
        break;
    case USB_EP_ABORT_DONE:
        /* W1C */
        if (alias == 0 || alias == 3) {
            usb_state.ep_abort_done &= ~val;
        } else {
            ALIAS_APPLY(usb_state.ep_abort_done);
        }
        break;
    case USB_EP_STALL_ARM:
        ALIAS_APPLY(usb_state.ep_stall_arm);
        break;
    case USB_EP_STATUS_STALL_NAK:
        /* W1C */
        if (alias == 0 || alias == 3) {
            usb_state.ep_status_stall_nak &= ~val;
        } else {
            ALIAS_APPLY(usb_state.ep_status_stall_nak);
        }
        break;
    case USB_USB_MUXING:
        ALIAS_APPLY(usb_state.usb_muxing);
        break;
    case USB_USB_PWR:
        ALIAS_APPLY(usb_state.usb_pwr);
        break;
    case USB_INTE:
        ALIAS_APPLY(usb_state.inte);
        break;
    case USB_INTF:
        ALIAS_APPLY(usb_state.intf);
        break;
    default:
        break;
    }

    #undef ALIAS_APPLY
}

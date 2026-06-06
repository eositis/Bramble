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
#include "netbridge.h"
#include "nvic.h"
#include "devtools.h"
#include "emulator.h"

/*
 * TinyUSB device globals for MegaFlash pico2_debug (megaflash.uf2).
 * Used to mirror host DTR/mounted when EP0 status stalls in emulation.
 */
#define USB_GUEST_TUD_DEVICE_STATE  0x20005e64u
#define USB_GUEST_TUD_MOUNTED_OFF   1u
#define USB_GUEST_TUD_CTRL_BUSY_OFF 0x35u
#define USB_GUEST_TUD_CDC_BASE      0x20005d14u
#define USB_GUEST_TUD_CDC_LINE_OFF  4u
#define USB_GUEST_TUD_CDC_TXFIFO    (USB_GUEST_TUD_CDC_BASE + 0x24u)
#define USB_GUEST_TUD_CDC_RXFIFO    (USB_GUEST_TUD_CDC_BASE + 0x10u)
#define USB_GUEST_TUD_CDC_RXBUF     (USB_GUEST_TUD_CDC_BASE + 0x38u)
#define USB_GUEST_TUD_CDC_TXBUF     (USB_GUEST_TUD_CDC_BASE + 0x78u)
#define USB_GUEST_STDIO_MUTEX       0x2000516cu
#define USB_GUEST_STDIO_USB_MUTEX   0x200526fcu
#define USB_GUEST_STDIO_USB_DRIVER  0x200047d4u /* Pico SDK stdio_usb driver RAM */
#define USB_GUEST_STDIO_ACTIVE_DRV  0x20007854u /* stdio_driver_t * chain head */
#define USB_GUEST_STDIO_USB_OUT     0x1000e32du /* stdio_usb_out_chars */
#define USB_GUEST_STDIO_USB_FLUSH   0x1000e0a5u /* stdio_usb_out_flush */
#define USB_GUEST_STDIO_USB_IN      0x1000e294u /* stdio_usb_in_chars */
#define USB_GUEST_TUD_CDC_AVAILABLE 0x1000fc0cu /* tud_cdc_n_available */
#define USB_GUEST_TUD_CDC_READ      0x1000fc24u /* tud_cdc_n_read */
#define USB_GUEST_STDIO_USB_AVAIL   0x1000e091u /* chars_available callback */
#define USB_GUEST_CHECK_PICOW_INITED  0x2005bc81u
#define USB_GUEST_CHECK_PICOW_RESULT  0x2005bc82u

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

#define USB_TCP_TX_PENDING_MAX 4096u

static usb_tcp_bridge_t usb_tcp;
static uint8_t usb_tcp_tx_pending[USB_TCP_TX_PENDING_MAX];
static size_t usb_tcp_tx_pending_len;
static int usb_tcp_logged_ready;
static int usb_logged_host_enable;
static int usb_ctrl_stall_steps;
static int usb_guest_cdc_synced;

static void usb_guest_bridge_activate(void);
static void usb_console_tcp_tx(const uint8_t *data, int len);
static uint16_t usb_guest_fifo_count(uint32_t fifo_addr);
static int usb_guest_cdc_rx_fifo_push(uint8_t byte);
static int usb_guest_cdc_rx_fifo_pop(uint8_t *out);

static int usb_console_bridge_mode(void) {
    return usb_tcp.port > 0;
}

static int guest_megaflash_hook_active(void) {
    return usb_console_bridge_mode() || net_bridge_any_active();
}

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
            if (usb_tcp_tx_pending_len > 0) {
                usb_console_tcp_tx(usb_tcp_tx_pending,
                                   (int)usb_tcp_tx_pending_len);
                usb_tcp_tx_pending_len = 0;
            }
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
                if (usb_guest_cdc_synced) {
                    (void)usb_guest_cdc_rx_fifo_push(buf[j]);
                } else {
                    (void)usb_cdc_rx_push(buf[j]);
                }
            }
        } else if (n == 0) {
            fprintf(stderr, "[USB] CDC console client disconnected\n");
            close(usb_tcp.client_fd);
            usb_tcp.client_fd = -1;
        }
    }
}

static void usb_console_tcp_tx(const uint8_t *data, int len) {
    if (len <= 0) {
        return;
    }
    if (usb_tcp.client_fd < 0) {
        for (int i = 0; i < len; i++) {
            if (usb_tcp_tx_pending_len < USB_TCP_TX_PENDING_MAX) {
                usb_tcp_tx_pending[usb_tcp_tx_pending_len++] = data[i];
            }
        }
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
    case USB_ENUM_CDC_SET_CTRL_LINE_DONE: return "CDC_SET_CTRL_LINE_DONE";
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

static void usb_sync_guest_cdc_connected(void) {
    static int logged_guest_patch;

    uint8_t mounted = mem_read8(USB_GUEST_TUD_DEVICE_STATE + USB_GUEST_TUD_MOUNTED_OFF);
    if (mounted == 0) {
        mem_write8(USB_GUEST_TUD_DEVICE_STATE + USB_GUEST_TUD_MOUNTED_OFF, 1);
        mounted = mem_read8(USB_GUEST_TUD_DEVICE_STATE + USB_GUEST_TUD_MOUNTED_OFF);
        if (!logged_guest_patch && mounted) {
            logged_guest_patch = 1;
            fprintf(stderr, "[USB] guest TinyUSB mounted flag set at 0x%08X\n",
                    (unsigned)(USB_GUEST_TUD_DEVICE_STATE + USB_GUEST_TUD_MOUNTED_OFF));
        }
    } else if (usb_console_bridge_mode()) {
        mem_write8(USB_GUEST_TUD_DEVICE_STATE + USB_GUEST_TUD_MOUNTED_OFF, 1);
    }

    uint8_t line = mem_read8(USB_GUEST_TUD_CDC_BASE + USB_GUEST_TUD_CDC_LINE_OFF);
    if ((line & 1u) == 0) {
        mem_write8(USB_GUEST_TUD_CDC_BASE + USB_GUEST_TUD_CDC_LINE_OFF, (uint8_t)(line | 1u));
    }

    /* tud_suspended() reads bit 2 of device state byte 0 */
    uint8_t dev0 = mem_read8(USB_GUEST_TUD_DEVICE_STATE);
    if (dev0 & (1u << 2)) {
        mem_write8(USB_GUEST_TUD_DEVICE_STATE, (uint8_t)(dev0 & ~(1u << 2)));
    }

    /* Let process_control_request complete SET_CONTROL_LINE_STATE status stage */
    mem_write8(USB_GUEST_TUD_DEVICE_STATE + USB_GUEST_TUD_CTRL_BUSY_OFF, 1);

    /* Stop SETUP_REQ IRQ storms once guest sees DTR */
    usb_state.sie_status &= ~USB_SIE_SETUP_REC;
    static int cleared_setup;
    if (!cleared_setup) {
        cleared_setup = 1;
        memset(usb_state.dpram, 0, 8);
    }
}

static uint32_t usb_sie_status_for_guest(void) {
    uint32_t status = usb_state.sie_status;
    if (usb_console_bridge_mode() && usb_guest_cdc_synced) {
        status &= ~(USB_SIE_SETUP_REC | USB_SIE_BUS_RESET);
        status |= USB_SIE_VBUS_DETECTED | USB_SIE_CONNECTED;
    }
    return status;
}

static uint32_t usb_buff_status_for_guest(void) {
    return usb_state.buff_status;
}

static void usb_log_cdc_active_once(void) {
    if (usb_tcp_logged_ready) {
        return;
    }
    usb_tcp_logged_ready = 1;
    fprintf(stderr,
            "[USB] CDC active — host may call stdio_usb_connected() (DTR asserted)\n");
}

#define USB_GUEST_STDIO_PUTSTRING   0x1000dbdeu  /* megaflash.uf2: blx out_chars */
#define USB_GUEST_STDIO_PUTSTRING_FN 0x1000db94u /* stdio_put_string entry */
#define USB_GUEST_WRAP_PUTCHAR_CALL 0x1000dce0u  /* megaflash.uf2: bl stdio_put_string (1 char) */
#define USB_GUEST_WRAP_PUTS         0x1000dceau  /* megaflash.uf2: __wrap_puts @ 0x1000DCEA */
#define USB_GUEST_WRAP_PUTS_CALL    0x1000dcfcu  /* megaflash.uf2: bl stdio_put_string */
#define USB_GUEST_WRAP_PRINTF       0x1000dd2au  /* megaflash.uf2: __wrap_printf entry */
#define USB_GUEST_USER_TERMINAL_DEVINFO 0x10005aecu /* bl DeviceInfo */
#define USB_GUEST_PRINT_BANNER      0x10005af0u
#define USB_GUEST_GET_DEVICE_INFO   0x10005058u /* GetDeviceInfoString */
#define USB_GUEST_PRINT_ALL_PARTITIONS 0x100057a0u /* PrintAllPartitions */
#define USB_GUEST_ASSERT_FUNC       0x1000da68u /* __assert_func */
#define USB_GUEST_CHECK_ALLOC       0x1000d924u /* check_alloc — skip heap bounds under emu */
#define USB_GUEST_ENABLE_SPI0       0x10002910u /* enable_spi0 — clamp deviceNum for emu */
#define USB_GUEST_SPI_WR_RD_VENEER  0x100347d0u /* __spi_write_read_blocking_veneer */
#define USB_GUEST_SPI_READ_BLOCKING_V 0x10034868u /* __spi_read_blocking_veneer */
#define USB_GUEST_ALARM_POOL_DEFAULT  0x1000b638u /* alarm_pool_get_default */
#define USB_GUEST_MULTICORE_LAUNCH    0x10000318u /* main: bl multicore_launch_core1 */
#define USB_GUEST_INIT_SPI_CALL         0x100002b6u /* main: bl InitSpi */
#define USB_GUEST_U2_INIT_CALL          0x100002fcu /* main: bl U2_Init */
#define USB_GUEST_LOAD_ALL_CONFIGS_CALL 0x10000300u /* main: bl LoadAllConfigs */
#define USB_GUEST_SETUP_FLASH_MAP_CALL  0x10000304u /* main: bl SetupFlashUnitMapping */
#define USB_GUEST_ENABLE_FLASH_MAP_CALL 0x10000308u /* main: bl EnableFlashUnitMapping */
#define USB_GUEST_SAVE_CONFIGS_CALL     0x1000031cu /* main: bl SaveConfigs */
#define USB_GUEST_IS_APPLE_CONNECTED_CALL  0x1000030cu /* main: bl IsAppleConnected */
#define USB_GUEST_IS_APPLE_CONNECTED_CALL2 0x100003dcu
#define USB_GUEST_CHECK_PICOW_CALL    0x1000036cu /* main: bl CheckPicoW */
#define USB_GUEST_CHECK_PICOW_CALL2   0x10000392u
#define USB_GUEST_CHECK_PICOW_FN      0x10004dd8u
#define USB_GUEST_IS_APPLE_CONNECTED_FN 0x10004d80u
#define USB_GUEST_CORE0_LOOP_VENEER   0x10034730u
#define USB_GUEST_ENABLE_APPLE_RST    0x10000294u
#define USB_GUEST_STDIO_USB_INIT_CALL1 0x1000039au /* main: bl stdio_usb_init (PicoW) */
#define USB_GUEST_STDIO_USB_INIT_CALL2 0x1000045au
#define USB_GUEST_STDIO_USB_INIT_FN   0x1000e400u
#define USB_GUEST_INIT_PICOLED_CALL1  0x1000039eu
#define USB_GUEST_INIT_PICOLED_CALL2  0x1000045eu
#define USB_GUEST_STDIO_USB_CONNECTED 0x1000e288u
#define USB_GUEST_USB_WAIT_LOOP       0x100003b2u /* PicoW USB wait before UserTerminal */
#define USB_GUEST_USB_CONN_LOOP       0x10000468u /* non-PicoW USB wait loop */
#define USB_GUEST_CLOCK_GET_HZ        0x1000bdd8u
#define USB_GUEST_SPI_GET_BAUDRATE    0x10011a30u
#define USB_GUEST_UART_PUTC           0x1000dd48u /* uart_putc — bridge to TCP under -uart-console */
#define USB_GUEST_MAIN_USB_LOOP       0x10000464u /* bl UserTerminal (PicoW USB path) */
#define USB_GUEST_USER_TERMINAL       0x10005ad0u
#define USB_GUEST_ALARM_POOL_RAM      0x200047a4u /* Pico SDK default alarm pool */
#define USB_GUEST_MUTEX_ENTER_V     0x10034858u /* __recursive_mutex_enter_blocking_veneer */
#define USB_GUEST_MUTEX_EXIT_V      0x10034848u /* __recursive_mutex_exit_veneer */
#define USB_GUEST_TS_READ_JEDECID   0x10003088u /* tsReadJEDECID — stub JEDEC for emu flash */
#define USB_GUEST_EXIT              0x1000d9c0u /* _exit → BKPT loop */
#define USB_GUEST_PANIC             0x1000a8b8u /* panic — skip BKPT _exit under emu */
#define USB_GUEST_HW_CLAIM_LOCK       0x1000a8e8u /* hw_claim_lock */
#define USB_GUEST_HW_CLAIM_OR_ASSERT  0x1000a920u /* hw_claim_or_assert */
#define USB_GUEST_HW_CLAIM_UNUSED     0x1000a952u /* hw_claim_unused_from_range */
#define USB_GUEST_HW_CLAIM_CLEAR_FAIL 0x1000a9c6u /* hw_claim_clear: bit not claimed */
#define USB_GUEST_HW_CLAIM_LOCK_BYTE  0x20005e3fu /* Pico SDK hw_claim mutex byte */
#define USB_GUEST_HW_CLAIM_BITMAP     0x20006580u /* spin_lock / hw claim bits */
#define USB_GUEST_SPIN_LOCK_HW        0x20005e34u /* Pico SDK spin_lock_hw[32] */
#define USB_GUEST_U2_CRIT_SECTION     0x20005a774u /* U2_MonInit critical_section */
#define USB_GUEST_U2_INIT             0x10006828u /* U2_Init — skip Apple II U2 bus under emu */
#define USB_GUEST_U2_MONINIT_CALL     0x10006830u /* U2_Init: bl U2_MonInit */
#define USB_GUEST_U2_RESET_CALL       0x1000684au /* U2_Init: bl u2_reset */
#define USB_GUEST_U2_MON_PUSH         0x1000686cu /* u2_mon_push — avoid ring/lock corruption */
#define USB_GUEST_U2_NET_INIT         0x10007540u /* U2_Net_Init */
#define USB_GUEST_U2_NET_POLL         0x10007ae8u /* U2_Net_Poll */
#define USB_GUEST_U2_MON_POLL_FLUSH   0x10006cb8u /* U2_MonPollFlush */
#define USB_GUEST_WRAP_VPRINTF      0x1000dd08u /* __wrap_vprintf */
#define USB_GUEST_VFPRINTF_R         0x1002f258u /* _vfprintf_r — skip locale/wchar body */
#define USB_GUEST_LOCALE_MB_CUR_MAX 0x10032278u /* __locale_mb_cur_max — vfprintf locale spin */
#define USB_GUEST_VFPRINTF_LOOP_BACK  0x1002f338u /* b.n 1002f30c — cap locale scan iterations */
#define USB_GUEST_VFPRINTF_LOOP_EXIT  0x1002f49cu
#define USB_GUEST_ASCII_MBTOWC      0x10033cc0u /* __ascii_mbtowc */
#define USB_GUEST_ASCII_MBTOWC_LOOP 0x10033ce8u /* internal b.n 10033cd4 */
#define USB_GUEST_VFPRINTF_LO       0x1002f200u
#define USB_GUEST_VFPRINTF_HI       0x10031300u
#define USB_GUEST_USB_CONN_CMP      0x1000046cu /* cmp r0,#0 after stdio_usb_connected (Pico path) */
#define USB_GUEST_USB_CONN_CMP_DBG  0x100003e6u /* same, debug main loop */

static void usb_console_guest_tx_cstr(uint32_t addr) {
    if (addr == 0) {
        return;
    }
    uint8_t chunk[128];
    uint32_t n = 0;
    for (uint32_t i = 0; i < 8192u; i++) {
        uint8_t c = mem_read8(addr + i);
        if (c == 0) {
            break;
        }
        chunk[n++] = c;
        if (n == sizeof(chunk)) {
            usb_console_tcp_tx(chunk, (int)n);
            if (usb_cdc_stdout_enabled) {
                fwrite(chunk, 1, n, stdout);
                fflush(stdout);
            }
            n = 0;
        }
    }
    if (n > 0) {
        usb_console_tcp_tx(chunk, (int)n);
        if (usb_cdc_stdout_enabled) {
            fwrite(chunk, 1, n, stdout);
            fflush(stdout);
        }
    }
}

static void usb_guest_return_to_lr(uint32_t ret) {
    cpu.r[0] = ret;
    cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
}

static void (*usb_guest_vprintf_tx)(uint8_t ch);

static void usb_guest_uart_tx_byte(uint8_t ch) {
    net_bridge_uart_tx(0, ch);
    if (net_bridge_mirror_stdio) {
        fputc((int)ch, stderr);
    }
}

static void usb_guest_usb_tx_byte(uint8_t ch) {
    usb_console_tcp_tx(&ch, 1);
    if (usb_cdc_stdout_enabled) {
        fputc((int)ch, stdout);
    }
}

static void usb_guest_uart_tx_buf(uint32_t buf, uint32_t len) {
    if (buf == 0 || len == 0) {
        return;
    }
    if (len > 8192u) {
        len = 8192u;
    }
    for (uint32_t i = 0; i < len; i++) {
        usb_guest_uart_tx_byte(mem_read8(buf + i));
    }
    if (net_bridge_mirror_stdio) {
        fflush(stderr);
    }
}

static uint32_t usb_guest_uart_make_ap(uint32_t sp) {
    uint32_t ap = sp - 32u;
    mem_write32(ap, cpu.r[1]);
    mem_write32(ap + 4u, cpu.r[2]);
    mem_write32(ap + 8u, cpu.r[3]);
    for (int i = 0; i < 5; i++) {
        mem_write32(ap + 12u + (uint32_t)i * 4u, mem_read32(sp + (uint32_t)i * 4u));
    }
    return ap;
}

static int usb_guest_host_vprintf(uint32_t fmt, uint32_t ap) {
    char spec[32];
    char out[160];
    int total = 0;
    void (*tx)(uint8_t) = usb_guest_vprintf_tx;
    if (!tx) {
        return 0;
    }

    while (1) {
        uint8_t c = mem_read8(fmt++);
        if (c == 0) {
            break;
        }
        if (c != '%') {
            tx(c);
            total++;
            continue;
        }
        c = mem_read8(fmt++);
        if (c == 0) {
            break;
        }
        if (c == '%') {
            tx('%');
            total++;
            continue;
        }
        int si = 0;
        spec[si++] = '%';
        if (c == '-' || c == '+' || c == ' ') {
            spec[si++] = (char)c;
            c = mem_read8(fmt++);
        }
        while ((c >= '0' && c <= '9') || c == '.' || c == 'l' || c == 'h' ||
               c == 'z' || c == '#') {
            if (si < (int)sizeof(spec) - 2) {
                spec[si++] = (char)c;
            }
            c = mem_read8(fmt++);
        }
        if (si < (int)sizeof(spec) - 1) {
            spec[si++] = (char)c;
        }
        spec[si] = '\0';

        if (c == 's') {
            uint32_t str = mem_read32(ap);
            ap += 4u;
            for (uint32_t i = 0; i < 8192u; i++) {
                uint8_t ch = mem_read8(str + i);
                if (ch == 0) {
                    break;
                }
                tx(ch);
                total++;
            }
        } else if (c == 'c') {
            uint32_t v = mem_read32(ap);
            ap += 4u;
            tx((uint8_t)v);
            total++;
        } else if (c == 'p' || c == 'x' || c == 'X' || c == 'u') {
            uint32_t v = mem_read32(ap);
            ap += 4u;
            snprintf(out, sizeof(out), spec, (unsigned)v);
            for (char *p = out; *p != '\0'; p++) {
                tx((uint8_t)*p);
                total++;
            }
        } else if (c == 'd' || c == 'i') {
            int32_t v = (int32_t)mem_read32(ap);
            ap += 4u;
            snprintf(out, sizeof(out), spec, v);
            for (char *p = out; *p != '\0'; p++) {
                tx((uint8_t)*p);
                total++;
            }
        } else {
            for (int i = 0; spec[i] != '\0'; i++) {
                tx((uint8_t)spec[i]);
                total++;
            }
        }
    }
    return total;
}

static void usb_console_guest_tx_buf(uint32_t buf, uint32_t len) {
    if (buf == 0 || len == 0 || len > 4096u) {
        return;
    }
    uint8_t tmp[256];
    while (len > 0) {
        uint32_t chunk = len > sizeof(tmp) ? (uint32_t)sizeof(tmp) : len;
        for (uint32_t i = 0; i < chunk; i++) {
            tmp[i] = mem_read8(buf + i);
        }
        usb_console_tcp_tx(tmp, (int)chunk);
        if (usb_cdc_stdout_enabled) {
            fwrite(tmp, 1, chunk, stdout);
            fflush(stdout);
        }
        buf += chunk;
        len -= chunk;
    }
}

static void usb_guest_hw_claim_bootstrap(void) {
    mem_write8(USB_GUEST_HW_CLAIM_LOCK_BYTE, 0);
    for (uint32_t off = 0; off < 64u; off++) {
        mem_write8(USB_GUEST_HW_CLAIM_BITMAP + off, 0);
    }
    for (uint32_t off = 0; off < 32u; off++) {
        mem_write8(USB_GUEST_SPIN_LOCK_HW + off, 0);
    }
    /* U2 monitor critical_section — uninitialized lock ptr breaks ldaexb in u2_mon_push */
    mem_write32((uint32_t)USB_GUEST_U2_CRIT_SECTION,
                (uint32_t)(USB_GUEST_SPIN_LOCK_HW + 1u));
    mem_write32((uint32_t)(USB_GUEST_U2_CRIT_SECTION + 4u), 0u);
    mem_write32(0x2005bbe4u, 0u);
    mem_write32(0x2005a780u, 0u);
    mem_write32(0x2005a77cu, 0u);
    mem_write32((uint32_t)(USB_GUEST_ALARM_POOL_RAM + 16u),
                (uint32_t)(USB_GUEST_SPIN_LOCK_HW + 2u));
}

#define USB_GUEST_EMU_FLASH_CHIP_MB  64u  /* per external flash chip in device-info stub */

static void usb_guest_write_cstr_to_guest(uint32_t buf, const char *s) {
    for (uint32_t i = 0; s[i] != '\0'; i++) {
        mem_write8(buf + i, (uint8_t)s[i]);
    }
    mem_write8(buf + (uint32_t)strlen(s), 0);
}

static void usb_guest_skip_get_device_info_string(void) {
    uint32_t buf = cpu.r[0];
    /*
     * Native GetDeviceInfoString uses sprintf(%f) after VFP; newlib float
     * formatting can spin under emulation. Build the same multiline layout
     * the firmware would produce (see megaflash .rodata @ 0x10035640).
     */
    char k_msg[512];
    unsigned total_mb = USB_GUEST_EMU_FLASH_CHIP_MB * 2u;
    int n = snprintf(k_msg, sizeof(k_msg),
        "Device Information\r\n"
        "==========\r\n\r\n"
        "Pico Board = Pico 2 RP2350\r\n"
        "Wifi Supported = Yes\r\n"
        "CPU Speed = 150MHz, SPI Speed = 1.0MHz\r\n"
        "MegaFlash Pico Firmware Version = V1.2.1-eo (DEBUG)\r\n"
        "Firmware build: 2026-05-03 03:55:51 UTC  (1777780551 Unix s)\r\n"
        "Pico SDK Version = 2.2.0\r\n"
        "Total Flash Capacity = %uMB\r\n"
        "Flash Chip #0 JEDEC ID = EF4017h\r\n"
        "Flash Chip #1 JEDEC ID = EF4017h\r\n",
        total_mb);
    if (n > 0) {
        usb_guest_write_cstr_to_guest(buf, k_msg);
    }
    cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
}

static void usb_guest_stub_print_all_partitions(void) {
    char k_msg[256];
    int n = snprintf(k_msg, sizeof(k_msg),
        "\r\nPartition Information:\r\n"
        "Drive Type    Volume Name        Size\r\n"
        "Flash Unit 1  MegaFlash          %uMB (Bramble emu)\r\n",
        (unsigned)USB_GUEST_EMU_FLASH_CHIP_MB);
    if (n <= 0) {
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    usb_console_tcp_tx((const uint8_t *)k_msg, n);
    if (usb_cdc_stdout_enabled) {
        fwrite(k_msg, 1, (size_t)n, stdout);
        fflush(stdout);
    }
    cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
}

void usb_console_guest_stdio_hook(void) {
    int usb_mode = usb_console_bridge_mode();
    if (!guest_megaflash_hook_active()) {
        return;
    }

    static int hw_claim_bootstrapped;
    static int user_terminal_logged;
    if (!hw_claim_bootstrapped++) {
        usb_guest_hw_claim_bootstrap();
    }

    uint32_t pc = cpu.r[15] & ~1u;
    if (usb_mode && !user_terminal_logged &&
        (pc == USB_GUEST_MAIN_USB_LOOP || pc == USB_GUEST_USER_TERMINAL)) {
        user_terminal_logged = 1;
        fprintf(stderr, "[USB] guest reached UserTerminal path @ 0x%08X\n", pc);
    }
    uint32_t lr = cpu.r[14] & ~1u;
    static uint32_t vf_locale_iters;

    if (pc == USB_GUEST_GET_DEVICE_INFO) {
        usb_guest_skip_get_device_info_string();
        return;
    }
    if (usb_mode && pc == USB_GUEST_PRINT_ALL_PARTITIONS) {
        usb_guest_stub_print_all_partitions();
        return;
    }
    if (pc == USB_GUEST_ENABLE_SPI0) {
        if (cpu.r[0] > 1u) {
            cpu.r[0] = (cpu.r[0] == 0x40080000u) ? 0u : 1u;
        }
        return;
    }
    if (pc == USB_GUEST_PANIC) {
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_MULTICORE_LAUNCH) {
        /* Same cpu_step runs the insn at the new PC — skip SaveConfigs bl too. */
        cpu.r[15] = 0x10000320u | 1u;
        return;
    }
    if (pc == USB_GUEST_INIT_SPI_CALL) {
        cpu.r[15] = 0x100002bau | 1u;
        return;
    }
    if (pc == USB_GUEST_U2_MONINIT_CALL) {
        cpu.r[15] = 0x10006834u | 1u;
        return;
    }
    if (pc == USB_GUEST_U2_RESET_CALL) {
        cpu.r[15] = 0x1000684eu | 1u;
        return;
    }
    if (pc == USB_GUEST_LOAD_ALL_CONFIGS_CALL) {
        /* Same cpu_step runs the insn at the new PC — leap past config + IsApple bls. */
        cpu.r[0] = 0;
        cpu.r[15] = 0x10000310u | 1u;
        return;
    }
    if (pc == USB_GUEST_SETUP_FLASH_MAP_CALL) {
        cpu.r[0] = 0;
        cpu.r[15] = 0x10000310u | 1u;
        return;
    }
    if (pc == USB_GUEST_ENABLE_FLASH_MAP_CALL) {
        cpu.r[0] = 0;
        cpu.r[15] = 0x10000310u | 1u;
        return;
    }
    if (pc == USB_GUEST_SAVE_CONFIGS_CALL) {
        cpu.r[15] = 0x10000324u | 1u;
        return;
    }
    if (pc == USB_GUEST_IS_APPLE_CONNECTED_CALL ||
        pc == USB_GUEST_IS_APPLE_CONNECTED_CALL2) {
        cpu.r[0] = 0;
        cpu.r[15] = (pc == USB_GUEST_IS_APPLE_CONNECTED_CALL ? 0x10000310u
                                                              : 0x100003e2u) |
                     1u;
        return;
    }
    if (pc == USB_GUEST_IS_APPLE_CONNECTED_FN) {
        cpu.r[0] = 0;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_CHECK_PICOW_CALL || pc == USB_GUEST_CHECK_PICOW_CALL2) {
        cpu.r[0] = usb_mode ? 1u : 0u;
        cpu.r[15] = (pc == USB_GUEST_CHECK_PICOW_CALL ? 0x10000370u : 0x10000396u) |
                     1u;
        return;
    }
    if (pc == USB_GUEST_CHECK_PICOW_FN) {
        cpu.r[0] = usb_mode ? 1u : 0u;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (usb_mode &&
        (pc == USB_GUEST_CORE0_LOOP_VENEER || pc == USB_GUEST_ENABLE_APPLE_RST)) {
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (usb_mode && pc == USB_GUEST_STDIO_USB_INIT_CALL1) {
        cpu.r[15] = 0x100003a2u | 1u;
        return;
    }
    if (usb_mode && pc == USB_GUEST_STDIO_USB_INIT_CALL2) {
        cpu.r[15] = 0x10000468u | 1u;
        return;
    }
    if (usb_mode && pc == USB_GUEST_STDIO_USB_INIT_FN) {
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (usb_mode &&
        (pc == USB_GUEST_INIT_PICOLED_CALL1 || pc == USB_GUEST_INIT_PICOLED_CALL2)) {
        cpu.r[15] = (pc == USB_GUEST_INIT_PICOLED_CALL1 ? 0x100003a2u
                                                         : 0x10000468u) |
                     1u;
        return;
    }
    if (usb_mode && pc == USB_GUEST_STDIO_USB_CONNECTED) {
        cpu.r[0] = usb_guest_cdc_synced ? 1u : 0u;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (usb_mode && pc == USB_GUEST_USB_WAIT_LOOP) {
        cpu.r[15] = 0x100003e8u | 1u;
        return;
    }
    if (usb_mode && pc == USB_GUEST_USB_CONN_LOOP) {
        cpu.r[15] = 0x10000464u | 1u;
        return;
    }
    if (pc == USB_GUEST_CLOCK_GET_HZ) {
        cpu.r[0] = 150000000u;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_SPI_GET_BAUDRATE) {
        cpu.r[0] = 1000000u;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (!usb_mode && net_bridge_uart_active(0)) {
        if (pc == USB_GUEST_WRAP_PRINTF) {
            usb_guest_vprintf_tx = usb_guest_uart_tx_byte;
            int n = usb_guest_host_vprintf(cpu.r[0],
                                           usb_guest_uart_make_ap(cpu.r[13]));
            if (net_bridge_mirror_stdio) {
                fflush(stderr);
            }
            usb_guest_return_to_lr((uint32_t)n);
            return;
        }
        if (pc == USB_GUEST_WRAP_VPRINTF) {
            usb_guest_vprintf_tx = usb_guest_uart_tx_byte;
            int n = usb_guest_host_vprintf(cpu.r[0], cpu.r[1]);
            if (net_bridge_mirror_stdio) {
                fflush(stderr);
            }
            usb_guest_return_to_lr((uint32_t)n);
            return;
        }
        if (pc == USB_GUEST_STDIO_PUTSTRING_FN) {
            uint32_t buf = cpu.r[0];
            uint32_t len = cpu.r[1];
            if (len == 0xffffffffu) {
                len = 0;
                while (len < 8192u && mem_read8(buf + len) != 0) {
                    len++;
                }
            }
            usb_guest_uart_tx_buf(buf, len);
            usb_guest_return_to_lr(len);
            return;
        }
        if (pc == USB_GUEST_UART_PUTC) {
            usb_guest_uart_tx_byte((uint8_t)cpu.r[1]);
            if (net_bridge_mirror_stdio) {
                fflush(stderr);
            }
            cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
            return;
        }
        if (pc == USB_GUEST_WRAP_PUTS) {
            uint32_t str = cpu.r[0];
            for (uint32_t i = 0; i < 8192u; i++) {
                uint8_t ch = mem_read8(str + i);
                if (ch == 0) {
                    break;
                }
                usb_guest_uart_tx_byte(ch);
            }
            if (net_bridge_mirror_stdio) {
                fflush(stderr);
            }
            cpu.r[0] = 0;
            cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
            return;
        }
    }
    if (pc == USB_GUEST_U2_NET_INIT || pc == USB_GUEST_U2_MON_PUSH ||
        pc == USB_GUEST_U2_NET_POLL || pc == USB_GUEST_U2_MON_POLL_FLUSH) {
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_HW_CLAIM_LOCK) {
        mem_write8(USB_GUEST_HW_CLAIM_LOCK_BYTE, 0);
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_HW_CLAIM_OR_ASSERT) {
        uint32_t base = cpu.r[0];
        uint32_t bit = cpu.r[1];
        uint32_t byte = bit >> 3;
        uint8_t mask = (uint8_t)(1u << (bit & 7u));
        uint8_t v = mem_read8(base + byte);
        mem_write8(base + byte, v | mask);
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_HW_CLAIM_UNUSED) {
        uint32_t base = cpu.r[0];
        uint32_t start = cpu.r[2];
        uint32_t end = cpu.r[3];
        if (start <= end) {
            uint32_t byte = start >> 3;
            uint8_t mask = (uint8_t)(1u << (start & 7u));
            uint8_t v = mem_read8(base + byte);
            mem_write8(base + byte, v | mask);
            cpu.r[0] = start;
        } else {
            cpu.r[0] = 0xffffffffu;
        }
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_HW_CLAIM_CLEAR_FAIL) {
        cpu.r[15] = 0x1000a9c4u | 1u; /* pop return — clear on unclaimed bit is OK in emu */
        return;
    }
    if (pc == USB_GUEST_TS_READ_JEDECID) {
        cpu.r[0] = 0xEF4017u;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_MUTEX_ENTER_V || pc == USB_GUEST_MUTEX_EXIT_V) {
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_SPI_WR_RD_VENEER) {
        uint32_t rx = cpu.r[2];
        if (rx >= 0x20000000u && rx < 0x20082000u) {
            mem_write8(rx + 0, 0x17);
            mem_write8(rx + 1, 0x40);
            mem_write8(rx + 2, 0xEF);
            mem_write8(rx + 3, 0x00);
        }
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_SPI_READ_BLOCKING_V) {
        uint32_t rx = cpu.r[2];
        uint32_t len = cpu.r[3];
        for (uint32_t i = 0; i < len && rx != 0; i++) {
            mem_write8(rx + i, 0xFF);
        }
        cpu.r[0] = len;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_ALARM_POOL_DEFAULT) {
        mem_write32((uint32_t)(USB_GUEST_ALARM_POOL_RAM + 16u),
                    (uint32_t)(USB_GUEST_SPIN_LOCK_HW + 2u));
        cpu.r[0] = USB_GUEST_ALARM_POOL_RAM;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_ASSERT_FUNC) {
        fprintf(stderr,
                "[guest] assert file=0x%08X line=%u func=0x%08X expr=0x%08X\n",
                cpu.r[0], (unsigned)cpu.r[1], cpu.r[2], cpu.r[3]);
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_CHECK_ALLOC) {
        usb_guest_return_to_lr(cpu.r[0]);
        return;
    }
    if (pc == USB_GUEST_VFPRINTF_R &&
        (usb_mode || net_bridge_uart_active(0))) {
        usb_guest_return_to_lr(0);
        return;
    }
    if (pc == USB_GUEST_LOCALE_MB_CUR_MAX) {
        usb_guest_return_to_lr(1);
        return;
    }
    if (pc == USB_GUEST_VFPRINTF_LOOP_BACK) {
        if (++vf_locale_iters > 512u) {
            vf_locale_iters = 0;
            cpu.r[15] = USB_GUEST_VFPRINTF_LOOP_EXIT;
            return;
        }
    } else if (pc < USB_GUEST_VFPRINTF_LO || pc > USB_GUEST_VFPRINTF_HI) {
        vf_locale_iters = 0;
    }
    if (pc == USB_GUEST_ASCII_MBTOWC) {
        int ret = 0;
        if (cpu.r[1] != 0 && cpu.r[2] != 0) {
            uint8_t ch = mem_read8(cpu.r[2]);
            mem_write32(cpu.r[1], (uint32_t)ch);
            ret = 1;
        }
        usb_guest_return_to_lr((uint32_t)ret);
        return;
    }
    if (pc == USB_GUEST_ASCII_MBTOWC_LOOP) {
        usb_guest_return_to_lr(1);
        return;
    }
    if (!usb_mode) {
        return;
    }
    if (pc == USB_GUEST_WRAP_PRINTF) {
        usb_guest_vprintf_tx = usb_guest_usb_tx_byte;
        int n = usb_guest_host_vprintf(cpu.r[0],
                                       usb_guest_uart_make_ap(cpu.r[13]));
        if (usb_cdc_stdout_enabled) {
            fflush(stdout);
        }
        usb_guest_return_to_lr((uint32_t)n);
        return;
    }
    if (pc == USB_GUEST_WRAP_VPRINTF) {
        usb_guest_vprintf_tx = usb_guest_usb_tx_byte;
        int n = usb_guest_host_vprintf(cpu.r[0], cpu.r[1]);
        if (usb_cdc_stdout_enabled) {
            fflush(stdout);
        }
        usb_guest_return_to_lr((uint32_t)n);
        return;
    }
    if (pc == USB_GUEST_STDIO_USB_IN) {
        uint32_t buf = cpu.r[0];
        uint32_t len = cpu.r[1];
        uint32_t nread = 0;
        if (buf != 0 && len != 0) {
            if (len == 0xffffffffu) {
                len = 1;
            }
            for (uint32_t i = 0; i < len && i < 64u; i++) {
                uint8_t ch;
                if (!usb_guest_cdc_rx_fifo_pop(&ch)) {
                    break;
                }
                mem_write8(buf + i, ch);
                nread++;
            }
        }
        usb_guest_return_to_lr(nread);
        return;
    }
    if (pc == USB_GUEST_TUD_CDC_AVAILABLE) {
        uint32_t fifo = USB_GUEST_TUD_CDC_BASE + cpu.r[0] * 0xc8u + 16u;
        cpu.r[0] = usb_guest_fifo_count(fifo);
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_TUD_CDC_READ) {
        uint32_t itf = cpu.r[0];
        uint32_t buf = cpu.r[1];
        uint32_t len = cpu.r[2];
        uint32_t fifo = USB_GUEST_TUD_CDC_BASE + itf * 0xc8u + 16u;
        uint32_t avail = usb_guest_fifo_count(fifo);
        if (len == 0xffffu) {
            len = avail;
        }
        if (len > avail) {
            len = avail;
        }
        for (uint32_t i = 0; i < len; i++) {
            uint8_t ch;
            if (!usb_guest_cdc_rx_fifo_pop(&ch)) {
                len = i;
                break;
            }
            mem_write8(buf + i, ch);
        }
        cpu.r[0] = len;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (usb_guest_cdc_synced &&
        (pc == USB_GUEST_USB_CONN_CMP || pc == USB_GUEST_USB_CONN_CMP_DBG)) {
        cpu.r[0] = 1;
        return;
    }

    if (pc == USB_GUEST_STDIO_PUTSTRING_FN) {
        uint32_t buf = cpu.r[0];
        uint32_t len = cpu.r[1];
        if (len == 0xffffffffu) {
            len = 0;
            while (len < 8192u && mem_read8(buf + len) != 0) {
                len++;
            }
        }
        usb_console_guest_tx_buf(buf, len);
        usb_guest_return_to_lr(len);
        return;
    }
    if (pc == USB_GUEST_WRAP_PUTS) {
        uint32_t str = cpu.r[0];
        uint32_t len = 0;
        while (len < 8192u && mem_read8(str + len) != 0) {
            len++;
        }
        usb_console_guest_tx_cstr(str);
        cpu.r[0] = len;
        cpu.r[15] = (cpu.r[14] & ~1u) | 1u;
        return;
    }
    if (pc == USB_GUEST_WRAP_PUTCHAR_CALL) {
        uint8_t ch = (uint8_t)cpu.r[4];
        usb_console_tcp_tx(&ch, 1);
        if (usb_cdc_stdout_enabled) {
            fputc(ch, stdout);
            fflush(stdout);
        }
        cpu.r[15] = 0x1000dce4u | 1u;
        return;
    }
    if (pc == USB_GUEST_WRAP_PUTS_CALL) {
        usb_console_guest_tx_buf(cpu.r[0], cpu.r[1]);
        return;
    }
    if (pc == USB_GUEST_STDIO_PUTSTRING) {
        usb_console_guest_tx_buf(cpu.r[1], cpu.r[2]);
    }
}

static void usb_guest_reset_stdio_mutexes(void) {
    /* Clear recursive owner/count only — do not zero [0] (hardware spinlock pointer). */
    mem_write8(USB_GUEST_STDIO_MUTEX + 4u, 0xFFu);
    mem_write8(USB_GUEST_STDIO_MUTEX + 5u, 0u);
    mem_write8(USB_GUEST_STDIO_USB_MUTEX + 4u, 0xFFu);
    mem_write8(USB_GUEST_STDIO_USB_MUTEX + 5u, 0u);
}

static void usb_guest_seed_picow_check(void) {
    mem_write8(USB_GUEST_CHECK_PICOW_INITED, 1u);
    mem_write8(USB_GUEST_CHECK_PICOW_RESULT, 1u);
}

static void usb_guest_init_stdio_usb_driver(void) {
    static int seeded;
    if (seeded++) {
        return;
    }
    /* stdio_usb_init copies this driver template; we skip that init under emu. */
    mem_write32(USB_GUEST_STDIO_USB_DRIVER + 0u, USB_GUEST_STDIO_USB_OUT);
    mem_write32(USB_GUEST_STDIO_USB_DRIVER + 4u, USB_GUEST_STDIO_USB_FLUSH);
    mem_write32(USB_GUEST_STDIO_USB_DRIVER + 8u, USB_GUEST_STDIO_USB_IN);
    mem_write32(USB_GUEST_STDIO_USB_DRIVER + 12u, USB_GUEST_STDIO_USB_AVAIL);
    mem_write32(USB_GUEST_STDIO_USB_DRIVER + 16u, 0u);
    mem_write32(USB_GUEST_STDIO_USB_DRIVER + 20u, 0x100u);
    mem_write32(USB_GUEST_STDIO_ACTIVE_DRV, USB_GUEST_STDIO_USB_DRIVER);
}

static int usb_guest_cdc_rx_fifo_pop(uint8_t *out) {
    uint32_t fifo = USB_GUEST_TUD_CDC_RXFIFO;
    uint32_t buf = mem_read32(fifo);
    uint16_t depth = mem_read16(fifo + 4u);
    uint16_t wr = mem_read16(fifo + 8u);
    uint16_t rd = mem_read16(fifo + 10u);

    if (buf == 0 || depth == 0 || wr == rd) {
        return 0;
    }
    *out = mem_read8(buf + (uint32_t)(rd % depth));
    mem_write16(fifo + 10u, (uint16_t)(rd + 1u));
    return 1;
}

static int usb_guest_cdc_rx_fifo_push(uint8_t byte) {
    uint32_t fifo = USB_GUEST_TUD_CDC_RXFIFO;
    uint32_t buf = mem_read32(fifo);
    uint16_t depth = mem_read16(fifo + 4u);
    uint16_t wr = mem_read16(fifo + 8u);
    uint16_t rd = mem_read16(fifo + 10u);

    if (buf == 0 || depth == 0) {
        return 0;
    }
    if (usb_guest_fifo_count(fifo) >= depth) {
        return 0;
    }
    mem_write8(buf + (uint32_t)(wr % depth), byte);
    mem_write16(fifo + 8u, (uint16_t)(wr + 1u));
    return 1;
}

static void usb_guest_init_cdc_fifos(void) {
    usb_guest_init_stdio_usb_driver();

    uint32_t tx = USB_GUEST_TUD_CDC_TXFIFO;
    if (mem_read32(tx) != 0 && mem_read16(tx + 4) != 0) {
        return;
    }

    /* Mirror cdcd_init() tu_fifo_config for CDC instance 0 (megaflash.uf2). */
    uint32_t rx = USB_GUEST_TUD_CDC_RXFIFO;
    mem_write32(rx, USB_GUEST_TUD_CDC_RXBUF);
    mem_write16(rx + 4, 64);
    mem_write16(rx + 6, 1);
    mem_write16(rx + 8, 0);
    mem_write16(rx + 10, 0);

    mem_write32(tx, USB_GUEST_TUD_CDC_TXBUF);
    mem_write16(tx + 4, 64);
    mem_write16(tx + 6, 1);
    mem_write16(tx + 8, 0);
    mem_write16(tx + 10, 0);

    /* Bulk EP1 IN/OUT — typical Pico stdio_usb descriptor layout */
    mem_write8(USB_GUEST_TUD_CDC_BASE + 2, 0x81);
    mem_write8(USB_GUEST_TUD_CDC_BASE + 3, 0x01);

    fprintf(stderr, "[USB] guest CDC fifos initialized (tx buf 0x%08X)\n",
            (unsigned)USB_GUEST_TUD_CDC_TXBUF);
}

static void usb_guest_bridge_activate(void) {
    if (usb_guest_cdc_synced) {
        usb_sync_guest_cdc_connected();
        return;
    }

    usb_state.sie_status |= USB_SIE_VBUS_DETECTED | USB_SIE_CONNECTED;
    usb_state.sie_status &= ~(USB_SIE_SETUP_REC | USB_SIE_BUS_RESET);
    usb_state.buff_status = 0;
    usb_state.intf = 0;
    usb_state.ctrl_state = USB_CTRL_IDLE;
    usb_state.enum_state = USB_ENUM_ACTIVE;
    usb_state.delay = 0;

    /* Pico SDK CDC data class typically uses bulk EP1 IN/OUT */
    if (usb_state.cdc_in_ep == 0) {
        usb_state.cdc_in_ep = 1;
    }
    if (usb_state.cdc_out_ep == 0) {
        usb_state.cdc_out_ep = 1;
    }
    if (usb_state.cdc_iface == 0) {
        usb_state.cdc_iface = 1;
    }

    nvic_clear_pending(5);
    usb_guest_hw_claim_bootstrap();
    usb_guest_init_cdc_fifos();
    usb_sync_guest_cdc_connected();
    usb_guest_cdc_synced = 1;
    usb_log_cdc_active_once();
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
    uint32_t buff = usb_buff_status_for_guest();
    uint32_t sie = usb_sie_status_for_guest();

    if (buff) {
        intr |= USB_INTR_BUFF_STATUS;
    }
    if (sie & USB_SIE_BUS_RESET) {
        intr |= USB_INTR_BUS_RESET;
    }
    if (sie & USB_SIE_SETUP_REC) {
        intr |= USB_INTR_SETUP_REQ;
    }
    if (usb_state.sie_status & USB_SIE_TRANS_COMPLETE) {
        intr |= USB_INTR_TRANS_COMPLETE;
    }
    return intr;
}

static void usb_fire_irq(void) {
    uint32_t intr = usb_compute_intr();
    uint32_t ints = (intr | usb_state.intf) & usb_state.inte;
    if (ints) {
        nvic_signal_irq(5);  /* USBCTRL_IRQ */
    }
}

static void usb_kick_stalled_ep0(void) {
    if (usb_state.ctrl_state != USB_CTRL_WAIT_STATUS_IN &&
        usb_state.ctrl_state != USB_CTRL_WAIT_STATUS_OUT &&
        usb_state.ctrl_state != USB_CTRL_SETUP_SENT) {
        usb_ctrl_stall_steps = 0;
        return;
    }

    usb_ctrl_stall_steps++;
    if (usb_ctrl_stall_steps < 8) {
        return;
    }

    if (usb_state.ctrl_state == USB_CTRL_WAIT_STATUS_IN) {
        uint32_t buf_ctrl = dpram_read32(USB_DPRAM_BUF_CTRL);
        if (!(buf_ctrl & USB_BUF_CTRL_AVAILABLE)) {
            buf_ctrl |= USB_BUF_CTRL_AVAILABLE;
            dpram_write32(USB_DPRAM_BUF_CTRL, buf_ctrl);
            usb_state.sie_status |= USB_SIE_TRANS_COMPLETE;
            usb_fire_irq();
        }
    } else if (usb_state.ctrl_state == USB_CTRL_WAIT_STATUS_OUT) {
        uint32_t buf_ctrl = dpram_read32(USB_DPRAM_BUF_CTRL + 4);
        if (!(buf_ctrl & USB_BUF_CTRL_AVAILABLE)) {
            buf_ctrl |= USB_BUF_CTRL_AVAILABLE;
            dpram_write32(USB_DPRAM_BUF_CTRL + 4, buf_ctrl);
            usb_state.sie_status |= USB_SIE_TRANS_COMPLETE;
            usb_fire_irq();
        }
    } else if (usb_state.ctrl_state == USB_CTRL_SETUP_SENT &&
               (usb_state.sie_status & USB_SIE_SETUP_REC) &&
               usb_ctrl_stall_steps > 256) {
        /* Firmware read setup but did not W1C SETUP_REC — unblock host model */
        usb_state.sie_status &= ~USB_SIE_SETUP_REC;
    }

    if (usb_ctrl_stall_steps >= 64 &&
        usb_state.dpram[1] == 0x22 &&
        (usb_state.enum_state == USB_ENUM_CDC_SET_CTRL_LINE_DONE ||
         usb_state.enum_state == USB_ENUM_CDC_SET_CTRL_LINE)) {
        usb_sync_guest_cdc_connected();
        usb_state.ctrl_state = USB_CTRL_DONE;
        if (usb_state.enum_state != USB_ENUM_ACTIVE) {
            usb_state.enum_state = USB_ENUM_ACTIVE;
            usb_guest_cdc_synced = 1;
            usb_log_cdc_active_once();
        }
    }
}

/* ========================================================================
 * Setup Packet Injection
 * ======================================================================== */

static void usb_send_setup(uint8_t bmRequestType, uint8_t bRequest,
                           uint16_t wValue, uint16_t wIndex, uint16_t wLength) {
    /* -usb-console: guest-only bridge; host SETUP packets trap TinyUSB. */
    if (usb_console_bridge_mode()) {
        return;
    }

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
            usb_state.enum_state = USB_ENUM_CDC_SET_CTRL_LINE_DONE;
        }
        break;

    case USB_ENUM_CDC_SET_CTRL_LINE_DONE:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            usb_state.ctrl_state = USB_CTRL_IDLE;
            usb_state.enum_state = USB_ENUM_ACTIVE;
            usb_sync_guest_cdc_connected();
            usb_guest_cdc_synced = 1;
            usb_log_cdc_active_once();
        }
        break;

    case USB_ENUM_ACTIVE:
        if (usb_state.ctrl_state == USB_CTRL_DONE) {
            usb_state.ctrl_state = USB_CTRL_IDLE;
        }
        if (!usb_guest_cdc_synced) {
            usb_sync_guest_cdc_connected();
            usb_guest_cdc_synced = 1;
            usb_log_cdc_active_once();
        }
        break;
    }
}

/* ========================================================================
 * CDC Data Path
 * ======================================================================== */

static void usb_handle_cdc_ep_in(int ep) {
    uint32_t buf_ctrl_off = USB_DPRAM_BUF_CTRL + (uint32_t)ep * 8;  /* IN */
    uint32_t buf_ctrl = dpram_read32(buf_ctrl_off);
    int len = (int)(buf_ctrl & USB_BUF_CTRL_LEN_MASK);

    if (usb_console_bridge_mode()) {
        if (!(buf_ctrl & USB_BUF_CTRL_AVAILABLE) || len <= 0) {
            return;
        }
    } else if (!(buf_ctrl & USB_BUF_CTRL_FULL) || len <= 0) {
        return;
    }

    uint32_t ep_ctrl_off = USB_DPRAM_EP_CTRL + (uint32_t)(ep - 1) * 8;  /* IN control */
    uint32_t ep_ctrl = dpram_read32(ep_ctrl_off);
    uint32_t buf_addr = ep_ctrl & 0xFFC0;

    if (buf_addr > 0 && buf_addr + (uint32_t)len <= USBCTRL_DPRAM_SIZE) {
        const uint8_t *payload = &usb_state.dpram[buf_addr];
        if (usb_tcp.client_fd >= 0) {
            static int logged_first_tx;
            if (!logged_first_tx) {
                logged_first_tx = 1;
                fprintf(stderr, "[USB] CDC IN ep%d first TX: %d bytes to TCP\n", ep, len);
            }
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

    buf_ctrl &= ~(USB_BUF_CTRL_AVAILABLE | USB_BUF_CTRL_FULL);
    dpram_write32(buf_ctrl_off, buf_ctrl);
    usb_state.buff_status |= (1u << (ep * 2));  /* EPn_IN complete */
    if (usb_console_bridge_mode()) {
        usb_fire_irq();
    } else {
        usb_state.buff_status &= ~(1u << (ep * 2));
        usb_fire_irq();
    }
}

static void usb_handle_cdc(void);

static void usb_dpram_write_hook(uint32_t off) {
    if (!usb_console_bridge_mode() || !usb_guest_cdc_synced) {
        return;
    }
    if (off < USB_DPRAM_BUF_CTRL || off >= USB_DPRAM_BUF_CTRL + 16u * 8u) {
        return;
    }
    uint32_t delta = off - USB_DPRAM_BUF_CTRL;
    if ((delta % 8u) != 0) {
        return;  /* OUT half of pair */
    }
    int ep = (int)(delta / 8u);
    if (ep > 0) {
        usb_handle_cdc_ep_in(ep);
    }
}

static void usb_handle_cdc(void) {
    if (usb_console_bridge_mode()) {
        for (int ep = 1; ep < 16; ep++) {
            usb_handle_cdc_ep_in(ep);
        }
        return;
    }

    if (usb_state.cdc_in_ep == 0) {
        return;
    }
    usb_handle_cdc_ep_in(usb_state.cdc_in_ep);
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
static void usb_cdc_rx_drain_ep(int ep) {
    if (usb_state.cdc_rx_count == 0) {
        return;
    }
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
            if (!usb_console_bridge_mode()) {
                usb_state.buff_status |= (1u << (ep * 2 + 1));  /* EPn_OUT */
                usb_fire_irq();
            }
        }
    }
}

static void usb_cdc_rx_drain(void) {
    if (usb_state.cdc_rx_count == 0) {
        return;
    }

    if (usb_console_bridge_mode()) {
        for (int ep = 1; ep < 16; ep++) {
            usb_cdc_rx_drain_ep(ep);
        }
        return;
    }

    if (usb_state.cdc_out_ep == 0) {
        return;
    }
    usb_cdc_rx_drain_ep(usb_state.cdc_out_ep);
}

/* ========================================================================
 * USB Step (called from main loop)
 * ======================================================================== */

static uint16_t usb_guest_fifo_count(uint32_t fifo_addr) {
    uint16_t depth = mem_read16(fifo_addr + 4);
    uint16_t wr_idx = mem_read16(fifo_addr + 8);
    uint16_t rd_idx = mem_read16(fifo_addr + 10);

    if (depth == 0) {
        return 0;
    }
    if (wr_idx >= rd_idx) {
        return wr_idx - rd_idx;
    }
    return (uint16_t)(depth - rd_idx + wr_idx);
}

/* TinyUSB CDC tx fifo lives in guest RAM; tap it when HW IN xfers never complete. */
static void usb_bridge_drain_cdc_tx_fifo(void) {
    if (!usb_console_bridge_mode() || !usb_guest_cdc_synced) {
        return;
    }

    uint32_t fifo_addr = USB_GUEST_TUD_CDC_TXFIFO;
    uint32_t buf_ptr = mem_read32(fifo_addr);
    uint16_t depth = mem_read16(fifo_addr + 4);
    uint16_t rd_idx = mem_read16(fifo_addr + 10);
    uint16_t count = usb_guest_fifo_count(fifo_addr);

    if (buf_ptr == 0 || count == 0) {
        return;
    }

    uint16_t chunk = count;
    if (chunk > 512) {
        chunk = 512;
    }

    uint8_t tmp[512];
    for (uint16_t i = 0; i < chunk; i++) {
        uint16_t idx = (uint16_t)((rd_idx + i) % depth);
        tmp[i] = mem_read8(buf_ptr + idx);
    }

    if (usb_tcp.client_fd >= 0) {
        static int logged_first_fifo;
        if (!logged_first_fifo) {
            logged_first_fifo = 1;
            fprintf(stderr, "[USB] CDC fifo tap: first %u bytes to TCP\n", chunk);
        }
        usb_console_tcp_tx(tmp, chunk);
    }
    if (usb_cdc_stdout_enabled) {
        fwrite(tmp, 1, chunk, stdout);
        fflush(stdout);
        if (__builtin_expect(expect_enabled, 0)) {
            expect_append((const char *)tmp, chunk);
        }
    }

    rd_idx = (uint16_t)((rd_idx + chunk) % depth);
    mem_write16(fifo_addr + 10, rd_idx);
}

static void usb_unstick_guest_control(void) {
    uint32_t pc = cores[CORE0].r[15];
    if (pc < 0x1000F5C0u || pc > 0x1000F5CCu) {
        return;
    }
    mem_write8(USB_GUEST_TUD_DEVICE_STATE + USB_GUEST_TUD_CTRL_BUSY_OFF, 1);
}

static void usb_escape_stuck_tinyusb(void) {
    uint32_t pc = cores[CORE0].r[15];

    if (pc < 0x1000F400u || pc >= 0x1000F800u) {
        return;
    }

    /* Let process_control_request observe SET_CONTROL_LINE_STATE complete */
    mem_write8(USB_GUEST_TUD_DEVICE_STATE + USB_GUEST_TUD_CTRL_BUSY_OFF, 1);
}

static void usb_step_console_bridge(void) {
    usb_console_tcp_poll();

    static int steps;
    steps++;

    if (!usb_guest_cdc_synced &&
        ((usb_state.sie_ctrl & USB_SIE_CTRL_PULLUP_EN) ||
         (usb_state.main_ctrl & USB_MAIN_CTRL_EN) ||
         usb_tcp.client_fd >= 0 ||
         steps >= 5000)) {
        usb_guest_bridge_activate();
    }

    if (usb_guest_cdc_synced) {
        usb_sync_guest_cdc_connected();
        usb_guest_seed_picow_check();
        usb_guest_reset_stdio_mutexes();
        usb_guest_init_cdc_fifos();
        usb_unstick_guest_control();
        usb_escape_stuck_tinyusb();
        usb_handle_cdc();
        usb_bridge_drain_cdc_tx_fifo();
        usb_cdc_rx_drain();
    }
}

void usb_step(void) {
    static usb_enum_state_t trace_last_enum = USB_ENUM_DISABLED;
    static usb_ctrl_state_t trace_last_ctrl = USB_CTRL_IDLE;

    if (usb_console_bridge_mode()) {
        usb_step_console_bridge();
        return;
    }

    usb_console_tcp_poll();

    int host_active = (usb_state.main_ctrl & USB_MAIN_CTRL_EN) ||
                      (usb_state.sie_ctrl & USB_SIE_CTRL_PULLUP_EN) ||
                      (usb_state.enum_state != USB_ENUM_DISABLED);

    if (!host_active) {
        return;
    }

    if ((usb_state.main_ctrl & USB_MAIN_CTRL_EN) && !usb_logged_host_enable) {
        usb_logged_host_enable = 1;
        if (usb_enum_trace_enabled) {
            usb_trace_status("controller enabled");
        }
    }

    /* First time device is active: start enumeration */
    if (usb_state.enum_state == USB_ENUM_DISABLED) {
        usb_state.enum_state = USB_ENUM_WAIT_PULLUP;
        usb_state.sie_status |= USB_SIE_VBUS_DETECTED;
        if (usb_enum_trace_enabled) {
            usb_trace_status("VBUS detected, waiting for pull-up");
        }
    }

    /* Advance control transfer */
    usb_ctrl_step();
    usb_kick_stalled_ep0();

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
        }
    }

    /* Handle CDC data when active */
    if (usb_state.enum_state == USB_ENUM_ACTIVE) {
        usb_handle_cdc();
        usb_cdc_rx_drain();
    }
}

/* ========================================================================
 * Init / Match / Read / Write
 * ======================================================================== */

void usb_init(void) {
    memset(&usb_state, 0, sizeof(usb_state_t));
    usb_state.ep_abort_done = 0xFFFFFFFF;
    usb_tcp_logged_ready = 0;
    usb_logged_host_enable = 0;
    usb_ctrl_stall_steps = 0;
    usb_guest_cdc_synced = 0;
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
        return usb_sie_status_for_guest();
    case USB_INT_EP_CTRL:
        return usb_state.int_ep_ctrl;
    case USB_BUFF_STATUS:
        return usb_buff_status_for_guest();
    case USB_BUFF_CPU_SHOULD_HANDLE:
        return usb_buff_status_for_guest();
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
        usb_dpram_write_hook(off);
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
        if (usb_console_bridge_mode() &&
            (usb_state.main_ctrl & USB_MAIN_CTRL_EN)) {
            usb_guest_bridge_activate();
        }
        if (usb_enum_trace_enabled) {
            usb_trace_status("write MAIN_CTRL");
        }
        break;
    case USB_SIE_CTRL:
        ALIAS_APPLY(usb_state.sie_ctrl);
        if (usb_state.sie_ctrl & USB_SIE_CTRL_PULLUP_EN) {
            usb_state.sie_status |= USB_SIE_VBUS_DETECTED;
            if (usb_console_bridge_mode()) {
                usb_guest_bridge_activate();
            }
        }
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

#ifndef USB_GUEST_H
#define USB_GUEST_H

#include <stdint.h>

/* Host-callable MegaFlash / SPI guest helpers used by USB console and overlays. */

void usb_guest_return_to_lr(uint32_t ret);
void usb_guest_set_vprintf_tx(void (*tx)(uint8_t ch));
uint32_t usb_guest_uart_make_ap(uint32_t sp);
int usb_guest_host_vprintf(uint32_t fmt, uint32_t ap);
uint32_t usb_guest_sprintf_make_ap(uint32_t sp);
int usb_guest_host_sprintf(uint32_t dest, uint32_t fmt, uint32_t ap, uint32_t cap);

void usb_guest_hw_claim_bootstrap(void);
void usb_guest_init_default_config(void);
void usb_guest_stub_save_user_settings(void);
void usb_guest_persist_config_to_host(void);
void usb_guest_fill_device_info_string(uint32_t dest);
void usb_guest_stub_get_config_byte1(void);
void usb_guest_stub_get_config_byte2(void);
void usb_guest_init_flash_stub(void);
void usb_guest_setup_flash_unit_mapping_stub(void);
uint32_t usb_guest_flash_unit_total(void);
void usb_guest_stub_get_total_unit_count(void);
void usb_guest_stub_is_valid_unit_num(void);
void usb_guest_stub_get_block_count(void);
void usb_guest_stub_get_volume_info(void);
void usb_guest_stub_copy_memory_entry(void);
void usb_guest_stub_read_block(void);
void usb_guest_stub_write_block(void);
void usb_guest_stub_write_block_for_image_transfer(void);
/* Write one ProDOS block for TFTP/XMODEM image path (logical unit). */
int usb_guest_flash_write_image_block(uint32_t logical_unit, uint32_t block,
                                      const uint8_t *data);

#endif /* USB_GUEST_H */

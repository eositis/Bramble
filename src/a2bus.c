#include "a2bus.h"
#include "gpio.h"
#include "pio.h"
#include <stdio.h>

static void a2bus_set_bus_pins(uint32_t busdata)
{
    for (int i = 0; i < A2BUS_GPIO_BUS_WIDTH; i++) {
        uint8_t pin = (uint8_t)(A2BUS_GPIO_BUS_BASE + i);
        gpio_set_input_pin(pin, (busdata >> i) & 1u);
    }
}

void a2bus_gpio_idle(void)
{
    gpio_set_input_pin(A2BUS_GPIO_NDEVSEL, 1);
    gpio_set_input_pin(A2BUS_GPIO_PHI0, 0);
    a2bus_set_bus_pins(0);
}

/* After a synthetic bus cycle, nudge listener SM0 RX so BusLoop sees FIFO data. */
static void a2bus_notify_listener(uint32_t busdata)
{
    pio_inject_rx(0, 0, busdata & 0x1Fu);
}

void a2bus_phi0_pulse_for_detect(void)
{
    /* IsAppleConnected() samples PHI0 for an edge in a tight loop. */
    gpio_set_input_pin(A2BUS_GPIO_PHI0, 0);
    a2bus_pio_burst(4);
    gpio_set_input_pin(A2BUS_GPIO_PHI0, 1);
    a2bus_pio_burst(4);
    gpio_set_input_pin(A2BUS_GPIO_PHI0, 0);
    a2bus_pio_burst(4);
}

static void a2bus_run_slot_cycle(uint32_t busdata)
{
    /* a2buslistener: wait nDEVSEL low -> sample -> wait PHI0 low -> wait nDEVSEL high */
    gpio_set_input_pin(A2BUS_GPIO_NDEVSEL, 1);
    gpio_set_input_pin(A2BUS_GPIO_PHI0, 0);
    a2bus_pio_burst(8);

    a2bus_set_bus_pins(busdata);
    gpio_set_input_pin(A2BUS_GPIO_NDEVSEL, 0);
    gpio_set_input_pin(A2BUS_GPIO_PHI0, 1);
    a2bus_pio_burst(64);

    gpio_set_input_pin(A2BUS_GPIO_PHI0, 0);
    a2bus_pio_burst(32);

    gpio_set_input_pin(A2BUS_GPIO_NDEVSEL, 1);
    a2bus_pio_burst(32);
    a2bus_notify_listener(busdata);
}

void a2bus_inject_read(uint8_t addr_nibble)
{
    uint32_t busdata = (addr_nibble & 0xFu) | A2BUS_READ_FLAG;
    a2bus_run_slot_cycle(busdata);
}

void a2bus_inject_write(uint8_t addr_nibble, uint8_t data)
{
    uint32_t busdata = (addr_nibble & 0xFu) | ((uint32_t)data << 5);
    a2bus_run_slot_cycle(busdata);
}

void a2bus_pio_burst(unsigned steps)
{
    for (unsigned i = 0; i < steps; i++) {
        pio_step();
    }
}

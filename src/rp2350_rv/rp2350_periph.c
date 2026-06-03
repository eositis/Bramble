/*
 * RP2350-Specific Peripheral Emulation
 *
 * Implements TICKS, POWMAN, QMI, OTP, BOOTRAM, TIMER1, GLITCH,
 * CORESIGHT, and ACCESSCTRL peripherals for RP2350 emulation.
 */

#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include "rp2350_rv/rp2350_periph.h"
#include "rp2350_rv/rp2350_memmap.h"
#include "nvic.h"
#include "timer.h"
#include "emulator.h"


/* RP2350 timer adds LOCKED (0x34) and SOURCE (0x38); INTR/INTE/INTF/INTS are +8 vs RP2040. */
enum {
    TIMER2350_MAP_DIRECT = 0,
    TIMER2350_MAP_LOCKED = 1,
    TIMER2350_MAP_SOURCE = 2,
    TIMER2350_MAP_IGNORE = 3,
};

static int timer_rp2350_translate_offset(uint32_t offset, uint32_t *rp2040_offset) {
    if (offset <= 0x30) {
        *rp2040_offset = offset;
        return TIMER2350_MAP_DIRECT;
    }
    switch (offset) {
    case 0x34: return TIMER2350_MAP_LOCKED;
    case 0x38: return TIMER2350_MAP_SOURCE;
    case 0x3C: *rp2040_offset = 0x34; return TIMER2350_MAP_DIRECT; /* INTR */
    case 0x40: *rp2040_offset = 0x38; return TIMER2350_MAP_DIRECT; /* INTE */
    case 0x44: *rp2040_offset = 0x3C; return TIMER2350_MAP_DIRECT; /* INTF */
    case 0x48: *rp2040_offset = 0x40; return TIMER2350_MAP_DIRECT; /* INTS */
    default: return TIMER2350_MAP_IGNORE;
    }
}

/* RP2040-style atomic aliases: XOR +0x1000, SET +0x2000, CLR +0x3000 */
static void timer_rp2040_write_alias(uint32_t rp2040_reg, uint32_t alias, uint32_t val) {
    if (alias == 0x0000) {
        timer_write32(rp2040_reg, val);
    } else {
        uint32_t cur = timer_read32(rp2040_reg);
        if (alias == 0x2000) {
            val = cur | val;
        } else if (alias == 0x3000) {
            val = cur & ~val;
        } else { /* XOR 0x1000 */
            val = cur ^ val;
        }
        timer_write32(rp2040_reg, val);
    }
}

static void timer_log_inte(int timer_num, uint32_t val) {
    static int logged[2];
    if (val != 0 && !logged[timer_num]) {
        logged[timer_num] = 1;
        fprintf(stderr, "[Init] TIMER%d INTE=0x%X (alarm IRQs armed)\n", timer_num, val & 0xF);
    }
}

static void timer_bus_trace_write(uint32_t addr, uint32_t val, int timer_num) {
    static int counts[2];
    if (counts[timer_num]++ < 8) {
        fprintf(stderr, "[TimerBus] TIMER%d write 0x%08X = 0x%08X\n", timer_num, addr, val);
    }
}

static uint32_t timer1_read(rp2350_timer1_state_t *t, uint32_t offset);
static void timer1_write(rp2350_timer1_state_t *t, uint32_t offset, uint32_t val);

static void timer1_write_alias(rp2350_timer1_state_t *t, uint32_t offset, uint32_t alias, uint32_t val) {
    if (alias == 0x0000) {
        timer1_write(t, offset, val);
        return;
    }
    uint32_t cur = timer1_read(t, offset);
    if (alias == 0x2000) {
        timer1_write(t, offset, cur | val);
    } else if (alias == 0x3000) {
        timer1_write(t, offset, cur & ~val);
    } else {
        timer1_write(t, offset, cur ^ val);
    }
}

/* ========================================================================
 * Initialization
 * ======================================================================== */

void rp2350_periph_init(rp2350_periph_state_t *state) {
    static const uint32_t bootram_xip_reentry_stub = 0x00008067u; /* jalr x0, 0(ra) */

    memset(state, 0, sizeof(*state));

    /* TICKS: enable proc0/proc1/timer0 by default (1 tick per cycle) */
    state->ticks.ctrl[0] = 1;  /* PROC0 enabled */
    state->ticks.ctrl[1] = 1;  /* PROC1 enabled */
    state->ticks.ctrl[2] = 1;  /* TIMER0 enabled */
    state->ticks.ctrl[3] = 1;  /* TIMER1 enabled */
    state->ticks.ctrl[4] = 1;  /* WATCHDOG enabled */

    /* POWMAN: default power state (all domains on) */
    state->powman.state = 0x0000000F;  /* All domains powered */
    state->powman.vreg_ctrl = 0x000000B1;  /* Default VREG (1.1V, enabled) */
    state->powman.bod_ctrl = 0x00000091;   /* Default BOD (enabled) */

    /* QMI: default flash read command (03h, standard SPI) */
    state->qmi.m0_rcmd = 0x03000000;
    state->qmi.direct_csr = 0x01;  /* EN=1 */

    /* OTP: unprogrammed (all 0xFFFF) */
    memset(state->otp.data, 0xFF, sizeof(state->otp.data));

    /* BOOTRAM: cleared */
    memset(state->bootram, 0, BOOTRAM_SIZE);
    state->bootram_bootlock_stat = 0xFF;  /* All bootlocks start unclaimed. */
    /* The RP2350 SDK later copies BOOTRAM_BASE into a RAM buffer and calls it
     * to "re-enter XIP". In the emulator XIP stays accessible, so a simple
     * return stub is sufficient and avoids jumping into zero-filled RAM. */
    memcpy(state->bootram, &bootram_xip_reentry_stub, sizeof(bootram_xip_reentry_stub));

    /* Timer1: starts at 0 */
    state->timer1.time_us = 0;

    /* ACCESSCTRL: default all-access */
    memset(state->accessctrl_regs, 0xFF, sizeof(state->accessctrl_regs));
}

/* ========================================================================
 * Address Matching
 * ======================================================================== */

int rp2350_periph_match(uint32_t addr) {
    uint32_t base = addr & ~0x3000u;  /* Strip atomic aliases */

    /* TICKS */
    if (base >= RP2350_TICKS_BASE && base < RP2350_TICKS_BASE + 0x100) return 1;
    /* POWMAN */
    if (base >= RP2350_POWMAN_BASE && base < RP2350_POWMAN_BASE + 0x100) return 1;
    /* QMI */
    if (base >= RP2350_QMI_BASE && base < RP2350_QMI_BASE + 0x100) return 1;
    /* OTP controller */
    if (base >= RP2350_OTP_BASE && base < RP2350_OTP_BASE + 0x100) return 1;
    /* OTP data */
    if (addr >= RP2350_OTP_DATA_BASE && addr < RP2350_OTP_DATA_BASE + OTP_NUM_ROWS * 4) return 1;
    /* BOOTRAM scratch plus adjacent bootrom-owned registers */
    if (addr >= RP2350_BOOTRAM_BASE && addr < RP2350_BOOTRAM_BASE + BOOTRAM_REGS_END) return 1;
    /* GLITCH */
    if (base >= RP2350_GLITCH_BASE && base < RP2350_GLITCH_BASE + 0x20) return 1;
    /* CORESIGHT */
    if (base >= RP2350_CORESIGHT_BASE && base < RP2350_CORESIGHT_BASE + 0x40) return 1;
    /* ACCESSCTRL */
    if (base >= RP2350_ACCESSCTRL_BASE && base < RP2350_ACCESSCTRL_BASE + 0x100) return 1;

    return 0;
}

/* ========================================================================
 * TICKS (0x40108000)
 * Each generator: CTRL at +0, CYCLES at +4, 8 bytes per generator
 * ======================================================================== */

static uint32_t ticks_read(rp2350_ticks_state_t *t, uint32_t offset) {
    uint32_t gen = offset / 8;
    uint32_t reg = offset % 8;
    if (gen >= RP2350_TICKS_NUM_GENERATORS) return 0;
    return (reg == 0) ? t->ctrl[gen] : t->cycles[gen];
}

static void ticks_write(rp2350_ticks_state_t *t, uint32_t offset, uint32_t val) {
    uint32_t gen = offset / 8;
    uint32_t reg = offset % 8;
    if (gen >= RP2350_TICKS_NUM_GENERATORS) return;
    if (reg == 0) t->ctrl[gen] = val;
    /* CYCLES is read-only */
}

/* ========================================================================
 * POWMAN (0x40100000)
 * ======================================================================== */

static uint32_t powman_read(rp2350_powman_state_t *p, uint32_t offset) {
    switch (offset) {
    case 0x00: return p->vreg_ctrl;
    case 0x04: return p->vreg_ctrl | 0x00001000;  /* VREG_STATUS: ROK=1 */
    case 0x08: return p->bod_ctrl;
    case 0x0C: return p->bod_ctrl | 0x00001000;   /* BOD_STATUS: OK=1 */
    case 0x10: return p->state;
    case 0x50: return (uint32_t)p->timer;
    case 0x54: return p->timer_hi;
    case 0x60: return p->inte;
    case 0x64: return p->intf;
    case 0x68: return p->ints;
    default:
        if (offset / 4 < 32) return p->regs[offset / 4];
        return 0;
    }
}

static void powman_write(rp2350_powman_state_t *p, uint32_t offset, uint32_t val) {
    switch (offset) {
    case 0x00: p->vreg_ctrl = val; break;
    case 0x08: p->bod_ctrl = val; break;
    case 0x60: p->inte = val; break;
    case 0x64: p->intf = val; break;
    default:
        if (offset / 4 < 32) p->regs[offset / 4] = val;
        break;
    }
}

/* ========================================================================
 * QMI (0x400D0000)
 * ======================================================================== */

static uint32_t qmi_read(rp2350_qmi_state_t *q, uint32_t offset) {
    switch (offset) {
    case 0x00: return q->direct_csr | 0x00040000;  /* BUSY=0, EN=1 */
    case 0x04: return q->direct_tx;
    case 0x08: return q->direct_rx;
    case 0x0C: return q->m0_timing;
    case 0x10: return q->m0_rfmt;
    case 0x14: return q->m0_rcmd;
    case 0x18: return q->m0_wfmt;
    case 0x1C: return q->m0_wcmd;
    case 0x20: return q->m1_timing;
    case 0x24: return q->m1_rfmt;
    case 0x28: return q->m1_rcmd;
    case 0x2C: return q->m1_wfmt;
    case 0x30: return q->m1_wcmd;
    default:
        if (offset >= 0x34 && offset < 0x54)
            return q->atrans[(offset - 0x34) / 4];
        return 0;
    }
}

static void qmi_write(rp2350_qmi_state_t *q, uint32_t offset, uint32_t val) {
    switch (offset) {
    case 0x00: q->direct_csr = val; break;
    case 0x04: q->direct_tx = val; break;
    case 0x0C: q->m0_timing = val; break;
    case 0x10: q->m0_rfmt = val; break;
    case 0x14: q->m0_rcmd = val; break;
    case 0x18: q->m0_wfmt = val; break;
    case 0x1C: q->m0_wcmd = val; break;
    case 0x20: q->m1_timing = val; break;
    case 0x24: q->m1_rfmt = val; break;
    case 0x28: q->m1_rcmd = val; break;
    case 0x2C: q->m1_wfmt = val; break;
    case 0x30: q->m1_wcmd = val; break;
    default:
        if (offset >= 0x34 && offset < 0x54)
            q->atrans[(offset - 0x34) / 4] = val;
        break;
    }
}

/* ========================================================================
 * OTP (0x40120000 control, 0x40130000 data readout)
 * ======================================================================== */

static uint32_t otp_read(rp2350_otp_state_t *o, uint32_t addr) {
    if (addr >= RP2350_OTP_DATA_BASE) {
        /* Data readout: each row is 32-bit aligned, returns 16-bit data in low halfword */
        uint32_t row = (addr - RP2350_OTP_DATA_BASE) / 4;
        if (row < OTP_NUM_ROWS) return o->data[row];
        return 0;
    }
    /* Controller registers */
    uint32_t offset = (addr - RP2350_OTP_BASE) & 0xFFF;
    if (offset / 4 < 32) return o->ctrl_regs[offset / 4];
    return 0;
}

static void otp_write(rp2350_otp_state_t *o, uint32_t addr, uint32_t val) {
    if (addr >= RP2350_OTP_DATA_BASE) return;  /* Data is read-only */
    uint32_t offset = (addr - RP2350_OTP_BASE) & 0xFFF;
    if (offset / 4 < 32) o->ctrl_regs[offset / 4] = val;
}

/* ========================================================================
 * BOOTRAM register bank (0x400E0000)
 * ======================================================================== */

static uint32_t bootram_read(rp2350_periph_state_t *state, uint32_t addr) {
    uint32_t offset = addr - RP2350_BOOTRAM_BASE;

    if (offset < BOOTRAM_SIZE) {
        uint32_t val = 0;
        memcpy(&val, &state->bootram[offset], 4);
        return val;
    }

    switch (offset) {
    case BOOTRAM_WRITE_ONCE_OFFSET:
        return state->bootram_write_once[0];
    case BOOTRAM_WRITE_ONCE_OFFSET + 4:
        return state->bootram_write_once[1];
    case BOOTRAM_BOOTLOCK_STAT_OFFSET:
        return state->bootram_bootlock_stat;
    default:
        if (offset >= BOOTRAM_BOOTLOCK0_OFFSET &&
            offset < BOOTRAM_BOOTLOCK0_OFFSET + BOOTRAM_BOOTLOCK_COUNT * 4) {
            uint32_t lock_num = (offset - BOOTRAM_BOOTLOCK0_OFFSET) / 4;
            uint32_t bit = 1u << lock_num;
            if (state->bootram_bootlock_stat & bit) {
                state->bootram_bootlock_stat &= ~bit;
                return bit;
            }
            return 0;
        }
        return 0;
    }
}

static void bootram_write(rp2350_periph_state_t *state, uint32_t addr, uint32_t val) {
    uint32_t offset = addr - RP2350_BOOTRAM_BASE;

    if (offset < BOOTRAM_SIZE) {
        memcpy(&state->bootram[offset], &val, 4);
        return;
    }

    switch (offset) {
    case BOOTRAM_WRITE_ONCE_OFFSET:
        state->bootram_write_once[0] |= val;
        break;
    case BOOTRAM_WRITE_ONCE_OFFSET + 4:
        state->bootram_write_once[1] |= val;
        break;
    case BOOTRAM_BOOTLOCK_STAT_OFFSET:
        state->bootram_bootlock_stat = val & 0xFF;
        break;
    default:
        if (offset >= BOOTRAM_BOOTLOCK0_OFFSET &&
            offset < BOOTRAM_BOOTLOCK0_OFFSET + BOOTRAM_BOOTLOCK_COUNT * 4) {
            uint32_t lock_num = (offset - BOOTRAM_BOOTLOCK0_OFFSET) / 4;
            state->bootram_bootlock_stat |= (1u << lock_num);
        }
        break;
    }
}

/* ========================================================================
 * TIMER1 (0x400B8000) — RP2350 register layout (not RP2040 offsets)
 * ======================================================================== */

static void timer1_fire_alarm(rp2350_timer1_state_t *t, int i) {
    t->intr |= (1u << i);
    t->armed &= ~(1u << i);
    if (t->inte & (1u << i)) {
        nvic_signal_irq(4 + i); /* TIMER1_IRQ_0..3 */
    }
    
}

static uint32_t timer1_read(rp2350_timer1_state_t *t, uint32_t offset) {
    switch (offset) {
    case 0x08:
        /* TIMEHR; nonzero only when alarm INTR pending (do not fake timeout here —
         * a nonzero byte at timer_hw+8 makes firmware blx through alarm registers). */
        if (t->intr & 0xF)
            return 1;
        return (uint32_t)(t->time_us >> 32);
    case 0x0C:
        t->latched_high = (uint32_t)(t->time_us >> 32);
        return (uint32_t)t->time_us;  /* TIMELR */
    case 0x10: return t->alarm[0];
    case 0x14: return t->alarm[1];
    case 0x18: return t->alarm[2];
    case 0x1C: return t->alarm[3];
    case 0x20: return t->armed;
    case 0x24: return (uint32_t)(t->time_us >> 32);  /* TIMERAWH */
    case 0x28: return (uint32_t)t->time_us;           /* TIMERAWL */
    case 0x30: return t->paused;
    case 0x34: return 0;  /* LOCKED — not latched in this model */
    case 0x38: return 0;  /* SOURCE */
    case 0x3C: return t->intr;
    case 0x40: return t->inte;
    case 0x44: return t->intf;
    case 0x48: return (t->intr | t->intf) & t->inte;  /* INTS */
    default: return 0;
    }
}

static void timer1_write(rp2350_timer1_state_t *t, uint32_t offset, uint32_t val) {
    switch (offset) {
    case 0x00: t->time_us = (t->time_us & 0xFFFFFFFF) | ((uint64_t)val << 32); break;
    case 0x04: t->time_us = (t->time_us & 0xFFFFFFFF00000000ULL) | val; break;
    case 0x10: t->alarm[0] = val; t->armed |= 1; break;
    case 0x14: t->alarm[1] = val; t->armed |= 2; break;
    case 0x18: t->alarm[2] = val; t->armed |= 4; break;
    case 0x1C: t->alarm[3] = val; t->armed |= 8; break;
    case 0x20: t->armed &= ~val; break;  /* W1C */
    case 0x30: t->paused = val & 1; break;
    case 0x34: /* LOCKED */ break;
    case 0x38: /* SOURCE */ break;
    case 0x3C: t->intr &= ~val; break;   /* INTR W1C */
    case 0x40:
        t->inte = val & 0xF;
        timer_log_inte(1, t->inte);
        {
            uint32_t ints = (t->intr | t->intf) & t->inte;
            for (int i = 0; i < 4; i++) {
                if (ints & (1u << i)) {
                    nvic_signal_irq(4 + i);
                }
            }
        }
        break;
    case 0x44:
        t->intf = val & 0xF;
        {
            uint32_t ints = (t->intr | t->intf) & t->inte;
            for (int i = 0; i < 4; i++) {
                if (ints & (1u << i)) {
                    nvic_signal_irq(4 + i);
                }
            }
        }
        break;
    default: break;
    }
}

void rp2350_timer1_tick(rp2350_periph_state_t *state, uint32_t us) {
    rp2350_timer1_state_t *t = &state->timer1;
    if (t->paused || us == 0) return;
    t->time_us += us;
    
    /* Check alarms */
    uint32_t time_lo = (uint32_t)t->time_us;
    for (int i = 0; i < 4; i++) {
        if ((t->armed & (1u << i)) && (int32_t)(time_lo - t->alarm[i]) >= 0) {
            timer1_fire_alarm(t, i);
        }
    }
}

/* ========================================================================
 * RP2350 TIMER0/TIMER1 membus routing (full SET/CLR/XOR alias regions)
 * ======================================================================== */

int timer_rp2350_bus_match(uint32_t addr) {
    /* TIMER0..TIMER1 span on RP2350 includes all SET/CLR/XOR alias combinations */
    if (addr >= RP2350_TIMER0_BASE && addr < RP2350_TIMER1_BASE) return 1;
    if (addr >= RP2350_TIMER1_BASE && addr < RP2350_TIMER1_BASE + 0x8000) return 1;
    return 0;
}

uint32_t timer_rp2350_bus_read32(rp2350_periph_state_t *state, uint32_t addr) {
    uint32_t base = addr & ~0x3000u;

    if (base >= RP2350_TIMER1_BASE && base < RP2350_TIMER1_BASE + 0x100) {
        uint32_t off = base - RP2350_TIMER1_BASE;
        uint32_t val = timer1_read(&state->timer1, off);
        
        return val;
    }

    if (base >= RP2350_TIMER0_BASE && base < RP2350_TIMER0_BASE + 0x100) {
        uint32_t off = base - RP2350_TIMER0_BASE;
        uint32_t mapped;
        uint32_t val = 0;
        switch (timer_rp2350_translate_offset(off, &mapped)) {
        case TIMER2350_MAP_DIRECT:
            val = timer_read32(TIMER_BASE + mapped);
            break;
        case TIMER2350_MAP_LOCKED:
        case TIMER2350_MAP_SOURCE:
            val = 0;
            break;
        default:
            val = 0;
            break;
        }
        
        return val;
    }

    return 0;
}

void timer_rp2350_bus_write32(rp2350_periph_state_t *state, uint32_t addr, uint32_t val) {
    uint32_t alias = addr & 0x3000u;
    uint32_t base = addr & ~0x3000u;

    if (base >= RP2350_TIMER1_BASE && base < RP2350_TIMER1_BASE + 0x100) {
        timer_bus_trace_write(addr, val, 1);
        
        timer1_write_alias(&state->timer1, base - RP2350_TIMER1_BASE, alias, val);
        return;
    }

    if (base >= RP2350_TIMER0_BASE && base < RP2350_TIMER0_BASE + 0x100) {
        uint32_t off = base - RP2350_TIMER0_BASE;
        uint32_t mapped;
        timer_bus_trace_write(addr, val, 0);
        
        switch (timer_rp2350_translate_offset(off, &mapped)) {
        case TIMER2350_MAP_DIRECT:
            timer_rp2040_write_alias(TIMER_BASE + mapped, alias, val);
            break;
        case TIMER2350_MAP_LOCKED:
        case TIMER2350_MAP_SOURCE:
        default:
            break;
        }
    }
}

/* ========================================================================
 * Unified Read/Write Dispatch
 * ======================================================================== */

uint32_t rp2350_periph_read32(rp2350_periph_state_t *state, uint32_t addr) {
    uint32_t base = addr & ~0x3000u;

    /* TICKS */
    if (base >= RP2350_TICKS_BASE && base < RP2350_TICKS_BASE + 0x100)
        return ticks_read(&state->ticks, base - RP2350_TICKS_BASE);

    /* POWMAN */
    if (base >= RP2350_POWMAN_BASE && base < RP2350_POWMAN_BASE + 0x100)
        return powman_read(&state->powman, base - RP2350_POWMAN_BASE);

    /* QMI */
    if (base >= RP2350_QMI_BASE && base < RP2350_QMI_BASE + 0x100)
        return qmi_read(&state->qmi, base - RP2350_QMI_BASE);

    /* OTP controller + data */
    if (base >= RP2350_OTP_BASE && base < RP2350_OTP_BASE + 0x100)
        return otp_read(&state->otp, base);
    if (addr >= RP2350_OTP_DATA_BASE && addr < RP2350_OTP_DATA_BASE + OTP_NUM_ROWS * 4)
        return otp_read(&state->otp, addr);

    /* BOOTRAM scratch plus adjacent bootrom registers */
    if (addr >= RP2350_BOOTRAM_BASE && addr < RP2350_BOOTRAM_BASE + BOOTRAM_REGS_END)
        return bootram_read(state, addr);

    /* GLITCH */
    if (base >= RP2350_GLITCH_BASE && base < RP2350_GLITCH_BASE + 0x20) {
        uint32_t idx = (base - RP2350_GLITCH_BASE) / 4;
        return (idx < 8) ? state->glitch_regs[idx] : 0;
    }

    /* CORESIGHT */
    if (base >= RP2350_CORESIGHT_BASE && base < RP2350_CORESIGHT_BASE + 0x40) {
        uint32_t idx = (base - RP2350_CORESIGHT_BASE) / 4;
        return (idx < 16) ? state->coresight_regs[idx] : 0;
    }

    /* ACCESSCTRL */
    if (base >= 0x40160000 && base < 0x40160000 + 0x100) {
        uint32_t idx = (base - 0x40160000) / 4;
        return (idx < 64) ? state->accessctrl_regs[idx] : 0;
    }

    /* TIMER1 (direct test/API access; membus uses timer_rp2350_bus_*) */
    if (base >= RP2350_TIMER1_BASE && base < RP2350_TIMER1_BASE + 0x100) {
        return timer1_read(&state->timer1, base - RP2350_TIMER1_BASE);
    }

    return 0;
}

void rp2350_periph_write32(rp2350_periph_state_t *state, uint32_t addr, uint32_t val) {
    uint32_t base = addr & ~0x3000u;

    /* TICKS */
    if (base >= RP2350_TICKS_BASE && base < RP2350_TICKS_BASE + 0x100) {
        ticks_write(&state->ticks, base - RP2350_TICKS_BASE, val);
        return;
    }

    /* POWMAN */
    if (base >= RP2350_POWMAN_BASE && base < RP2350_POWMAN_BASE + 0x100) {
        powman_write(&state->powman, base - RP2350_POWMAN_BASE, val);
        return;
    }

    /* QMI */
    if (base >= RP2350_QMI_BASE && base < RP2350_QMI_BASE + 0x100) {
        qmi_write(&state->qmi, base - RP2350_QMI_BASE, val);
        return;
    }

    /* OTP */
    if (base >= RP2350_OTP_BASE && base < RP2350_OTP_BASE + 0x100) {
        otp_write(&state->otp, base, val);
        return;
    }

    /* BOOTRAM scratch plus adjacent bootrom registers */
    if (addr >= RP2350_BOOTRAM_BASE && addr < RP2350_BOOTRAM_BASE + BOOTRAM_REGS_END) {
        bootram_write(state, addr, val);
        return;
    }

    /* GLITCH */
    if (base >= RP2350_GLITCH_BASE && base < RP2350_GLITCH_BASE + 0x20) {
        uint32_t idx = (base - RP2350_GLITCH_BASE) / 4;
        if (idx < 8) state->glitch_regs[idx] = val;
        return;
    }

    /* CORESIGHT */
    if (base >= RP2350_CORESIGHT_BASE && base < RP2350_CORESIGHT_BASE + 0x40) {
        uint32_t idx = (base - RP2350_CORESIGHT_BASE) / 4;
        if (idx < 16) state->coresight_regs[idx] = val;
        return;
    }

    /* ACCESSCTRL */
    if (base >= 0x40160000 && base < 0x40160000 + 0x100) {
        uint32_t idx = (base - 0x40160000) / 4;
        if (idx < 64) state->accessctrl_regs[idx] = val;
        return;
    }

    /* TIMER1 (direct test/API access; membus uses timer_rp2350_bus_*) */
    if (base >= RP2350_TIMER1_BASE && base < RP2350_TIMER1_BASE + 0x100) {
        timer1_write(&state->timer1, base - RP2350_TIMER1_BASE, val);
        return;
    }
}

uint8_t rp2350_periph_read8(rp2350_periph_state_t *state, uint32_t addr) {
    /* BOOTRAM byte access */
    if (addr >= RP2350_BOOTRAM_BASE && addr < RP2350_BOOTRAM_BASE + BOOTRAM_SIZE)
        return state->bootram[addr - RP2350_BOOTRAM_BASE];
    /* Fall back to 32-bit read */
    uint32_t aligned = addr & ~3u;
    uint32_t val = rp2350_periph_read32(state, aligned);
    return (uint8_t)(val >> ((addr & 3) * 8));
}

void rp2350_periph_write8(rp2350_periph_state_t *state, uint32_t addr, uint8_t val) {
    /* BOOTRAM byte access */
    if (addr >= RP2350_BOOTRAM_BASE && addr < RP2350_BOOTRAM_BASE + BOOTRAM_SIZE) {
        state->bootram[addr - RP2350_BOOTRAM_BASE] = val;
        return;
    }
    /* Other peripherals: read-modify-write */
    uint32_t aligned = addr & ~3u;
    uint32_t word = rp2350_periph_read32(state, aligned);
    uint32_t shift = (addr & 3) * 8;
    word = (word & ~(0xFFu << shift)) | ((uint32_t)val << shift);
    rp2350_periph_write32(state, aligned, word);
}

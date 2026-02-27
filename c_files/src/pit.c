/* =========================================================================
 * pit.c – 8253/8254 Programmable Interval Timer driver
 *
 * Configures the PIT channel 0 to generate IRQ0 at a fixed rate.
 * The IRQ0 handler is registered by the scheduler (sched.c) which calls
 * pit_tick() to keep the tick counter up-to-date.
 *
 * Reference: OSDev wiki "Programmable Interval Timer".
 * ========================================================================= */

#include "pit.h"
#include "stdio.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * Module-private tick counter.
 * Declared volatile because the ISR (a different execution context) updates it.
 * ------------------------------------------------------------------------- */
static volatile unsigned int tick_count = 0;

/* -------------------------------------------------------------------------
 * pit_init
 * ------------------------------------------------------------------------- */
void pit_init(unsigned int interval_ms)
{
    unsigned int divider;
    unsigned int hz;

    /* Convert milliseconds to Hz: hz = 1000 / interval_ms */
    if (interval_ms == 0) {
        interval_ms = PIT_TICK_MS;   /* default: 10 ms */
    }
    hz = 1000u / interval_ms;
    if (hz == 0) hz = 1;

    /* Compute divider = PIT_BASE_HZ / hz.  Clamp to [1, 65535]. */
    divider = PIT_BASE_HZ / hz;
    if (divider < 1)     divider = 1;
    if (divider > 65535) divider = 65535;

    log_info("[pit] init: interval=%d ms  hz=%d  divider=%d",
             (int)interval_ms, (int)hz, (int)divider);

    /*
     * Send the command byte:
     *   Channel 0 | lo/hi byte access | mode 3 (square wave) | binary mode
     *   = 0x36
     */
    outb(PIT_COMMAND, PIT_CONFIG);

    /* Send the divider: low byte first, then high byte. */
    outb(PIT_CHANNEL0_DATA, (unsigned char)(divider & 0xFF));
    outb(PIT_CHANNEL0_DATA, (unsigned char)((divider >> 8) & 0xFF));

    tick_count = 0;

    log_info("[pit] configured  IRQ0 @ ~%d ms intervals", (int)interval_ms);
}

/* -------------------------------------------------------------------------
 * pit_tick – called from the IRQ0/timer ISR to advance the counter.
 * ------------------------------------------------------------------------- */
void pit_tick(void)
{
    tick_count++;
}

/* -------------------------------------------------------------------------
 * pit_get_ticks – return elapsed ticks since pit_init().
 * ------------------------------------------------------------------------- */
unsigned int pit_get_ticks(void)
{
    return tick_count;
}

#ifndef PIT_H
#define PIT_H

/* =========================================================================
 * pit.h – 8253/8254 Programmable Interval Timer (PIT) driver
 *
 * The PIT is the classic x86 hardware timer sitting at I/O ports 0x40-0x43.
 * It runs off a fixed 1193182 Hz reference clock.  We use channel 0 which
 * fires IRQ0 (remapped to vector 32 by our PIC) at a configurable rate.
 *
 * Configuration byte (sent to command port 0x43):
 *   Bits 7-6  : Channel select   = 00 (channel 0)
 *   Bits 5-4  : Access mode      = 11 (low byte then high byte)
 *   Bits 3-1  : Operating mode   = 011 (square-wave generator, mode 3)
 *   Bit  0    : BCD/binary       = 0  (16-bit binary)
 *   => 0x36
 *
 * The divider is a 16-bit value: divider = PIT_BASE_HZ / desired_hz
 *   Minimum divider: 1    → 1193182 Hz
 *   Maximum divider: 65535 → ~18.2 Hz
 *
 * Reference: OSDev wiki "Programmable Interval Timer", Intel 82C54 datasheet.
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * I/O port addresses
 * ------------------------------------------------------------------------- */
#define PIT_CHANNEL0_DATA  0x40   /* Channel 0 data port (R/W)       */
#define PIT_CHANNEL1_DATA  0x41   /* Channel 1 data port (R/W)       */
#define PIT_CHANNEL2_DATA  0x42   /* Channel 2 data port (R/W)       */
#define PIT_COMMAND        0x43   /* Command/mode register (W only)  */

/* -------------------------------------------------------------------------
 * PIT base clock frequency (Hz)
 * The PIT oscillator runs at 1.193182 MHz ≈ 1193182 Hz.
 * ------------------------------------------------------------------------- */
#define PIT_BASE_HZ  1193182u

/* -------------------------------------------------------------------------
 * Configuration byte: channel 0, lo/hi byte access, mode 3 (square wave)
 * ------------------------------------------------------------------------- */
#define PIT_CMD_CHANNEL0   0x00  /* Select channel 0           */
#define PIT_CMD_ACCESS_LH  0x30  /* Access: send low byte first, then high */
#define PIT_CMD_MODE3      0x06  /* Mode 3: square-wave generator */
#define PIT_CMD_BINARY     0x00  /* 16-bit binary (not BCD)    */

#define PIT_CONFIG  (PIT_CMD_CHANNEL0 | PIT_CMD_ACCESS_LH | PIT_CMD_MODE3 | PIT_CMD_BINARY)

/* -------------------------------------------------------------------------
 * Tick interval used by the CFS scheduler (milliseconds per tick).
 * Keep this in sync with the value passed to pit_init().
 * ------------------------------------------------------------------------- */
#define PIT_TICK_MS  10u

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * pit_init – configure the PIT to fire IRQ0 every @interval_ms milliseconds.
 *
 * @param interval_ms  Desired tick period in milliseconds (1 – 55 ms).
 *                     Values outside [1, 55] are clamped to the hardware
 *                     divider range [1, 65535].
 *
 * After this call, IRQ0 (vector 32) fires at the configured rate.
 * The caller is responsible for registering a handler via
 * register_interrupt_handler(32, handler).
 */
void pit_init(unsigned int interval_ms);

/**
 * pit_get_ticks – return the total number of PIT ticks since pit_init().
 *
 * Incremented by the timer ISR.  Useful for timeouts and sleep().
 */
unsigned int pit_get_ticks(void);

/**
 * pit_tick – called by the timer ISR to increment the tick counter.
 * Not meant to be called from user code.
 */
void pit_tick(void);

#endif /* PIT_H */

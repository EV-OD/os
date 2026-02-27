# Programmable Interval Timer (PIT)

The 8253/8254 Programmable Interval Timer is the classic x86 hardware timer. It generates IRQ0 at a configurable rate, which drives the kernel's preemptive scheduler.

---

## 1. Hardware Overview

The PIT has three independent channels:

| Channel | I/O port | Use |
|---------|----------|-----|
| 0 | `0x40` | System timer → IRQ0 (**used by the scheduler**) |
| 1 | `0x41` | Historically used for DRAM refresh (obsolete) |
| 2 | `0x42` | PC speaker |

The command/mode register is write-only at port `0x43`.

The PIT runs off a fixed **1,193,182 Hz** reference clock (derived from the original IBM PC's 14.31818 MHz crystal divided by 12). All frequency calculations are based on this constant.

---

## 2. Configuration

### Command byte (sent to port 0x43)

We use:

```
0x36 = 0011 0110
  Bits 7-6  = 00   Channel 0
  Bits 5-4  = 11   Access: low byte then high byte
  Bits 3-1  = 011  Mode 3: square-wave generator
  Bit  0    = 0    Binary (not BCD)
```

The **square-wave mode (mode 3)** produces a symmetrical output: the count counts from the divider value down to zero, toggles the output, repeats. This gives the most consistent tick intervals.

### Divider calculation

The caller passes a desired interval in milliseconds. The driver converts it to a 16-bit divider:

```
hz      = 1000 / interval_ms
divider = PIT_BASE_HZ / hz
        = 1193182 / hz
```

For the default `PIT_TICK_MS = 10` ms:

```
hz      = 1000 / 10 = 100 Hz
divider = 1193182 / 100 = 11931  (≈ 10.00 ms actual interval)
```

The divider is sent as **two bytes** to port `0x40`: low byte first, then high byte. This two-step write is why the command byte must set bits 5-4 = 11 (lo/hi mode).

### Divider range

| Divider | Frequency | Interval |
|---------|-----------|---------|
| 1 | 1,193,182 Hz | ~0.84 µs |
| 11931 | ~100 Hz | ~10 ms ← **our setting** |
| 65535 | ~18.2 Hz | ~54.9 ms |

---

## 3. Initialization Sequence

```c
pit_init(PIT_TICK_MS);   /* called from kernel_init() */
pic_clear_mask(0);        /* unmask IRQ0 so ticks arrive */
```

Inside `pit_init(interval_ms)`:

```
1. Compute:  hz = 1000 / interval_ms
             divider = 1193182 / hz   (clamped to [1, 65535])
2. outb(PIT_COMMAND, 0x36)            ; configure channel 0 mode 3
3. outb(PIT_CHANNEL0_DATA, lo(div))   ; send low byte of divider
4. outb(PIT_CHANNEL0_DATA, hi(div))   ; send high byte of divider
5. tick_count = 0
```

---

## 4. Runtime — Tick Counter

`pit_tick()` is called on every timer interrupt (by `sched_tick()`). It increments the module-private `tick_count`:

```c
static volatile unsigned int tick_count = 0;

void pit_tick(void)   { tick_count++; }
unsigned int pit_get_ticks(void) { return tick_count; }
```

`tick_count` is `volatile` because it is written by the ISR (an asynchronous execution context) and read from normal kernel code.

---

## 5. Interrupt Flow

```
PIT fires IRQ0 every 10 ms
    │
    └─ CPU: vector 32 → common_isr_stub  (asm/isr.s)
           │
           └─ interrupt_handler(cpu, stack, 32)  (isr.c)
                  │
                  ├─ calls registered handlers (e.g. timer_stub stub)
                  ├─ pic_acknowledge(32)          ← EOI sent here
                  └─ sched_tick(cpu, 32)          ← CFS tick + possible ctx switch
                         ├─ pit_tick()            ← increment tick_count
                         ├─ update vruntime of current process
                         ├─ pick next process
                         └─ return new_esp (0 if no switch)
```

The EOI is sent **before** `sched_tick()` returns the new ESP so the PIC is ready for the next interrupt regardless of which process resumes.

---

## 6. I/O Port Summary

| Port | R/W | Name |
|------|-----|------|
| `0x40` | R/W | Channel 0 data |
| `0x41` | R/W | Channel 1 data (unused) |
| `0x42` | R/W | Channel 2 data (unused) |
| `0x43` | W   | Mode/command register |

---

## 7. Source Files

| File | Role |
|------|------|
| [c_files/includes/pit.h](../../c_files/includes/pit.h) | Port constants, `PIT_TICK_MS`, API declarations |
| [c_files/src/pit.c](../../c_files/src/pit.c) | `pit_init()`, `pit_tick()`, `pit_get_ticks()` |

---

## 8. References

- OSDev wiki — "Programmable Interval Timer" <https://wiki.osdev.org/Programmable_Interval_Timer>
- Intel 82C54 Programmable Interval Timer datasheet
- IBM PC Technical Reference Manual (original divider derivation)

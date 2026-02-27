# Process Subsystem

A **process** in this kernel is a kernel-mode task with its own execution context (saved register state) and its own kernel stack. It is the unit of scheduling for the CFS scheduler.

---

## 1. Process Descriptor (`process_t`)

Each process is described by a `process_t` struct, allocated on the kernel heap via `kmalloc()`:

```c
typedef struct process {
    /* Identity */
    unsigned int    pid;        /* Unique process ID (1-based, auto-incremented) */
    const char     *name;       /* Human-readable label (for log output only)    */

    /* CFS scheduling fields */
    int             nice;       /* Nice value: -20 (highest) … +19 (lowest prio) */
    unsigned int    weight;     /* CFS weight derived from nice (see table below) */
    unsigned int    vruntime;   /* Virtual runtime in CFS ms-units               */

    /* State */
    proc_state_t    state;      /* RUNNABLE / RUNNING / SLEEPING / DEAD          */

    /* Kernel stack and saved context */
    unsigned char  *kstack;     /* Base (lowest address) of the kernel stack      */
    unsigned int    saved_esp;  /* Kernel ESP at last context-save                */

    /* Run queue linkage */
    struct process *next;       /* Next in the CFS run queue linked list          */
} process_t;
```

`sizeof(process_t)` ≈ 36 bytes. Each process also owns a **4 KB kernel stack** (`PROC_KSTACK_SIZE`), separately allocated from the heap.

---

## 2. Process States

```
  ┌─────────────┐   sched picks it   ┌─────────────┐
  │  RUNNABLE   │ ─────────────────► │   RUNNING   │
  └─────────────┘                    └─────────────┘
        ▲                                   │
        │  timer tick (re-insert)            │  (future: block / yield)
        │                                   ▼
        │                            ┌─────────────┐
        └────────────────────────────│  SLEEPING   │
                                     └─────────────┘
                                           │  (future: exit)
                                           ▼
                                     ┌─────────────┐
                                     │    DEAD     │
                                     └─────────────┘
```

Currently only `RUNNABLE` and `RUNNING` are actively used. `SLEEPING` and `DEAD` are reserved for future yield and exit support.

---

## 3. Nice Value → CFS Weight

The nice-to-weight mapping follows the Linux kernel's `prio_to_weight[]` table (simplified). All 40 values (nice −20 … +19) are stored in the static array `nice_to_weight[]` in [c_files/includes/process.h](../../c_files/includes/process.h):

| Nice | Weight | Approximate CPU share |
|------|--------|-----------------------|
| −20 | 88761 | ~87× baseline |
| −10 | 9548 | ~9.3× baseline |
| −5 | 3121 | ~3× baseline |
| 0 | 1024 | **baseline** |
| +5 | 335 | ~0.33× baseline |
| +10 | 110 | ~0.11× baseline |
| +19 | 15 | ~1/68 baseline |

The ratio between adjacent nice levels is approximately **1.25×**. The full range (nice −20 vs +19) is about **87:1**.

---

## 4. Creating a Process — `process_create()`

`process_create(name, entry, nice)` does the following:

1. `kmalloc(sizeof(process_t))` — allocate the descriptor.
2. `kmalloc(PROC_KSTACK_SIZE)` — allocate a 4 KB kernel stack.
3. `memset(kstack, 0, PROC_KSTACK_SIZE)` — zero the stack (clean register state).
4. Fill in the descriptor fields.
5. **Build an initial interrupt-return frame** on the top of the kernel stack.
6. Set `saved_esp` to point at the top of the pusha register block.

### Initial Kernel Stack Frame

`process_create()` builds a fake frame that looks exactly like what `common_isr_stub` in [asm/isr.s](../../asm/isr.s) would have produced if the process had been interrupted by IRQ0 during its very first instruction.

```
kstack + PROC_KSTACK_SIZE  ← top of stack (highest address)
    [- 4 ]  EFLAGS  = 0x0202   (IF=1, reserved bit 1 = 1)
    [- 8 ]  CS      = 0x0008   (kernel code segment selector)
    [- 12]  EIP     = entry    (address of the task function)
    [- 16]  error_code = 0     (dummy; mirrors IRQ macro format in isr.s)
    [- 20]  int_number = 32    (dummy; mirrors IRQ0 vector)
    [- 24]  EAX = 0  ┐
    [- 28]  ECX = 0  │
    [- 32]  EDX = 0  │  pusha block
    [- 36]  EBX = 0  │  (8 × 4 = 32 bytes)
    [- 40]  ESP = 0  │  all zeroed by memset above
    [- 44]  EBP = 0  │
    [- 48]  ESI = 0  │
    [- 52]  EDI = 0  ┘
    ↑
    saved_esp  (= kstack + PROC_KSTACK_SIZE - 52)
```

When the scheduler restores this process (via `sched_start()` or a context switch):

```nasm
mov esp, proc->saved_esp   ; switch to process's kernel stack
popa                        ; restore 8 GP registers (all zero for new proc)
add esp, 8                  ; skip dummy error_code + int_number
iret                        ; pop EIP=entry, CS=0x08, EFLAGS=0x0202
```

`iret` begins executing the process at `entry` with interrupts enabled (IF=1 from EFLAGS).

---

## 5. Memory Layout per Process

```
Process heap allocations (kernel heap at 0xC0300000+):

  ┌──────────────────────┐  ← kmalloc result for process_t
  │  process_t  (36 B)   │
  └──────────────────────┘

  ┌──────────────────────┐  ← kstack (lowest address)
  │                      │
  │  kernel stack 4 KB   │  grows downward
  │                      │
  │  [initial frame]     │  ← built by process_create()
  └──────────────────────┘  ← kstack + PROC_KSTACK_SIZE (top)
```

---

## 6. Summary of Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `PROC_MAX` | 16 | Maximum simultaneous processes |
| `PROC_KSTACK_SIZE` | 4096 | Kernel stack size per process (bytes) |
| `NICE0_WEIGHT` | 1024 | CFS weight for nice 0 (baseline) |
| `NICE_MIN` | −20 | Most negative (highest priority) nice value |
| `NICE_MAX` | +19 | Most positive (lowest priority) nice value |

---

## 7. Source Files

| File | Role |
|------|------|
| [c_files/includes/process.h](../../c_files/includes/process.h) | `process_t`, state enum, `nice_to_weight[]`, API |
| [c_files/src/process.c](../../c_files/src/process.c) | `process_init()`, `process_create()` |

---

## 8. References

- Linux kernel `include/linux/sched.h` — `task_struct` (conceptual reference)
- Linux kernel `kernel/sched/fair.c` — `prio_to_weight[]` table
- *Operating Systems: Three Easy Pieces* — Chapter 7 "Scheduling: Introduction"

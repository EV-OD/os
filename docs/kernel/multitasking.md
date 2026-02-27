# Multitasking

This document gives a top-level view of how all the multitasking components fit together: the PIT, TSS, process subsystem, CFS scheduler, and the context-switch mechanism in the ISR stub.

---

## 1. Components and Their Roles

```
┌────────────────────────────────────────────────────────────────┐
│                         Multitasking                           │
│                                                                │
│  ┌──────────────┐  IRQ0 every 10 ms   ┌──────────────────┐   │
│  │   PIT driver  │ ──────────────────► │  interrupt_handler│  │
│  │  (pit.c)      │                     │  (isr.c)          │  │
│  └──────────────┘                     └────────┬─────────┘   │
│                                                │              │
│                                         sched_tick()          │
│                                                │              │
│  ┌──────────────┐   update TSS.esp0   ┌────────▼─────────┐   │
│  │  TSS         │ ◄────────────────── │  CFS Scheduler   │   │
│  │  (tss.c)     │                     │  (sched.c)        │   │
│  └──────────────┘                     └────────┬─────────┘   │
│                                                │              │
│                                      return new_esp / 0       │
│                                                │              │
│  ┌──────────────┐  popa + iret        ┌────────▼─────────┐   │
│  │  Processes   │ ◄────────────────── │  common_isr_stub  │  │
│  │  (process.c) │                     │  (isr.s)          │   │
│  └──────────────┘                     └──────────────────┘   │
└────────────────────────────────────────────────────────────────┘
```

---

## 2. Full Context Switch Walkthrough

This is the complete sequence that happens every 10 ms:

### Step 1 — PIT fires IRQ0

The PIT channel 0 fires an interrupt at vector 32. The CPU:
1. Finishes the current instruction.
2. Pushes `EFLAGS`, `CS`, `EIP` onto the current process's kernel stack (ring 0 → ring 0: no stack change because we are already in kernel mode during most of the execution).
3. Looks up IDT entry 32 and jumps to `irq0` stub in [asm/isr.s](../../asm/isr.s).

### Step 2 — `common_isr_stub` saves context

```nasm
irq0:
    push dword 0    ; dummy error code
    push dword 32   ; interrupt number

common_isr_stub:
    pusha           ; save EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
    ; at this point ESP points to the cpu_state (= saved_esp of current process)

    push [eax+32]   ; arg3: interrupt number
    lea  ecx, [eax+36]
    push ecx        ; arg2: stack_state* (EIP, CS, EFLAGS on stack)
    push eax        ; arg1: cpu_state*
    call interrupt_handler
    add  esp, 12
```

### Step 3 — `interrupt_handler` dispatches and ticks

```c
unsigned int interrupt_handler(cpu_state *cpu, stack_state *stack, unsigned int interrupt)
{
    /* registered handler (e.g. timer_stub) */
    handlers[32](cpu, stack, 32);

    /* send EOI now, before potential context switch */
    pic_acknowledge(32);

    /* CFS tick: update vruntime, pick next, return new ESP */
    return sched_tick(cpu, 32);
}
```

### Step 4 — `sched_tick()` decides whether to switch

```c
unsigned int sched_tick(cpu_state *cpu, unsigned int interrupt)
{
    pit_tick();                             // increment tick counter
    current_proc->saved_esp = (unsigned int)cpu;  // save current ESP

    current_proc->vruntime += delta;        // delta = TICK_MS * W0 / weight
    enqueue_sorted(current_proc);           // re-insert into run queue
    next = dequeue_min();                   // pick smallest vruntime

    if (next == current_proc)               // same process wins again?
        return 0;                           // → no switch

    tss_set_kernel_stack(                   // update TSS for ring-3→ring-0 later
        next->kstack + PROC_KSTACK_SIZE);
    current_proc = next;
    return next->saved_esp;                 // → switch to this ESP
}
```

### Step 5 — `common_isr_stub` performs the switch

```nasm
    ; eax = return value from interrupt_handler
    test eax, eax
    jz   .no_ctx_switch
    mov  esp, eax          ; ← switch kernel stack to next process
.no_ctx_switch:
    popa                   ; restore GP registers (from new process's stack)
    add  esp, 8            ; skip dummy error_code + int_number
    iret                   ; pop EIP, CS, EFLAGS → enter new process
```

If `eax != 0`, `esp` is loaded with the **new process's** `saved_esp` before `popa`. The `popa` then restores the new process's registers, and `iret` jumps to its saved `EIP`.

### Kernel stack state during a switch

```
Old process's kernel stack (before tick):        New process's kernel stack:
─────────────────────────────────────────        ────────────────────────────
  [EFLAGS]              (pushed by CPU)            [EFLAGS]
  [CS]                  (pushed by CPU)            [CS]
  [EIP]                 (pushed by CPU)            [EIP]       ← next task's EIP
  [0]                   (dummy error code)         [0]
  [32]                  (interrupt number)         [32]
  [EAX..EDI (pusha)]    ← current saved_esp        [EAX..EDI]  ← next saved_esp
        ↑ esp before switch                               ↑ esp after `mov esp, eax`
```

---

## 3. Test Tasks (`tasks.c`)

Four kernel-mode tasks demonstrate proportional CPU allocation:

| Task | nice | weight | Expected loop-count ratio |
|------|------|--------|--------------------------|
| `task_a` | −5 | 3121 | ~3.0× task_b |
| `task_b` | 0 | 1024 | 1.0× (baseline) |
| `task_c` | +5 | 335 | ~0.33× task_b |
| `task_idle` | +19 | 15 | ~0.015× task_b |

Each task writes its loop counter to a dedicated VGA row (rows 9–12) in real time. The difference in counter speeds shows the CFS weight in action. The header row (row 8) is written once by `task_a`.

```
Row 8:  TASK NAME    NICE    LOOP COUNT (CFS demo)
Row 9:  task_a       -5      [fast growing counter]  ← bright green
Row 10: task_b        0      [medium counter]         ← bright cyan
Row 11: task_c       +5      [slow counter]           ← bright magenta
Row 12: task_idle   +19      [very slow counter]      ← dark grey
```

---

## 4. Initialization in `kmain()`

```c
// After all tests pass:
process_init();         // reset PID counter
sched_init();           // zero run queue

// Create 4 test tasks
process_t *pa = process_create("task_a",    task_a,    -5);
process_t *pb = process_create("task_b",    task_b,     0);
process_t *pc = process_create("task_c",    task_c,     5);
process_t *pi = process_create("task_idle", task_idle, 19);

sched_add(pa); sched_add(pb);
sched_add(pc); sched_add(pi);

interrupts_disable();   // disable before switch; iret re-enables via EFLAGS.IF
sched_start();          // NEVER RETURNS
```

---

## 5. GDT Segments Added for User Mode (Future)

As part of this implementation the GDT was extended from 3 to 6 entries to lay the groundwork for ring-3 processes, even though the current tasks still run at ring 0:

| Index | Selector | Name | DPL |
|-------|----------|------|-----|
| 0 | 0x00 | Null | — |
| 1 | 0x08 | Kernel code | 0 |
| 2 | 0x10 | Kernel data | 0 |
| 3 | 0x18 | **User code** | **3** |
| 4 | 0x20 | **User data** | **3** |
| 5 | 0x28 | **TSS** | 0 |

When entering user mode via `iret`, CS must be `0x1B` (selector `0x18 | RPL=3`) and SS must be `0x23` (selector `0x20 | RPL=3`).

---

## 6. Source Files

| File | Role |
|------|------|
| [c_files/src/kmain.c](../../c_files/src/kmain.c) | Creates processes, calls `sched_start()` |
| [c_files/src/process.c](../../c_files/src/process.c) | `process_create()`, initial stack frame |
| [c_files/src/sched.c](../../c_files/src/sched.c) | CFS run queue, `sched_tick()`, `sched_start()` |
| [c_files/src/isr.c](../../c_files/src/isr.c) | `interrupt_handler()` calls `sched_tick()`, returns new ESP |
| [asm/isr.s](../../asm/isr.s) | `common_isr_stub` — context-switch via `mov esp, eax` |
| [c_files/src/tss.c](../../c_files/src/tss.c) | `tss_set_kernel_stack()` updates TSS.esp0 per switch |
| [c_files/src/pit.c](../../c_files/src/pit.c) | 10 ms timer tick source |
| [c_files/src/gdt.c](../../c_files/src/gdt.c) | User segments + TSS descriptor |
| [c_files/src/tasks.c](../../c_files/src/tasks.c) | Demo tasks writing live counters to VGA |

---

## 7. Related Documents

- [docs/kernel/process.md](process.md) — process_t descriptor and stack frame construction
- [docs/kernel/sched.md](sched.md) — CFS algorithm, vruntime, run queue design
- [docs/architecture/tss.md](../architecture/tss.md) — TSS structure and esp0 update
- [docs/drivers/pit.md](../drivers/pit.md) — PIT timer configuration
- [docs/architecture/gdt.md](../architecture/gdt.md) — GDT including user and TSS entries
- [docs/architecture/isr.md](../architecture/isr.md) — ISR stub and context-switch mechanism

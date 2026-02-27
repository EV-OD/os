# CFS Scheduler

This kernel implements a simplified **Completely Fair Scheduler (CFS)** inspired by the Linux kernel. It is preemptive: the PIT fires IRQ0 every 10 ms, and the scheduler decides on each tick which process should run next.

---

## 1. Core Idea — Virtual Runtime

Every process accumulates a **virtual runtime** (`vruntime`) that grows on each timer tick. The scheduler always picks the process with the **smallest `vruntime`** to run next.

The key insight is that `vruntime` grows at different rates depending on a process's priority (weight):

```
vruntime_delta = PIT_TICK_MS × NICE0_WEIGHT / process.weight
```

- A **high-priority** process (low nice, high weight) has a **small** delta → its `vruntime` grows slowly → it is picked more often.
- A **low-priority** process (high nice, low weight) has a **large** delta → its `vruntime` grows quickly → it is picked less often.
- All processes eventually run, because they all advance toward the minimum and will eventually have the smallest `vruntime`.

### Example (10 ms tick, NICE0_WEIGHT = 1024)

| Process | nice | weight | delta / tick | Relative CPU |
|---------|------|--------|-------------|-------------|
| task_a | −5 | 3121 | 10 × 1024 / 3121 ≈ **3** | 3121/1024 ≈ **3.0×** |
| task_b | 0 | 1024 | 10 × 1024 / 1024 = **10** | **1.0×** (baseline) |
| task_c | +5 | 335 | 10 × 1024 / 335 ≈ **31** | 335/1024 ≈ **0.33×** |
| task_idle | +19 | 15 | 10 × 1024 / 15 ≈ **682** | 15/1024 ≈ **0.015×** |

Over time, task_a will show a loop counter approximately **3×** higher than task_b and **9×** higher than task_c — directly visible on the VGA demo screen.

---

## 2. Run Queue Design

The run queue is a **singly linked list sorted ascending by `vruntime`**:

```
run_queue ──► [proc B  vr=10] ──► [proc A  vr=13] ──► [proc D  vr=41] ──► NULL
               ↑ minimum                                last
               picked next
```

- **Pick-next** — O(1): dequeue the head of the list.
- **Insert** — O(n): walk the list to find the right position (n ≤ `PROC_MAX` = 16, acceptable).
- The **currently running** process is removed from the queue while it is `RUNNING`. It is re-inserted (with updated `vruntime`) on the next tick.

### Invariants

1. `current_proc` is **not** in `run_queue` while `state == RUNNING`.
2. Every `RUNNABLE` process has `state == RUNNABLE` and is in `run_queue`.
3. On each tick: `current_proc` → re-insert → pick new minimum → compare.

---

## 3. `sched_tick()` — The Hot Path

`sched_tick()` is called from `interrupt_handler()` on every PIT IRQ0. It runs in interrupt context (interrupts are disabled by the CPU during the handler).

```
sched_tick(cpu_state *cpu, interrupt=32):

1. pit_tick()                        increment tick counter
2. current_proc->saved_esp = cpu     save kernel stack pointer
3. delta = PIT_TICK_MS * NICE0_WEIGHT / current_proc->weight
   current_proc->vruntime += delta   update virtual runtime
4. enqueue_sorted(current_proc)      re-insert into run queue
5. next = dequeue_min()              pick minimum vruntime
6. if (next == current_proc)         no change?
       return 0                      ← no context switch
7. tss_set_kernel_stack(             update TSS.esp0 for ring-0 entry
       next->kstack + PROC_KSTACK_SIZE)
8. current_proc = next               update global
9. return next->saved_esp            ← context switch signal
```

The return value `next->saved_esp` is the crucial signal to [asm/isr.s](../../asm/isr.s):

```nasm
call interrupt_handler
add  esp, 12
test eax, eax          ; eax = return value
jz   .no_ctx_switch
mov  esp, eax          ; ← switch kernel stack here
.no_ctx_switch:
popa
add  esp, 8
iret
```

If `eax != 0`, the CPU switches to `next->kstack` before executing `popa + iret`, which restores the **new process's** saved registers and jumps to its `EIP`.

---

## 4. `sched_start()` — Entering the First Process

`sched_start()` launches the scheduler. Called once from `kmain()` with interrupts disabled. It never returns.

```
1. Dequeue the minimum-vruntime process from run_queue.
2. Set state = RUNNING, current_proc = first.
3. tss_set_kernel_stack(first->kstack + PROC_KSTACK_SIZE).
4. Inline assembly:
       mov esp, first->saved_esp    ; switch to process's kernel stack
       popa                          ; restore GP registers (all zero for new proc)
       add esp, 8                    ; skip dummy error_code + int_number
       iret                          ; pop EIP, CS, EFLAGS=0x202 → enter process
```

EFLAGS has `IF=1`, so `iret` re-enables interrupts, and the PIT starts firing.

---

## 5. New Process Entry Rule

When `sched_add(proc)` enqueues a new process, it sets:

```c
proc->vruntime = min_vruntime();
```

`min_vruntime()` returns the smallest `vruntime` among all processes currently in the run queue and the running process. This ensures the new process starts at a fair point:

- It is **not** set to 0 (which would give it an unfair head-start over long-running processes).
- It is **not** set to the maximum (which would starve it for a long time).
- It starts at the "frontier" of the current scheduling epoch.

---

## 6. EOI Ordering

The End-of-Interrupt (EOI) for IRQ0 is sent to the PIC **before** `sched_tick()` returns the new ESP:

```c
/* in interrupt_handler() */
pic_acknowledge(interrupt);   /* EOI first */
new_esp = sched_tick(cpu, interrupt);
return new_esp;
```

This is intentional. If EOI were sent after the context switch, the PIC would remain masked until the old process ran again — which might be a long time if a high-priority process preempts it. Sending EOI first means the timer IRQ can fire again in the new process's context.

---

## 7. Public API Summary

| Function | Called from | Purpose |
|----------|-------------|---------|
| `sched_init()` | `kmain()` | Zero run queue, set current_proc = NULL |
| `sched_add(proc)` | `kmain()` | Enqueue a new process at `min_vruntime` |
| `sched_start()` | `kmain()` | Launch scheduler, never returns |
| `sched_tick(cpu, irq)` | `interrupt_handler()` | Per-tick CFS update, returns new ESP or 0 |
| `sched_current()` | anywhere | Return running process pointer |
| `sched_dump()` | debug | Log full run queue to serial |

---

## 8. Source Files

| File | Role |
|------|------|
| [c_files/includes/sched.h](../../c_files/includes/sched.h) | Public API, design notes |
| [c_files/src/sched.c](../../c_files/src/sched.c) | CFS implementation |
| [c_files/includes/process.h](../../c_files/includes/process.h) | `process_t`, nice→weight table |
| [c_files/src/process.c](../../c_files/src/process.c) | Process creation, initial stack frame |
| [asm/isr.s](../../asm/isr.s) | `common_isr_stub` with context-switch ESP check |
| [c_files/src/isr.c](../../c_files/src/isr.c) | `interrupt_handler()` calling `sched_tick()` |

---

## 9. Limitations and Future Work

| Limitation | Notes |
|------------|-------|
| No user-mode context switch | Currently all tasks run at ring 0. User-mode requires saving/restoring `SS`, `ESP` (user stack) too. |
| No `yield()` / `sleep()` | Processes cannot voluntarily give up the CPU yet. |
| No `exit()` | Processes run forever; no `DEAD` state cleanup. |
| No `fork()` / `exec()` | Process creation requires C function pointers; no binary loading yet. |
| Min-vruntime wraps at 2³² | For very long-running systems `vruntime` will wrap. Linux uses a 64-bit counter. |
| Single CPU only | Extending to SMP requires per-CPU run queues and load balancing. |

---

## 10. References

- Linux kernel `kernel/sched/fair.c` — CFS implementation
- Linux kernel `Documentation/scheduler/sched-design-CFS.rst`
- *Operating Systems: Three Easy Pieces* — Chapter 9 "Scheduling: Proportional Share"
- Ingo Molnár's original CFS announcement (2007)

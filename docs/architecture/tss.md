# Task State Segment (TSS)

The Task State Segment is an x86 hardware structure that the CPU reads automatically during **privilege-level transitions**. When an interrupt fires while user-mode code (ring 3) is running, the CPU needs to know where to find a kernel-mode stack. It looks this up in the TSS.

---

## 1. Why the TSS Is Needed

The x86 CPU enforces separate stacks for each privilege level. When a ring-3 process is interrupted:

1. The CPU loads `SS` and `ESP` from the TSS fields `ss0` / `esp0`.
2. It switches to that kernel stack.
3. It pushes the user-mode context (`SS`, `ESP`, `EFLAGS`, `CS`, `EIP`) onto the new kernel stack.
4. It jumps to the interrupt handler.

Without a valid TSS this entire sequence would fault, making user-mode code impossible to interrupt safely.

> We use **software multi-tasking** (a single TSS, updated per switch) rather than hardware task-switching (one TSS per process). Hardware task-switching is slow and inflexible; no modern OS uses it.

---

## 2. TSS Structure

The full 32-bit TSS is 104 bytes. Only two fields matter at runtime:

```c
typedef struct tss_entry {
    unsigned int prev_tss;   /* hardware task link – unused           */
    unsigned int esp0;       /* ← updated per context switch          */
    unsigned int ss0;        /* ← set to GDT_KERNEL_DATA_SELECTOR     */
    /* … 22 more fields, all zero … */
    unsigned short trap;
    unsigned short iomap_base; /* set to sizeof(tss_entry_t) = no IOPM */
} __attribute__((packed)) tss_entry_t;
```

`iomap_base` is set to `sizeof(tss_entry_t)` to indicate that there is no I/O permission bitmap; ring-3 code therefore has no direct I/O port access.

---

## 3. GDT Descriptor for the TSS

The TSS must have an 8-byte **system descriptor** in the GDT at index 5 (selector `0x28`):

```
Access byte = 0x89  (binary: 1000 1001)
  Bit 7   P    = 1   Segment present
  Bit 6-5 DPL  = 00  Ring 0 only (only kernel can ltr / call-gate)
  Bit 4   S    = 0   System descriptor (NOT a code/data segment)
  Bits 3-0 Type = 1001  Available 32-bit TSS

Granularity byte = 0x00  (byte-level; no 4 KB page scaling)
```

Compare with a normal code/data descriptor which has `S=1`. Setting `S=0` signals to the CPU that this entry describes a system structure, not a memory segment.

The base is the linear address of the `tss_entry_t` variable; the limit is `sizeof(tss_entry_t) - 1`.

---

## 4. Initialization Flow

```
kernel_init()
    └─ gdt_init()             ← GDT built, slot 5 left zeroed
    └─ ...
    └─ tss_init()
           zero TSS struct
           tss.ss0 = GDT_KERNEL_DATA_SELECTOR (0x10)
           tss.esp0 = 0 (scheduler sets this before first user task)
           tss.iomap_base = sizeof(tss_entry_t)
           gdt_set_tss_entry(5, &tss, sizeof(tss)-1)
               └─ gdt_set_entry(5, base, limit, 0x89, 0x00)
           ltr 0x28            ← load Task Register
```

`ltr` does **not** cause a task switch. It simply initialises the hidden `TR` (Task Register) cache so the CPU knows where the TSS lives for future privilege-level transitions.

---

## 5. Runtime Usage — Per-Process `esp0` Update

Every time the scheduler switches to a new process it calls:

```c
tss_set_kernel_stack(new_proc->kstack + PROC_KSTACK_SIZE);
```

This updates `tss.esp0` to point at the **top** of the incoming process's kernel stack. If that process is later interrupted (ring 3 → ring 0) the CPU will push the user context at the right address.

```
Before switch          After switch
──────────────         ──────────────
tss.esp0 = A           tss.esp0 = B
            ↓                      ↓
   process A's             process B's
   kernel stack             kernel stack
```

---

## 6. Selector Constants

| GDT Index | Selector | Name |
|-----------|----------|------|
| 5 | `0x28` | TSS descriptor |

```c
#define TSS_GDT_INDEX  5
#define TSS_SELECTOR   0x28   /* = 5 * 8 */
```

---

## 7. Source Files

| File | Role |
|------|------|
| [c_files/includes/tss.h](../../c_files/includes/tss.h) | `tss_entry_t` struct, constants, API |
| [c_files/src/tss.c](../../c_files/src/tss.c) | `tss_init()`, `tss_set_kernel_stack()` |
| [c_files/includes/descriptor.h](../../c_files/includes/descriptor.h) | `GDT_NUM_ENTRIES=6`, `GDT_TSS_SELECTOR` |
| [c_files/src/gdt.c](../../c_files/src/gdt.c) | `gdt_set_tss_entry()` helper |

---

## 8. References

- Intel SDM Vol. 3A §7.2 "Task-State Segment (TSS)"
- Intel SDM Vol. 3A §7.2.1 "TSS Descriptor"
- OSDev wiki — "TSS" <https://wiki.osdev.org/TSS>

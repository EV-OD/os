#include "isr.h"
#include "pic.h"
#include "serial.h"
#include "stdio.h"
#include "string.h"
#include "sched.h"
#include "pit.h"
#include "process.h"
#include "log.h"

static isr_handler_t handlers[IDT_NUM_ENTRIES];

/*
 * PIT timer handler – silences the interrupt until the scheduler is started.
 * Once sched_start() is called, sched_tick() inside interrupt_handler takes
 * over the actual scheduling work and this stub is no longer needed.
 * We keep it registered so unhandled-interrupt spam is suppressed.
 */
static void timer_stub(struct cpu_state *cpu, struct stack_state *stack, unsigned int interrupt)
{
    (void)cpu;
    (void)stack;
    (void)interrupt;
    /* EOI is sent by interrupt_handler after all handlers run. */
}

/*
 * Page Fault (#PF, vector 14) handler.
 *
 * Reads the CR2 register (faulting linear address) and the CPU-pushed
 * error code to produce a diagnostic log on the serial port.
 *
 * Error-code bits (Intel SDM Vol. 3A §4.7):
 *   bit 0 (P)   : 0 = not-present page, 1 = protection violation
 *   bit 1 (W/R) : 0 = read access, 1 = write access
 *   bit 2 (U/S) : 0 = supervisor mode, 1 = user mode
 */
static void page_fault_handler(struct cpu_state *cpu, struct stack_state *stack,
                               unsigned int interrupt)
{
    (void)interrupt;

    unsigned int cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    unsigned int err = stack->error_code;
    char buf[128];

    sprintf(buf, "[#PF] CR2=0x%x EIP=0x%x err=0x%x CS=0x%x",
            cr2, stack->eip, err, stack->cs);
    serial_write(buf);
    serial_write("\r\n");

    sprintf(buf, "[#PF] %s, %s, %s mode",
            (err & 1) ? "protection" : "not-present",
            (err & 2) ? "write"      : "read",
            (err & 4) ? "user"       : "kernel");
    serial_write(buf);
    serial_write("\r\n");

    /* Dump registers for easier diagnosis */
    sprintf(buf, "[#PF] EAX=0x%x EBX=0x%x ECX=0x%x EDX=0x%x",
            cpu->eax, cpu->ebx, cpu->ecx, cpu->edx);
    serial_write(buf);
    serial_write("\r\n");
    sprintf(buf, "[#PF] ESI=0x%x EDI=0x%x EBP=0x%x uESP=0x%x",
            cpu->esi, cpu->edi, cpu->ebp, stack->user_esp);
    serial_write(buf);
    serial_write("\r\n");

    /* If the fault came from user mode, kill the current process. */
    if (err & 4) {
        process_t *cur = sched_current();
        if (cur) {
            sprintf(buf, "[#PF] killing user process '%s' pid=%d",
                    cur->name ? cur->name : "?", (int)cur->pid);
            serial_write(buf);
            serial_write("\r\n");

            cur->exit_status = -14;
            cur->state       = PROC_DEAD;
            /* Spin until the scheduler switches us out. */
            for (;;) { __asm__ volatile("sti; hlt"); }
        }
    }

    /* Kernel-mode page fault – try to recover by killing the current process.
     * This typically means a bug in a syscall handler (e.g. bad user pointer).
     * Kill the guilty process and let the PIT scheduler pick the next one. */
    {
        process_t *cur = sched_current();
        if (cur) {
            char kbuf[128];
            sprintf(kbuf, "[#PF] kernel fault while running '%s' pid=%d – killing process\r\n",
                    cur->name ? cur->name : "?", (int)cur->pid);
            serial_write(kbuf);
            log_error("[#PF] kernel fault in process '%s' pid=%d (CR2=0x%x EIP=0x%x)",
                      cur->name ? cur->name : "?", (int)cur->pid, cr2, stack->eip);
            cur->exit_status = -14;
            cur->state       = PROC_DEAD;
            /* Enable interrupts so the PIT can fire and the scheduler can
             * switch to the next runnable process. */
            for (;;) { __asm__ volatile("sti; hlt"); }
        }
    }
    /* True kernel panic – no current user process to blame.
     * Keep scheduling alive with sti so the system can continue if possible. */
    serial_write("[#PF] KERNEL PANIC – no current process (CR2 and EIP above).\r\n");
    log_error("[#PF] KERNEL PANIC CR2=0x%x EIP=0x%x", cr2, stack->eip);
    for (;;) { __asm__ volatile("sti; hlt"); }
}

/* -------------------------------------------------------------------------
 * Generic CPU exception handler (for all exceptions without a dedicated
 * handler: #DE, #UD, #GP, #SS, etc.)
 *
 * Logs the fault vector and faulting EIP, then kills the current process
 * if it was running in user mode.
 * ------------------------------------------------------------------------- */
static const char *exception_names[] = {
    [0]  = "#DE Divide Error",
    [6]  = "#UD Invalid Opcode",
    [8]  = "#DF Double Fault",
    [10] = "#TS Invalid TSS",
    [11] = "#NP Segment Not Present",
    [12] = "#SS Stack-Segment Fault",
    [13] = "#GP General Protection",
};

static void generic_exception_handler(struct cpu_state *cpu, struct stack_state *stack,
                                      unsigned int interrupt)
{
    (void)cpu;

    char buf[128];
    const char *name = (interrupt < sizeof(exception_names)/sizeof(exception_names[0])
                        && exception_names[interrupt])
                       ? exception_names[interrupt] : "Exception";

    sprintf(buf, "[FAULT] %s (vec=%d) EIP=0x%x CS=0x%x err=0x%x",
            name, (int)interrupt, stack->eip, stack->cs, stack->error_code);
    serial_write(buf);
    serial_write("\r\n");

    log_error("[FAULT] %s (vec=%d) EIP=0x%x err=0x%x",
              name, (int)interrupt, stack->eip, stack->error_code);

    /* If the fault came from user mode (CS RPL bits != 0), kill the process. */
    if (stack->cs & 3) {
        process_t *cur = sched_current();
        if (cur) {
            log_error("[FAULT] killing user process '%s' pid=%d",
                      cur->name ? cur->name : "?", (int)cur->pid);
            cur->exit_status = -(int)interrupt;
            cur->state       = PROC_DEAD;
            for (;;) { __asm__ volatile("sti; hlt"); }
        }
    }

    /* Kernel-mode exception – try to recover by killing the current process. */
    {
        process_t *cur = sched_current();
        if (cur) {
            char kbuf[128];
            sprintf(kbuf, "[FAULT] kernel %s in process '%s' pid=%d – killing\r\n",
                    name, cur->name ? cur->name : "?", (int)cur->pid);
            serial_write(kbuf);
            log_error("[FAULT] kernel %s vec=%d EIP=0x%x in '%s' pid=%d",
                      name, (int)interrupt, stack->eip,
                      cur->name ? cur->name : "?", (int)cur->pid);
            cur->exit_status = -(int)interrupt;
            cur->state       = PROC_DEAD;
            for (;;) { __asm__ volatile("sti; hlt"); }
        }
    }
    serial_write("[FAULT] KERNEL PANIC – no current process.\r\n");
    log_error("[FAULT] KERNEL PANIC %s vec=%d EIP=0x%x", name, (int)interrupt, stack->eip);
    for (;;) { __asm__ volatile("sti; hlt"); }
}

/* ISR/IRQ stubs defined in asm/isr.s */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

/* Syscall gate – int 0x80 (vector 128) */
extern void isr128(void);

static void set_isr(int n, unsigned int base)
{
    idt_set_gate(n, base, GDT_KERNEL_CODE_SELECTOR,
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_INTERRUPT_32);
}

void isr_install(void)
{
    set_isr(0,  (unsigned int)isr0);
    set_isr(1,  (unsigned int)isr1);
    set_isr(2,  (unsigned int)isr2);
    set_isr(3,  (unsigned int)isr3);
    set_isr(4,  (unsigned int)isr4);
    set_isr(5,  (unsigned int)isr5);
    set_isr(6,  (unsigned int)isr6);
    set_isr(7,  (unsigned int)isr7);
    set_isr(8,  (unsigned int)isr8);
    set_isr(9,  (unsigned int)isr9);
    set_isr(10, (unsigned int)isr10);
    set_isr(11, (unsigned int)isr11);
    set_isr(12, (unsigned int)isr12);
    set_isr(13, (unsigned int)isr13);
    set_isr(14, (unsigned int)isr14);
    set_isr(15, (unsigned int)isr15);
    set_isr(16, (unsigned int)isr16);
    set_isr(17, (unsigned int)isr17);
    set_isr(18, (unsigned int)isr18);
    set_isr(19, (unsigned int)isr19);
    set_isr(20, (unsigned int)isr20);
    set_isr(21, (unsigned int)isr21);
    set_isr(22, (unsigned int)isr22);
    set_isr(23, (unsigned int)isr23);
    set_isr(24, (unsigned int)isr24);
    set_isr(25, (unsigned int)isr25);
    set_isr(26, (unsigned int)isr26);
    set_isr(27, (unsigned int)isr27);
    set_isr(28, (unsigned int)isr28);
    set_isr(29, (unsigned int)isr29);
    set_isr(30, (unsigned int)isr30);
    set_isr(31, (unsigned int)isr31);

    set_isr(32, (unsigned int)irq0);
    set_isr(33, (unsigned int)irq1);
    set_isr(34, (unsigned int)irq2);
    set_isr(35, (unsigned int)irq3);
    set_isr(36, (unsigned int)irq4);
    set_isr(37, (unsigned int)irq5);
    set_isr(38, (unsigned int)irq6);
    set_isr(39, (unsigned int)irq7);
    set_isr(40, (unsigned int)irq8);
    set_isr(41, (unsigned int)irq9);
    set_isr(42, (unsigned int)irq10);
    set_isr(43, (unsigned int)irq11);
    set_isr(44, (unsigned int)irq12);
    set_isr(45, (unsigned int)irq13);
    set_isr(46, (unsigned int)irq14);
    set_isr(47, (unsigned int)irq15);

    /* Register a default handler for the PIT timer to avoid "Unhandled interrupt: 32" spam. */
    register_interrupt_handler(32, timer_stub);

    /* Register the page-fault handler (#PF, vector 14) for diagnostics. */
    register_interrupt_handler(14, page_fault_handler);

    /* Register the generic exception handler for common CPU faults. */
    register_interrupt_handler(0,  generic_exception_handler); /* #DE Divide Error    */
    register_interrupt_handler(6,  generic_exception_handler); /* #UD Invalid Opcode  */
    register_interrupt_handler(8,  generic_exception_handler); /* #DF Double Fault    */
    register_interrupt_handler(10, generic_exception_handler); /* #TS Invalid TSS     */
    register_interrupt_handler(11, generic_exception_handler); /* #NP Seg Not Present */
    register_interrupt_handler(12, generic_exception_handler); /* #SS Stack Fault     */
    register_interrupt_handler(13, generic_exception_handler); /* #GP General Prot.   */

    /* Syscall gate – DPL=3 so ring-3 code can invoke int 0x80. */
    idt_set_gate(128, (unsigned int)isr128, GDT_KERNEL_CODE_SELECTOR,
                 IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_INTERRUPT_32);
}

void register_interrupt_handler(unsigned int interrupt, isr_handler_t handler)
{
    if (interrupt < IDT_NUM_ENTRIES) {
        handlers[interrupt] = handler;
    }
}

unsigned int interrupt_handler(struct cpu_state *cpu, struct stack_state *stack, unsigned int interrupt)
{
    unsigned int new_esp = 0;

    /* Dispatch to a registered C handler first (e.g., keyboard driver). */
    if (interrupt < IDT_NUM_ENTRIES && handlers[interrupt]) {
        handlers[interrupt](cpu, stack, interrupt);
    } else if (interrupt != 32u) {
        /* Only log truly unhandled non-timer interrupts to avoid spam. */
        char buf[64];
        sprintf(buf, "Unhandled interrupt: %d", (int)interrupt);
        serial_write(buf);
        serial_write("\r\n");
    }

    /*
     * CFS scheduler tick – only for PIT (IRQ0 = vector 32).
     * sched_tick() returns a non-zero new kernel ESP when a context switch
     * is required; returning it causes common_isr_stub to switch stacks.
     * EOI must be sent BEFORE the potential stack switch so the PIC is
     * ready for the next interrupt in the new process context.
     */
    if (interrupt == 32u) {
        if (interrupt >= PIC1_OFFSET && interrupt <= PIC2_END) {
            pic_acknowledge(interrupt);
        }
        new_esp = sched_tick(cpu, interrupt);
        return new_esp;
    }

    /* Send EOI for all other hardware interrupts. */
    if (interrupt >= PIC1_OFFSET && interrupt <= PIC2_END) {
        pic_acknowledge(interrupt);
    }

    return 0;  /* no context switch */
}

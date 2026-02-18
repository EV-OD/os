#include "isr.h"
#include "pic.h"
#include "serial.h"
#include "stdio.h"
#include "string.h"

static isr_handler_t handlers[IDT_NUM_ENTRIES];

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
}

void register_interrupt_handler(unsigned int interrupt, isr_handler_t handler)
{
    if (interrupt < IDT_NUM_ENTRIES) {
        handlers[interrupt] = handler;
    }
}

void interrupt_handler(struct cpu_state *cpu, struct stack_state *stack, unsigned int interrupt)
{
    (void)cpu;
    (void)stack;

    if (interrupt < IDT_NUM_ENTRIES && handlers[interrupt]) {
        handlers[interrupt](cpu, stack, interrupt);
    } else {
        char buf[64];
        sprintf(buf, "Unhandled interrupt: %d", (int)interrupt);
        serial_write(buf);
        serial_write("\r\n");
    }

    if (interrupt >= PIC1_OFFSET && interrupt <= PIC2_END) {
        pic_acknowledge(interrupt);
    }
}

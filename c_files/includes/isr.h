#ifndef INCLUDE_ISR_H
#define INCLUDE_ISR_H

#include "idt.h"

typedef void (*isr_handler_t)(struct cpu_state *cpu, struct stack_state *stack, unsigned int interrupt);

void isr_install(void);
void register_interrupt_handler(unsigned int interrupt, isr_handler_t handler);

#endif /* INCLUDE_ISR_H */

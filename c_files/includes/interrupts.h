#ifndef INCLUDE_INTERRUPTS_H
#define INCLUDE_INTERRUPTS_H

/* Small helpers to control the CPU interrupt flag. */
static inline void interrupts_enable(void)
{
    __asm__ __volatile__("sti" : : : "memory");
}

static inline void interrupts_disable(void)
{
    __asm__ __volatile__("cli" : : : "memory");
}

static inline void cpu_halt(void) 
{ 
    __asm__ __volatile__("hlt"); 
}

#endif /* INCLUDE_INTERRUPTS_H */

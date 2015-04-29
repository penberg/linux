#ifndef _ASM_VAX_DELAY_H
#define _ASM_VAX_DELAY_H

#include <asm-generic/param.h>

static inline void __delay(unsigned long loops)
{
        asm volatile(
		"1:	sobgtr %0, 1b   \n"
		: "=r"(loops)
		: "0"(loops));

}

static inline void __udelay(unsigned long usecs)
{
	unsigned long loops_per_usec;

	loops_per_usec = (loops_per_jiffy * HZ) / 1000000;

	__delay(usecs * loops_per_usec);
}

#define udelay(usecs) __udelay(usecs)

#endif /* _ASM_VAX_DELAY_H */

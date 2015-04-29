#ifndef _ASM_VAX_IPR_H
#define _ASM_VAX_IPR_H

static inline void mtpr(unsigned long value, unsigned long reg)
{
	asm volatile(
		"       mtpr    %0, %1		\n"
		:
		: "g"(value), "g"(reg)
		: "memory");
}

static inline unsigned long mfpr(unsigned long reg)
{
	unsigned long value;

	asm volatile(
		"       mfpr    %1, %0		\n"
		: "=g"(value)
		: "g"(reg)
		: "memory");

	return value;
}

#endif /* _ASM_VAX_IPR_H */

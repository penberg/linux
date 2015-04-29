#ifndef _ASM_VAX_PSL_H
#define _ASM_VAX_PSL_H

/*
 * Processor status longword. See Table 1.2 of VAX Architecture Reference
 * Manual for details.
 */
struct psl {
        unsigned c:1;
        unsigned v:1;
        unsigned z:1;
        unsigned n:1;
        unsigned t:1;
        unsigned iv:1;
        unsigned fu:1;
        unsigned dv:1;
        unsigned reserved1:8;
        unsigned ipl:5;
        unsigned reserved2:1;
        unsigned prv_mod:2;
        unsigned cur_mod:2;
        unsigned is:1;
        unsigned fpd:1;
        unsigned reserved3:2;
        unsigned tp:1;
        unsigned cm:1;
};

#define PSL_MODE_KERNEL		0
#define PSL_MODE_EXEC		1
#define PSL_MODE_SUPER		2
#define PSL_MODE_USER		3

static inline void movpsl(struct psl *psl)
{
	asm volatile(
		"	movpsl	%0		\n"
		: "=g"(*psl)
		:
		: "memory");
}

#endif /* _ASM_VAX_PSL_H */

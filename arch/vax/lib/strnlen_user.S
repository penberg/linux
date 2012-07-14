/*
 * Copyright (C) 2001, Dave Airlie
 *
 * VAX Assembly implementation of strnlen
 */

#include <asm/errno.h>
#include <linux/linkage.h>

/* long __strnlen_user(const char *s, long n)
 * Returns either strlen s or n
 */
#define EX(insn, arg0, arg1, handler) \
9:	 insn	arg0, arg1; \
	.section __ex_table, "a"; \
	.align 2 ; \
	.long 9b, handler; \
	.previous

	.text
ENTRY(__strnlen_user)
	.word 0x3e
	movl 4(%ap), %r0
	movl 8(%ap), %r1
	movl %r0, %r2

1:	EX(movb, (%r0)+, %r3, fault)
	cmpb $0, %r3
	beql 2f
	sobgtr %r1, 1b
	incl %r0
2:	subl2 %r2, %r0
	ret

	.section .fixup,"ax"
fault:	movl $0, %r0
	ret


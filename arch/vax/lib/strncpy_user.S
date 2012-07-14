/*
 * Copyright (C) 2001, Dave Airlie
 *
 * VAX Assembly implementation of strncpy_from_user
 */

#include <asm/errno.h>
#include <linux/linkage.h>

/* int __strncpy_from_user(char *dst, const char *src, long count)
 * Returns number of bytes copied
 */

#define EX(insn, addr, reg, handler) \
9:	insn	addr, reg; \
	.section __ex_table, "a"; \
	.align 2 ; \
	.long 9b, handler; \
	.previous

	.text
ENTRY(__strncpy_from_user)
	.word 0x3e
	movl 4(%ap), %r2 /* r2 now has dst */
	movl 8(%ap), %r3 /* r3 now has src */
	movl 12(%ap), %r0 /* r0 has count */
	movl %r0, %r1 /* keep count in r1 */
	beql 2f
1:	EX(movb, (%r3)+, %r4, fault)
	movb %r4, (%r2)+
	cmpb $0, %r4
	beql 2f
	sobgtr %r1, 1b
2:	subl2 %r1, %r0
	ret
	.section .fixup, "ax"
fault:	movl $-EFAULT, %r0
	ret
	.previous


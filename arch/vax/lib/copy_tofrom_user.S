/*
 * Copyright (C) 2001, Dave Airlie
 *
 * VAX Assembly implementation of copy_tofrom_user
 */

#include <asm/errno.h>
#include <linux/linkage.h>

/*
 * int __copy_tofrom_user(void *to, const void *from, unsigned long size);
 */
#define EX(insn, arg0, arg1, handler) \
9:	 insn	arg0, arg1; \
	.section __ex_table, "a"; \
	.align 2 ; \
	.long 9b, handler; \
	.previous

	.text
ENTRY(__copy_tofrom_user)
	.word 0x3e
	movl 4(%ap), %r2  /* to in r2 */
	movl 8(%ap), %r3  /* from in r3 */
	movl 12(%ap), %r0 /* size in r0 */

1:	EX(movb, (%r3)+, %r4, l_fixup)
	EX(movb, %r4, (%r2)+, s_fixup)
	sobgtr %r0, 1b
	ret
	.section .fixup,"ax"
	.align 4
l_fixup:
s_fixup: ret
	.previous

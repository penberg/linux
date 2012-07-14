/*
 * Copyright (C) 2001, Dave Airlie
 *
 * VAX Assembly implementation of clear_user
 */

#include <asm/errno.h>
#include <linux/linkage.h>

/* unsigned long __clear_user(void *addr, unsigned long size);
 * number of bytes not cleared is returned
 */

#define EX(insn, addr, reg, handler) \
9:	insn	addr, reg; \
	.section __ex_table, "a"; \
	.align 2 ; \
	.long 9b, handler; \
	.previous

	.text
ENTRY(__clear_user)
	.word 0x3e
	movl 4(%ap), %r1 /* r1 now has addr */
	movl 8(%ap), %r0 /* r0 has size */
	beql 2f
1:	EX(movb, $0, (%r1)+, fault)
	sobgtr %r0, 1b
2:	ret

	.section .fixup, "ax"
fault:	ret
	.previous


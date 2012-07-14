/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Small optimized versions should generally be found as inline code
 * in <asm-xx/string.h>. However, if size matters (inlined way too
 * often) or if speed doesn't matter (overhead of a function call),
 * just drop them here.
 */
#include <linux/string.h>

void *memset(void *s, int c , __kernel_size_t count)
{
	asm (
	"	movl %2, %%r6			\n" /* R6 holds bytes left */
	"	movl %0, %%r3			\n" /* dest in R3 */
	"	movl $0xffff, %%r7		\n" /* R7 always holds 65535 */
	"next_chunk:				\n"
	"	cmpl %%r6, %%r7			\n"
	"	blequ last_chunk		\n" /* < 65535 bytes left */
	"	movc5 $0, 0, %1, %%r7, (%%r3)	\n" /* MOVC5 updates R3 for us */
	"	subl2 %%r7, %%r6		\n"
	"	brb next_chunk			\n"
	"last_chunk:				\n"
	"	movc5 $0, 0, %1, %%r6, (%%r3)	"
		: /* no outputs */
		: "g"(s), "g"(c), "g"(count)
		: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7");

	return s;
}


#include <asm/io.h>

static volatile unsigned long *ka52_console = NULL;

/*
 * FIXME: This function gets "unsigned char" instead of "int". All other
 *		console I/O functions get int, but that looks a bit
 *		non-intuitive for byte I/O ...
 * FIXME: 0x2004aaa8 isn't an official address. I wasn't ad hoc able to
 *		use the official string-printing function (see
 *		http://computer-refuge.org/classiccmp/dec94mds/473aamga.pdf,
 *		pp. B-4 ff.). This address (0x2004aaa8) is internally
 *		called for the purpose of printing out one byte to the
 *		console. Also, I was too lazy to properly check register
 *		usage of the subroutine, so I invalidate them all...
 */
void
ka52_prevm_putchar (unsigned char c)
{
	asm (
	"	movzbl	%0, %%r0	\n"
	"	jsb	0x2004aaa8	\n"
	: /* nothing */
	: "g"(c)
	: "r0", "r1", "r2", "r3", "r4", "r5",
	  "r6", "r7", "r8", "r9", "r10", "r11"
	  /* As it seems in theory, R2, R3 and R11 are PUSHRed by this
	   * subroutine, so (in theory) this shouldn't needed... */
	);

	return;
}

void
ka52_console_init (unsigned long address)
{
	ka52_console = ioremap (address, 8 * sizeof (unsigned long));
}

void
ka52_postvm_putchar (unsigned char c)
{
	unsigned long temp = c;

#if 0
	if (ka52_console)
		ka52_console[3] = temp;
#endif

	return;
}

unsigned char
ka52_prevm_getchar (void)
{
	asm ("halt");
	return 0;
}


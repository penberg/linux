/*
 * Dave Airlie wrote the original IP checksum code for Linux/VAX
 * in assembler (transliterating from the i386 version).
 *
 * In 2.5.69, the NFS client code was changed to use zero-copy
 * which leads to this function being called with odd-byte-aligned
 * buffers, which broke Dave's code.
 *
 * While fixing this, I re-wrote it in C, only using assembler for
 * the carry-handling that is impossible to do in C.  Some inspiration
 * came from NetBSD :-)  The generated looks as good as Dave's.
 *     - Kenn Humborg, 2003-10-01
 */

#include <asm/checksum.h>

unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum)
{
	int odd = 0;

	/* First we want to get aligned on a longword boundary,
	   so that our 32-bit operations later are as fast as
	   possible.  So deal with any non-aligned bytes first */

	/* But NetBSD doesn't try to align on 32-bits for very small
	   buffers (less than 16 bytes), so we won't either */
	if (len < 16) {
		goto short_buffer;
	}

	if (((unsigned int)buff) & 1) {
		/* Starts on odd boundary - pull in first byte.
		   And make a note that we need to byte swap at the end */

		int byte = *buff << 8;

		/* BTW, the funny positioning of the quotes here is
		   so that the assembly listing comes out aligned nicely */

		__asm__ __volatile (
		       "addl2 %2, %0	\n"
		"	adwc  $0, %0	"
			: "=r" (sum)
			: "0" (sum), "r" (byte)
		);

		odd = 1;
		buff++;
		len--;
	}

	if (((unsigned int)buff) & 2) {
		/* Still not on 32-bit boundary
		   And make a note that we need to byte swap at the end */

		int word = *(unsigned short *)buff;

		__asm__ __volatile (
		       "addl2 %2, %0	\n"
		"	adwc  $0, %0	"
			: "=r" (sum)
			: "0" (sum), "r" (word)
		);

		buff += 2;
		len -= 2;
	}

	/* Now we MUST be aligned on 32-bits */

	while (len >= 32) {
		__asm__ __volatile (
		       "addl2 (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc     $0, %0		"
			: "=r" (sum)
			: "0" (sum), "r" (buff)
		);
		len -= 32;
	}

	if (len >= 16) {
		__asm__ __volatile (
		       "addl2 (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc     $0, %0		"
			: "=r" (sum)
			: "0" (sum), "r" (buff)
		);
		len -= 16;
	}

short_buffer:
	if (len >= 8) {
		__asm__ __volatile (
		       "addl2 (%2)+, %0		\n"
		"	adwc  (%2)+, %0		\n"
		"	adwc     $0, %0		"
			: "=r" (sum)
			: "0" (sum), "r" (buff)
		);
		len -= 8;
	}

	if (len >= 4) {
		__asm__ __volatile (
		       "addl2 (%2)+, %0		\n"
		"	adwc     $0, %0		"
			: "=r" (sum)
			: "0" (sum), "r" (buff)
		);
		len -= 4;
	}

	if (len >= 2) {
		int word = *(unsigned short *)buff;
		__asm__ __volatile (
		       "addl2 %2, %0		\n"
		"	adwc  $0, %0		"
			: "=r" (sum)
			: "0" (sum), "r" (word)
		);
		buff += 2;
		len -= 2;
	}

	if (len > 0) {
		int byte = *buff;
		__asm__ __volatile (
		       "addl2 %2, %0		\n"
		"	adwc  $0, %0		"
			: "=r" (sum)
			: "0" (sum), "r" (byte)
		);
	}

	if (odd) {
		/*
		 * Need to byte-swap - just roll everything around
		 *through 8 bits.
		 */
		__asm__ __volatile (
		       "rotl $8, %0, %0		"
			: "=r" (sum)
			: "0" (sum)
		);
	}

	return sum;
}


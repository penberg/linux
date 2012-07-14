/*
 * Boot-time CPU identification - requires virtual memory to be
 * turned off (MAPEN=0).
 */

#include <asm/mtpr.h>		/* Processor register definitions */
#include <asm/mv.h>		/* machine vector definitions */
#include <linux/mm.h>		/* for PAGE_OFFSET and KERNEL_START_PHYS */
#include <asm/system.h>		/* for HALT */

/*
 * Given a virtual address in the final kernel image (i.e. an S0
 * address like 0x80123456, convert it to the corresponding address
 * in the loaded kernel before we relocate (which depends on the
 * exact load address)
 */
static void *
s0vmaddr_to_load_addr(void *vaddr, unsigned int kernel_load_addr)
{
	return (char *) vaddr - PAGE_OFFSET - KERNEL_START_PHYS + kernel_load_addr;
}

struct vax_mv *
idcpu (unsigned int kernel_load_addr)
{
	extern struct cpu_match __init_cpumatch_start, __init_cpumatch_end;
	struct cpu_match *match = &__init_cpumatch_start;
	unsigned long sid;
	unsigned long sidex;
	unsigned int i;
	unsigned int num_matches;
	struct vax_mv *retmv;

	sid = __mfpr (PR_SID);
	num_matches = &__init_cpumatch_end - &__init_cpumatch_start;

	for (i = 0; i < num_matches; i++) {
		if ((sid & match[i].sid_mask) == match[i].sid_match) {
			/*
			 * No sidex known? Accept the vector.
			 * FIXME: Maybe sort the metch structs to have
			 * those with "long" masks first, then the loose
			 * entries with weaker/shorter masks
			 */
			if (!match[i].sidex_addr)
				return s0vmaddr_to_load_addr(match[i].mv, kernel_load_addr);

			/*
			 * If a SIDEX match was supplied, too, check it!
			 */
			sidex = * ((unsigned long *) match[i].sidex_addr);
			if ((sidex & match[i].sidex_mask) == match[i].sidex_match) {
				retmv = s0vmaddr_to_load_addr(match[i].mv, kernel_load_addr);
				retmv->sidex = sidex;
				return retmv;
			}
		}
	}

	/*
	 * No matching vector found, so you're on your own to get a SID
	 * and SIDEX value and add it to one of the existing vectors (if
	 * that works for you) or create an own vector for your machine.
	 */
	HALT;

	/* Not reached */
	return NULL;
}


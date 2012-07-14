/*
 * Copyright (C) 2000  Mattias Nordlund
 *
 * This file contains machine vector handlers for the
 * KA640 CPU in the MicroVAX 3400 series machines
 */

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>		/* For clock_init routines */

static unsigned int *ka640_cacr = (unsigned int *)0x20084000;

static void ka640_pre_vm_init(void)
{
	 __mtpr(0, PR_CADR);
}

static void ka640_post_vm_init(void)
{
#define KA640_CADR_S2E 0x80  /* Enable set 2 of level 1 cache */
#define KA640_CADR_S1E 0x40  /* Enable set 1 of level 1 cache */
#define KA640_CADR_ISE 0x20  /* Enable instruction caching in level 1 cache */
#define KA640_CADR_DSE 0x10  /* Enable data caching in level 1 cache */

	/*
	 * Writing to PR_CADR on the CVAX chip implicitly clears
	 * the level 1 cache
	 */
	__mtpr(KA640_CADR_S2E|KA640_CADR_S1E|KA640_CADR_ISE|KA640_CADR_DSE, PR_CADR);
}

static const char *ka640_cpu_type_str(void)
{
	return "KA640";
}

struct vax_mv mv_ka640 = {
	.pre_vm_init = ka640_pre_vm_init,
	.post_vm_init = ka640_post_vm_init,
	.pre_vm_putchar = mtpr_putchar,
	.pre_vm_getchar = mtpr_getchar,
	.post_vm_putchar = mtpr_putchar,
	.post_vm_getchar = mtpr_getchar,
	.cpu_type_str = ka640_cpu_type_str,
	.clock_init = generic_clock_init,
};

static struct cpu_match __CPU_MATCH cpumatch_ka640 = {
        .mv = &mv_ka640,
        .sid_mask = VAX_SID_FAMILY_MASK,
        .sid_match = VAX_CVAX << VAX_SID_FAMILY_SHIFT,

        .sidex_addr = CVAX_SIDEX_ADDR,

        .sidex_mask = CVAX_SIDEX_TYPE_MASK | CVAX_Q22_SUBTYPE_MASK,
        .sidex_match = (CVAX_SIDEX_TYPE_Q22 << CVAX_SIDEX_TYPE_SHIFT) |
                        (CVAX_Q22_SUBTYPE_KA640 << CVAX_Q22_SUBTYPE_SHIFT),
};


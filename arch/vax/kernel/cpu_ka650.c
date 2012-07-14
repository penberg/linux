/*
 * Copyright (C) 2000  Kenn Humborg
 *
 * This file contains machine vector handlers for the
 * KA650 CPU in the MicroVAX III series machines
 */

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <linux/init.h>
#include <linux/device.h>

#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>		/* For clock_init routines */

static unsigned int *ka650_cacr = (unsigned int *) 0x20084000;

static void ka650_pre_vm_init(void)
{
	/*
	 * Disable the level 1 and level 2 caches.  My docs say that the
	 * caches are disabled automatically at power up and when DCOK
	 * is negated when the processor is halted.  The firmware BOOT
	 * command might also do this, but I can't find any docs to
	 * prove this.
	 */
	__mtpr(0, PR_CADR);

	*ka650_cacr = 0;

	/*
	 * We need to clear out the second level cache at some point.
	 * On the KA650, you do this by writing directly to the cache
	 * diagnostic space at 0x10000000 (physical).  The cache enable
	 * bit is also set here, but the cache won't actually start
	 * caching until the level 1 cache is enabled in post_vm_init()
	 */
#define KA650_CACR_CPE 0x20  /* Level 2 cache parity error (write to clear) */
#define KA650_CACR_CEN 0x10  /* Level 2 cache enable */
#define KA650_L2CACHE_DIAG_ADDR 0x10000000
#define KA650_L2CACHE_DIAG_SIZE 0x00010000
	memset((void *)KA650_L2CACHE_DIAG_ADDR, 0, KA650_L2CACHE_DIAG_SIZE);
	*ka650_cacr = KA650_CACR_CPE | KA650_CACR_CEN;
}

static void ka650_post_vm_init(void)
{
#define KA650_CADR_S2E 0x80  /* Enable set 2 of level 1 cache */
#define KA650_CADR_S1E 0x40  /* Enable set 1 of level 1 cache */
#define KA650_CADR_ISE 0x20  /* Enable instruction caching in level 1 cache */
#define KA650_CADR_DSE 0x10  /* Enable data caching in level 1 cache */

	/*
	 * Writing to PR_CADR on the CVAX chip implicitly clears
	 * the level 1 cache.
	 */
	__mtpr(KA650_CADR_S2E|KA650_CADR_S1E|KA650_CADR_ISE|KA650_CADR_DSE, PR_CADR);
}

static const char *ka650_cpu_type_str(void)
{
	return "KA650";
}

struct vax_mv mv_ka650 = {
	.pre_vm_init = ka650_pre_vm_init,
	.post_vm_init = ka650_post_vm_init,
	.pre_vm_putchar = mtpr_putchar,
	.pre_vm_getchar = mtpr_getchar,
	.post_vm_putchar = mtpr_putchar,
	.post_vm_getchar = mtpr_getchar,
	.cpu_type_str = ka650_cpu_type_str,
	.clock_init = generic_clock_init,
};

static struct cpu_match __CPU_MATCH cpumatch_ka650 = {
	.mv = &mv_ka650,
	.sid_mask = VAX_SID_FAMILY_MASK,
	.sid_match = VAX_CVAX << VAX_SID_FAMILY_SHIFT,

	.sidex_addr = CVAX_SIDEX_ADDR,

	.sidex_mask = CVAX_SIDEX_TYPE_MASK | CVAX_Q22_SUBTYPE_MASK,
	.sidex_match = (CVAX_SIDEX_TYPE_Q22 << CVAX_SIDEX_TYPE_SHIFT) |
			(CVAX_Q22_SUBTYPE_KA650 << CVAX_Q22_SUBTYPE_SHIFT),
};

static struct cpu_match __CPU_MATCH cpumatch_ka655 = {
	.mv = &mv_ka650,
	.sid_mask = VAX_SID_FAMILY_MASK,
	.sid_match = VAX_CVAX << VAX_SID_FAMILY_SHIFT,

	.sidex_addr = CVAX_SIDEX_ADDR,

	.sidex_mask = CVAX_SIDEX_TYPE_MASK | CVAX_Q22_SUBTYPE_MASK,
	.sidex_match = (CVAX_SIDEX_TYPE_Q22 << CVAX_SIDEX_TYPE_SHIFT) |
			(CVAX_Q22_SUBTYPE_KA655 << CVAX_Q22_SUBTYPE_SHIFT),
};

static struct platform_device ka650_cqbic_device = {
	.name = "cqbic"
};

static struct platform_device ka650_iprcons_device = {
	.name = "iprcons"
};

static int __init ka650_platform_device_init(void)
{
	if (!is_ka650())
		return -ENODEV;

	platform_device_register(&ka650_cqbic_device);
	platform_device_register(&ka650_iprcons_device);

	return 0;
}

arch_initcall(ka650_platform_device_init);


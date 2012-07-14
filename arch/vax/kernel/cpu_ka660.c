/*
 *  This file contains machine vector handlers for the
 *  KA660 CPU in the VAXserver 4000-200 machines.
 *
 *  For the VAXserver machines I have, the SID is 14000006 and
 *  the sidex is 01370502.  The sidex seems to have a simlar
 *  breakdown that a CVAX with a Q22 bus has.  Bootstrap indicates
 *  a firmware rev 3.7 supporting this assumption.  If anyone knows
 *  differently, let me know.
 *
 */

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <linux/init.h>
#include <linux/device.h>

#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>		/* For clock_init routines */

static const char *ka660_cpu_type_str(void)
{
	return "KA660";
}

struct vax_mv mv_ka660 = {
	.pre_vm_putchar = mtpr_putchar,
	.pre_vm_getchar = mtpr_getchar,
	.post_vm_putchar = mtpr_putchar,
	.post_vm_getchar = mtpr_getchar,
	.cpu_type_str = ka660_cpu_type_str,
	.clock_init = generic_clock_init,
};

static struct cpu_match __CPU_MATCH cpumatch_ka660 = {
        .mv = &mv_ka660,
        .sid_mask = VAX_SID_FAMILY_MASK,
        .sid_match = VAX_SOC << VAX_SID_FAMILY_SHIFT,

        .sidex_addr = SOC_SIDEX_ADDR,

        .sidex_mask = SOC_SIDEX_TYPE_MASK | SOC_Q22_SUBTYPE_MASK,
        .sidex_match = (SOC_SIDEX_TYPE_Q22 << SOC_SIDEX_TYPE_SHIFT) |
                        (SOC_Q22_SUBTYPE_KA660 << SOC_Q22_SUBTYPE_SHIFT),
};

static struct platform_device ka660_cqbic_device = {
	.name = "cqbic"
};

static struct platform_device ka660_iprcons_device = {
	.name = "iprcons"
};

static int __init ka660_platform_device_init(void)
{
	if (!is_ka660())
		return -ENODEV;

	platform_device_register(&ka660_cqbic_device);
	platform_device_register(&ka660_iprcons_device);

	return 0;
}

arch_initcall(ka660_platform_device_init);


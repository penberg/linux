/*
 *  This file contains machine vector handlers for the
 *  VXT CPU in the VXT2000 machines.
 *
 *  From mailing list messages the SID is 14000006 but NetBSD uses 14000008.
 *
 *  This may work for other ka48 based systems,
 *
 *  may 2002. It looks as if the 20040058 address is right for prom output.
 */

#warning recent VXT work in 2.4 needs to be pulled over

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <linux/device.h>
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/system.h>
#include <asm/clock.h>		/* For clock_init routines */

static void vxt_pre_vm_init(void)
{
}

static void vxt_post_vm_init(void)
{
	init_vxt2694_console (0x200a0000);
}

static const char *vxt_cpu_type_str(void)
{
	if (mv->sidex == 0x08050002 /* FIXME */)
		return "VXT2000+";
	else
		return "probably VXT2000";
}

struct vax_mv mv_vxt = {
	.pre_vm_init = vxt_pre_vm_init,
	.post_vm_init = vxt_post_vm_init,
	.pre_vm_putchar = ka4x_prom_putchar,
	.pre_vm_getchar = ka4x_prom_getchar,
	.post_vm_putchar = vxt2694_putchar,
	.post_vm_getchar = vxt2694_getchar,
	.cpu_type_str = vxt_cpu_type_str,
	.clock_init = generic_clock_init,
	.keep_early_console = 1,
};

static struct cpu_match __CPU_MATCH cpu_vxt = {
        .mv = &mv_vxt,
        .sid_mask = VAX_SID_FAMILY_MASK,
        .sid_match = VAX_SOC << VAX_SID_FAMILY_SHIFT,

        .sidex_addr = SOC_SIDEX_ADDR,

        .sidex_mask = SOC_SIDEX_TYPE_MASK,
        .sidex_match = SOC_SIDEX_TYPE_VXT << SOC_SIDEX_TYPE_SHIFT
};

static struct platform_device vxt_diag_led_device = {
	.name = "diag_led"
};

static int __init vxt_platform_device_init (void)
{
	if (!is_vxt())
		return -ENODEV;

	platform_device_register (&vxt_diag_led_device);

	return 0;
}

arch_initcall (vxt_platform_device_init);


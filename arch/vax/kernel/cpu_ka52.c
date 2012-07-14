/*
 * Copyright (C) 2005 by Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *
 * This file contains generic machine vector handler for the
 * KA52 CPU (used in VAXstations 4000 Model 100A)
 */

#include <linux/types.h>   /* For NULL */
#include <linux/kernel.h>  /* For printk */
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/ka46.h>
#include <asm/clock.h>   /* for clock_init routines */
#include <asm/bus/vsbus.h>

/* From bootcons/ */
extern void ka52_prevm_putchar (unsigned char c);
extern void ka52_console_init (unsigned long address);
extern void ka52_postvm_putchar (unsigned char c);



static void ka52_post_vm_init(void)
{
#if 0
	//ka52_console_init (0x20140080);
	ka52_console_init (0x25000000);
#endif
#ifdef CONFIG_DZ
	init_dz11_console(0x25000000, 3);
	dz_serial_console_init();
#endif
}

static const char *ka52_cpu_type_str(void)
{
	return "KA52";
}

static void ka52_pre_vm_init(void)
{
	//__mtpr(PR_ACCS, 2);	/* Enable floating points */
}

static void
ka52_mcheck (void *stkframe)
{
	return;
}

struct vax_mv mv_ka52 = {
	.pre_vm_init = ka52_pre_vm_init,
	.post_vm_init = ka52_post_vm_init,
	.pre_vm_putchar = ka52_prevm_putchar,
	.pre_vm_getchar = ka46_48_49_prom_getchar,
	.post_vm_putchar = ka46_48_49_prom_putchar /*ka52_postvm_putchar*/,
	.post_vm_getchar = dz11_getchar,
	.cpu_type_str = ka52_cpu_type_str,
	//.clock_init = ka4x_clock_init,
	.mcheck = ka52_mcheck,
	.nicr_required = 1,
};

static struct cpu_match __CPU_MATCH cpumatch_ka52 = {
	.mv = &mv_ka52,
	.sid_mask = VAX_SID_FAMILY_MASK,
	.sid_match = VAX_NVAX << VAX_SID_FAMILY_SHIFT,
	.sidex_addr = NVAX_SIDEX_ADDR,
	.sidex_mask = 0x00000000,
	.sidex_match = 0x00000000,
};

static struct platform_device ka52_vsbus_device = {
	.name = "ka4x-vsbus"
};

static struct platform_device ka52_diag_led_device = {
	.name = "diag_led"
};

static int __init ka52_platform_device_init(void)
{
	int retval;

	if (!is_ka52 ())
		return -ENODEV;

	platform_device_register (&ka52_diag_led_device);

	retval = platform_device_register (&ka52_vsbus_device);
	if (!retval) {
#ifdef CONFIG_VSBUS
		vsbus_add_fixed_device (&ka52_vsbus_device.dev, "lance", 0x200e0000, 1);
		vsbus_add_fixed_device (&ka52_vsbus_device.dev, "dz", 0x200a0000, 4);
#endif
	}

	return retval;
}

arch_initcall (ka52_platform_device_init);


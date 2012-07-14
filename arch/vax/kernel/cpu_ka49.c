/*
 *  Copyright (C) 2004 by Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *
 *  This file contains a machine vector for the KA49 CPU.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>   /* for clock_init routines */
#include <asm/bus/vsbus.h>

static void ka49_post_vm_init(void)
{
	int start, slut;

#ifdef CONFIG_DZ
	init_dz11_console (0x25000000, 3);
	dz_serial_console_init ();
#endif
	/*
	 * Enable Caches
	 */
#define PR_CCTL		0x0a
#define CCTL_ENABLE	0x00000001
#define CCTL_SSIZE	0x00000002
#define CCTL_VSIZE	0x00000004
#define CCTL_SW_ETM	0x40000000
#define CCTL_HW_ETM	0x80000000

#define PR_BCETSTS	0xa3
#define PR_BCEDSTS	0xa6
#define PR_NESTS	0xae

#define PR_VMAR		0xd0
#define PR_VTAG		0xd1
#define PR_ICSR		0xd3
#define ICSR_ENABLE	0x01

#define PR_PCCTL	0xf8
#define PCCTL_P_EN	0x10	/* Primary Cache Enable */
#define PCCTL_I_EN	0x02	/* Instruction Cache Enable */
#define PCCTL_D_EN	0x01	/* Data Cache Enable */

	/*
	 * Caches off
	 */
	__mtpr (0, PR_ICSR);
	__mtpr (0, PR_PCCTL);
	__mtpr (__mfpr (PR_CCTL) | CCTL_SW_ETM, PR_CCTL);

	/*
	 * Invalidate Caches
	 */
	__mtpr (__mfpr (PR_CCTL) | 0x10, PR_CCTL);	/* Set Cache Size */
	__mtpr (__mfpr (PR_BCETSTS), PR_BCETSTS);	/* Clear Error Bits */
	__mtpr (__mfpr (PR_BCEDSTS), PR_BCEDSTS);	/* Clear Error Bits */
	__mtpr (__mfpr (PR_NESTS), PR_NESTS);		/* Clear Error Bits */

	/*
	 * Flush Cache Lines
	 */
	start = 0x01400000;
	slut = 0x01440000;
	for (; start < slut; start += 0x20)
		__mtpr (0, start);
	__mtpr ((__mfpr (PR_CCTL) & ~(CCTL_SW_ETM | CCTL_ENABLE)) | CCTL_HW_ETM, PR_CCTL);

	/*
	 * Clear Tag and Valid
	 */
	start = 0x01000000;
	slut = 0x01040000;
	for (; start < slut; start += 0x20)
		__mtpr (0, start);
	__mtpr (__mfpr (PR_CCTL) | 0x10 | CCTL_ENABLE, PR_CCTL);	/* Enable BCache */

	/*
	 * Clear Primary Cache (2nd level, 8KB, on-CPU)
	 */
	start = 0x01800000;
	slut = 0x01802000;
	for (; start < slut; start += 0x20)
		__mtpr (0, start);

	/*
	 * Flush Instruction Cache
	 */
	flush_icache ();

	/*
	 * Enable Primary Cache
	 */
	__mtpr (PCCTL_P_EN | PCCTL_I_EN | PCCTL_D_EN, PR_PCCTL);

	/*
	 * Enable Virtual Instruction Cache (1st level, 2KB, on-CPU)
	 */
	start = 0x00000000;
	slut = 0x00000800;
	for (; start < slut; start += 0x20) {
		__mtpr (start, PR_VMAR);
		__mtpr (0, PR_VTAG);
	}
	__mtpr (ICSR_ENABLE, PR_ICSR);

	return;
}

static const char *ka49_cpu_type_str(void)
{
	return "KA49";
}

static void ka49_init_devices(void)
{
	printk ("ka49: init_devices\n");
}

struct vax_mv mv_ka49 = {
	.post_vm_init = ka49_post_vm_init,
	.pre_vm_putchar = ka46_48_49_prom_putchar,
	.pre_vm_getchar = ka46_48_49_prom_getchar,
	.post_vm_putchar = dz11_putchar,
	.post_vm_getchar = dz11_getchar,
	.init_devices = ka49_init_devices,
	.cpu_type_str = ka49_cpu_type_str,
	.clock_init = ka4x_clock_init,
	.nicr_required = 1,
};

static struct cpu_match __CPU_MATCH cpumatch_ka49 = {
	.mv = &mv_ka49,
	.sid_mask = VAX_SID_FAMILY_MASK,
	.sid_match = VAX_NVAX << VAX_SID_FAMILY_SHIFT,
	.sidex_addr = NVAX_SIDEX_ADDR,
	.sidex_mask = 0xffffffff,	/* Don't yet know how to interpret
					   SID + SIDEX, so keep it tight */
	.sidex_match = 0x04010002,
};

static struct platform_device ka49_vsbus_device = {
	.name = "ka4x-vsbus"
};

static struct platform_device ka49_diag_led_device = {
	.name = "diag_led"
};

static int __init ka49_platform_device_init(void)
{
	int retval;

	if (!is_ka49())
		return -ENODEV;

	platform_device_register (&ka49_diag_led_device);

	retval = platform_device_register (&ka49_vsbus_device);
	if (!retval) {
#ifdef CONFIG_VSBUS
		vsbus_add_fixed_device(&ka49_vsbus_device.dev, "sgec", 0x20008000, 1);
		vsbus_add_fixed_device(&ka49_vsbus_device.dev, "dz", 0x25000000, 4);
#endif
	}

	return retval;
}

arch_initcall (ka49_platform_device_init);


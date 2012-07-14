/*
 * Copyright (C) 2000  Kenn Humborg
 *
 * This file contains generic machine vector handlers for the
 * KA42 CPUs in the early CVAX-based VAXstation 3100
 * machines (models 10 to 48)
 */

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <linux/init.h>
#include <linux/device.h>
#include <linux/config.h>
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>		/* For clock_init routines */
#include <asm/bus/vsbus.h>

static void ka42_post_vm_init(void)
{
#define KA42_CADR_S2E 0x80
#define KA42_CADR_S1E 0x40
#define KA42_CADR_ISE 0x20
#define KA42_CADR_DSE 0x10
	__mtpr(KA42_CADR_S2E|KA42_CADR_S1E|KA42_CADR_ISE|KA42_CADR_DSE, PR_CADR);

#ifdef CONFIG_DZ
	init_dz11_console(0x200A0000, 3);
	dz_serial_console_init();
#endif
}

static const char *ka42_cpu_type_str(void)
{
	return "KA42";
}

struct vax_mv mv_ka42 = {
	.post_vm_init = ka42_post_vm_init,
	.pre_vm_putchar = ka4x_prom_putchar,
	.pre_vm_getchar = ka4x_prom_getchar,
	.post_vm_putchar = dz11_putchar,
	.post_vm_getchar = dz11_getchar,
	.cpu_type_str = ka42_cpu_type_str,
	.clock_init = ka4x_clock_init,
};

static struct cpu_match __CPU_MATCH cpu_match_ka42 = {
        .mv = &mv_ka42,
        .sid_mask = VAX_SID_FAMILY_MASK,
        .sid_match = VAX_CVAX << VAX_SID_FAMILY_SHIFT,

        .sidex_addr = CVAX_SIDEX_ADDR,
        .sidex_mask = CVAX_SIDEX_TYPE_MASK,
        .sidex_match = CVAX_SIDEX_TYPE_VS3100 << CVAX_SIDEX_TYPE_SHIFT,
};

static struct platform_device ka42_vsbus_device = {
	.name = "ka4x-vsbus"
};

static struct platform_device ka42_diag_led_device = {
	.name = "diag_led"
};

static int __init ka42_platform_device_init(void)
{
	int retval;

	if (!is_ka42())
		return -ENODEV;

	platform_device_register(&ka42_diag_led_device);

	retval = platform_device_register(&ka42_vsbus_device);
	if (!retval) {
#ifdef CONFIG_VSBUS
		vsbus_add_fixed_device(&ka42_vsbus_device.dev, "lance", 0x200e0000, 5);
		vsbus_add_fixed_device(&ka42_vsbus_device.dev, "dz", 0x200a0000, 6);

		/* Register internal SCSI bus */
		vsbus_add_fixed_device(&ka42_vsbus_device.dev, "vax-5380-int", 0x200c0080, 1);

		/* Register external SCSI bus */
		vsbus_add_fixed_device(&ka42_vsbus_device.dev, "vax-5380-ext", 0x200c0180, 0);
#endif
	}

	return retval;
}

arch_initcall(ka42_platform_device_init);


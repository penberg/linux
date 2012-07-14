/*
 * Copyright (C) 2005 by Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *
 * This file contains generic machine vector handlers for the
 * KA41 CPU of the MicroVAX 3100.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/config.h>
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>
#include <asm/bus/vsbus.h>

static void ka41_post_vm_init(void)
{
#ifdef CONFIG_DZ
	init_dz11_console(0x200A0000, 3);
	dz_serial_console_init();
#endif
}

static const char *ka41_cpu_type_str(void)
{
	return "KA41";
}

struct vax_mv mv_ka41 = {
	.post_vm_init = ka41_post_vm_init,
	.pre_vm_putchar = ka4x_prom_putchar,
	.pre_vm_getchar = ka4x_prom_getchar,
	.post_vm_putchar = dz11_putchar,
	.post_vm_getchar = dz11_getchar,
	.cpu_type_str = ka41_cpu_type_str,
	.clock_init = ka4x_clock_init,
};

static struct cpu_match __CPU_MATCH cpu_match_ka41 = {
        .mv = &mv_ka41,
        .sid_mask = VAX_SID_FAMILY_MASK,
        .sid_match = VAX_CVAX << VAX_SID_FAMILY_SHIFT,

        .sidex_addr = CVAX_SIDEX_ADDR,
        .sidex_mask = CVAX_SIDEX_TYPE_MASK,
        .sidex_match = CVAX_SIDEX_TYPE_VS3100 << CVAX_SIDEX_TYPE_SHIFT,
};

static struct platform_device ka41_vsbus_device = {
	.name = "ka4x-vsbus"
};

static struct platform_device ka41_diag_led_device = {
	.name = "diag_led"
};

static int __init ka41_platform_device_init (void)
{
	int retval;

	if (!is_ka41 ())
		return -ENODEV;

	platform_device_register (&ka41_diag_led_device);

	retval = platform_device_register (&ka41_vsbus_device);
	if (!retval) {
#ifdef CONFIG_VSBUS
		vsbus_add_fixed_device (&ka41_vsbus_device.dev, "lance", 0x200e0000, 5);
		vsbus_add_fixed_device (&ka41_vsbus_device.dev, "dz", 0x200a0000, 6);

		/* Register internal SCSI bus */
		vsbus_add_fixed_device (&ka41_vsbus_device.dev, "vax-5380-int", 0x200c0080, 1);

		/* Register external SCSI bus */
		vsbus_add_fixed_device (&ka41_vsbus_device.dev, "vax-5380-ext", 0x200c0180, 0);
#endif
	}

	return retval;
}

arch_initcall (ka41_platform_device_init);


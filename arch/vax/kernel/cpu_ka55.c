/*
 * Copyright (C) 2000  Kenn Humborg
 *
 * This file contains generic machine vector handlers for the
 * KA5 CPU in the NVAX-based MicroVAX 3100 Model 85
 */

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <linux/config.h>
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>		/* For clock_init routines */

static void ka55_prom_putchar(int c)
{
	asm (
	"	movl $0x2014044b, %%r11		# console page addr	\n"
	"1:	jsb *0x20(%%r11)		# ready to TX?		\n"
	"	blbc %%r0, 1b						\n"
	"	movl %0, %%r1						\n"
	"	jsb *0x24(%%r11)		# TX char in R11	\n"
	: /* no outputs */
	: "g"(c)
	: "r0", "r1", "r11");
}

static int ka55_prom_getchar(void)
{
	/* Not yet implemented */
	asm("halt");
	return 0;
}

static void ka55_post_vm_init(void)
{
#ifdef CONFIG_DZ
	init_dz11_console(0x25000000, 3);
	dz_serial_console_init();
#endif
}

static const char *ka55_cpu_type_str(void)
{
	return "KA55";
}

static void ka55_init_devices(void)
{
}

struct vax_mv mv_ka55 = {
	.post_vm_init = ka55_post_vm_init,
	.pre_vm_putchar = ka55_prom_putchar,
	.pre_vm_getchar = ka55_prom_getchar,
	.post_vm_putchar = dz11_putchar,
	.post_vm_getchar = dz11_getchar,
	.init_devices = ka55_init_devices,
	.cpu_type_str = ka55_cpu_type_str,
	.clock_init = generic_clock_init,
};

#warning "KA55 needs a struct cpumatch"

#warning "KA55 needs a platform_device_init function"


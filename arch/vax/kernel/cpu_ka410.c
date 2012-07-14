/*
 *  Copyright (C) 2000  Kenn Humborg
 *
 *  This file contains machine vector handlers for the
 *  KA410 CPU in the MicroVAX 2000 machines
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>	/* For printk */
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>		/* For clock_init routines */



static void ka410_post_vm_init(void)
{
#ifdef CONFIG_DZ
	init_dz11_console(0x200A0000, 3);
	dz_serial_console_init();
#endif
}

static const char *ka410_cpu_type_str(void)
{
	return "KA410";
}

struct vax_mv mv_ka410 = {
	.post_vm_init = ka410_post_vm_init,
	.pre_vm_putchar = ka4x_prom_putchar,
	.pre_vm_getchar = ka4x_prom_getchar,
	.post_vm_putchar = dz11_putchar,
	.post_vm_getchar = dz11_getchar,
	.cpu_type_str = ka410_cpu_type_str,
	.clock_init = ka4x_clock_init,
};

static struct cpu_match __CPU_MATCH cpumatch_ka410 = {
        .mv = &mv_ka410,
        .sid_mask = VAX_SID_FAMILY_MASK | UVAX2_SID_SUBTYPE_MASK,
        .sid_match = (VAX_UVAX2 << VAX_SID_FAMILY_SHIFT) |
			(UVAX2_SID_SUBTYPE_KA410 << UVAX2_SID_SUBTYPE_SHIFT),
        .sidex_addr = 0,
        .sidex_mask = 0x00000000,
        .sidex_match = 0x00000000,
};


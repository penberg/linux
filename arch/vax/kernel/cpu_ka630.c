/*
 * Copyright (C) 2000  Kenn Humborg
 *
 * This file contains machine vector handlers for the
 * KA630 CPU in the MicroVAX II machines
 */

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>		/* For clock_init routines */

static const char *ka630_cpu_type_str(void)
{
	return "KA630";
}

struct vax_mv mv_ka630 = {
	.pre_vm_putchar = mtpr_putchar,
	.pre_vm_getchar = mtpr_getchar,
	.post_vm_putchar = mtpr_putchar,
	.post_vm_getchar = mtpr_getchar,
	.cpu_type_str = ka630_cpu_type_str,
	.clock_init = generic_clock_init,
};

static struct cpu_match __CPU_MATCH cpumatch_ka630 = {
        .mv = &mv_ka630,
        .sid_mask = VAX_SID_FAMILY_MASK | UVAX2_SID_SUBTYPE_MASK,
        .sid_match = (VAX_UVAX2 << VAX_SID_FAMILY_SHIFT) |
                        (UVAX2_SID_SUBTYPE_KA630 << UVAX2_SID_SUBTYPE_SHIFT),
        .sidex_addr = 0,
        .sidex_mask = 0x00000000,
        .sidex_match = 0x00000000,
};

static struct cpu_match __CPU_MATCH cpumatch_charon = {
        .mv = &mv_ka630,
        .sid_mask = VAX_SID_FAMILY_MASK | UVAX2_SID_SUBTYPE_MASK,
        .sid_match = (VAX_UVAX2 << VAX_SID_FAMILY_SHIFT) |
                        (UVAX2_SID_SUBTYPE_KA630 << UVAX2_SID_SUBTYPE_SHIFT),
        .sidex_addr = 0,
        .sidex_mask = 0x00000000,
        .sidex_match = 0x00000000,
};


/*
 * Experimental CPU vector for my 6000/320
 */

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/clock.h>		/* For clock_init routines */

static const char *ka62_cpu_type_str(void)
{
	return "KA62";
}

struct vax_mv mv_ka62 = {
	.pre_vm_putchar = mtpr_putchar,
	.pre_vm_getchar = mtpr_getchar,
	.post_vm_putchar = mtpr_putchar,
	.post_vm_getchar = mtpr_getchar,
	.cpu_type_str = ka62_cpu_type_str,
	.clock_init = generic_clock_init,
};

static struct cpu_match __CPU_MATCH cpumatch_ka62 = {
        .mv = &mv_ka62,
        .sid_mask = 0xffffffff,
        .sid_match = 0x0a000005,
        .sidex_addr = 0,
};


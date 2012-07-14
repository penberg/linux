/* vax_dev_init.c
 *  atp Feb 2001
 *
 *  Called from initial do_basic_setup in linux/init/main.c
 *  Initialise devices according to mv.
 *
 *  Add any other vax device specific initialisation stuff here.
 */
#include <linux/types.h>   /* For NULL */
#include <linux/kernel.h>  /* For printk */
#include <linux/init.h>

#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>

static int __init vax_dev_init(void)
{
	if (mv->init_devices) {
		mv->init_devices();
	}

	return 1;
}

subsys_initcall(vax_dev_init);


/*
 * Copyright (C) 2004  Bérczi Gábor (Gabucino)
 * Based on cpu_ka46.c Copyright (C) 2000  Kenn Humborg
 *
 * This file contains generic machine vector handlers for the
 * KA48 CPU in the VAXstation 4000/VLC
 */

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/config.h>

#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/ka48.h>
#include <asm/clock.h>		/* For clock_init routines */
#include <asm/bus/vsbus.h>

static void ka48_post_vm_init(void)
{
#ifdef CONFIG_DZ
	init_dz11_console(0x200A0000, 3);
	dz_serial_console_init();
#endif
}

static const char *ka48_cpu_type_str(void)
{
	return "KA48";
}

static void ka48_cache_disable(void)
{
	long *par_ctl = (long *)KA48_PARCTL;

	__mtpr(0, PR_CADR);			/* Disable */
	*par_ctl &= ~KA48_PARCTL_INVENA;	/* Clear? Invalid enable */
	__mtpr(2, PR_CADR);			/* Flush */
}

static void ka48_cache_clear(void)
{
	int *tmp, i;

	/* Clear caches */
	tmp = (void *)KA48_INVFLT;	/* Inv filter */
	for (i = 0; i < KA48_INVFLTSZ / sizeof(int); i++)
		tmp[i] = 0;
}

static void ka48_cache_enable(void)
{
	/* Enable cache */
       long *par_ctl = (long *)KA48_PARCTL;

	*par_ctl |= KA48_PARCTL_INVENA;		/* Enable ???? */
	__mtpr(4, PR_CADR);			/* enable cache */
	*par_ctl |= (KA48_PARCTL_AGS |		/* AGS? */
			KA48_PARCTL_NPEN |	/* N? Parity Enable */
			KA48_PARCTL_CPEN);	/* Cpu parity enable */
}

static void ka48_pre_vm_init(void)
{
	/*
	 * Resetting the cache involves disabling it, then clear it and enable
	 * again.
	 */
	ka48_cache_disable();
	ka48_cache_clear();
	ka48_cache_enable();
	__mtpr(PR_ACCS, 2);	/* Enable floating points */
}

static void ka48_dma_init(void)
{
	int i;
	unsigned int __iomem *base_addr;
	unsigned long int *ka48_dmamap;

	/*
	 * At present we just map all of the GFP_DMA region
	 * this is obviously wasteful...
	 */

	/* Grab a block  of 128kb */
	ka48_dmamap = (unsigned long int *)__get_free_pages(GFP_DMA, 5);
	if (ka48_dmamap == NULL) {
		printk(KERN_ERR "KA48 DMA unable to allocate map\n");
		return;
	}

	/*
	 * Map all 16MB of I/O space to low 16MB of memory (the GFP_DMA
	 * region)
	 */
	base_addr = ioremap(KA48_DMAMAP, 0x4);
	*base_addr = (unsigned int)ka48_dmamap;
	for (i = 0; i < 0x8000; i++)
		ka48_dmamap[i] = 0x80000000 | i;
	iounmap(base_addr);

	return;
}

static void ka48_init_devices(void)
{
	printk("ka48: init_devices\n");

	/* Initialise the DMA area */
	ka48_dma_init();
}

struct vax_mv mv_ka48 = {
	.pre_vm_init = ka48_pre_vm_init,
	.post_vm_init = ka48_post_vm_init,
	.pre_vm_putchar = ka46_48_49_prom_putchar,
	.pre_vm_getchar = ka46_48_49_prom_getchar,
	.post_vm_putchar = dz11_putchar,
	.post_vm_getchar = dz11_getchar,
	.init_devices = ka48_init_devices,
	.cpu_type_str = ka48_cpu_type_str,
	.clock_init = ka4x_clock_init,
};

static struct cpu_match __CPU_MATCH cpumatch_ka48 = {
        .mv = &mv_ka48,
        .sid_mask = VAX_SID_FAMILY_MASK,
        .sid_match = VAX_SOC << VAX_SID_FAMILY_SHIFT,
        .sidex_addr = SOC_SIDEX_ADDR,
        .sidex_mask = SOC_SIDEX_TYPE_MASK,
        .sidex_match = SOC_SIDEX_TYPE_KA48 << SOC_SIDEX_TYPE_SHIFT,
};

static struct platform_device ka48_vsbus_device = {
	.name = "ka4x-vsbus"
};

static int __init ka48_platform_device_init(void)
{
	int retval;

	if (!is_ka48())
		return -ENODEV;

	retval = platform_device_register(&ka48_vsbus_device);
	if (!retval) {
		vsbus_add_fixed_device(&ka48_vsbus_device.dev, "lance", 0x200e0000, 1);
	}

	return retval;
}

arch_initcall(ka48_platform_device_init);


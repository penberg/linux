/*
 * Copyright (C) 2000  Kenn Humborg
 *
 * This file contains generic machine vector handlers for the
 * KA46 CPU in the MARIAH-based VAXstation 4000/60
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

unsigned long int *ka46_dmamap;

static void ka46_post_vm_init(void)
{
#ifdef CONFIG_DZ
	init_dz11_console(0x200A0000, 3);
	dz_serial_console_init();
#endif
}

static const char *ka46_cpu_type_str(void)
{
	return "KA46";
}

static void ka46_cache_disable(void)
{
	*(int *)KA46_CCR &= ~KA46_CCR_SPECIO;	/* Secondary */
	__mtpr(PCSTS_FLUSH, PR_PCSTS);		/* Primary */
	*(int *)KA46_BWF0 &= ~KA46_BWF0_FEN;	/* Invalidate filter */
}

static void ka46_cache_clear(void)
{
	int *tmp, i;

	/* Clear caches */
	tmp = (void *)KA46_INVFLT;	/* Inv filter */
	for (i = 0; i < 32768; i++)
		tmp[i] = 0;

	/* Write valid parity to all primary cache entries */
	for (i = 0; i < 256; i++) {
		__mtpr(i << 3, PR_PCIDX);
		__mtpr(PCTAG_PARITY, PR_PCTAG);
	}

	/* Secondary cache */
	tmp = (void *)KA46_TAGST;
	for (i = 0; i < KA46_TAGSZ * 2; i += 2)
		tmp[i] = 0;
}

static void ka46_cache_enable(void)
{
	/* Enable cache */
	*(int *)KA46_BWF0 |= KA46_BWF0_FEN;	/* Invalidate filter */
	__mtpr(PCSTS_ENABLE, PR_PCSTS);
	*(int *)KA46_CCR = KA46_CCR_SPECIO | KA46_CCR_CENA;
}

static void ka46_pre_vm_init(void)
{
	/* Resetting the cache. */
	ka46_cache_disable();
	ka46_cache_clear();
	ka46_cache_enable();

	__mtpr(PR_ACCS, 2);	/* Enable floating points */
}

static void ka46_dma_init(void)
{
	int i;
	unsigned int __iomem *base_addr;

	/*
	 * At present we just map all of the GFP_DMA region
	 * this is obviously wasteful
	 */

	/* Grab a block of 128kb */
	ka46_dmamap = (unsigned long int *)__get_free_pages(GFP_DMA, 5);
	if (ka46_dmamap == NULL) {
		printk(KERN_ERR "KA46 DMA unable to allocate map\n");
		return;
	}

	/*
	 * Map all 16MB of I/O space to low 16MB of
	 * memory (the GFP_DMA region)
	 */
	base_addr = ioremap(KA46_DMAMAP, 0x4);
	*base_addr = (unsigned int)ka46_dmamap;
	for (i = 0; i < 0x8000; i++)
		ka46_dmamap[i] = 0x80000000 | i;

	iounmap(base_addr);

	return;
}

static void ka46_init_devices(void)
{
	printk("ka46: init_devices\n");

	/* Initialise the DMA area */
	ka46_dma_init();
}

struct vax_mv mv_ka46 = {
	.pre_vm_init = ka46_pre_vm_init,
	.post_vm_init = ka46_post_vm_init,
	.pre_vm_putchar = ka46_48_49_prom_putchar,
	.pre_vm_getchar = ka46_48_49_prom_getchar,
	.post_vm_putchar = dz11_putchar,
	.post_vm_getchar = dz11_getchar,
	.init_devices = ka46_init_devices,
	.cpu_type_str = ka46_cpu_type_str,
	.clock_init = ka4x_clock_init,
};

static struct cpu_match __CPU_MATCH cpumatch_ka46 = {
	.mv = &mv_ka46,
	.sid_mask = VAX_SID_FAMILY_MASK,
	.sid_match = VAX_MARIAH << VAX_SID_FAMILY_SHIFT,
	.sidex_addr = MARIAH_SIDEX_ADDR,
	.sidex_mask = 0x00000000,
	.sidex_match = 0x00000000,
};

static struct platform_device ka46_vsbus_device = {
	.name = "ka4x-vsbus"
};

static struct platform_device ka46_diag_led_device = {
	.name = "diag_led"
};

static int __init ka46_platform_device_init(void)
{
	int retval;

	if (!is_ka46())
		return -ENODEV;

	platform_device_register(&ka46_diag_led_device);

	retval = platform_device_register(&ka46_vsbus_device);
	if (!retval) {
#ifdef CONFIG_VSBUS
		vsbus_add_fixed_device(&ka46_vsbus_device.dev, "lance", 0x200e0000, 1);
		vsbus_add_fixed_device(&ka46_vsbus_device.dev, "dz", 0x200a0000, 4);
#endif
	}

	return retval;
}

arch_initcall(ka46_platform_device_init);


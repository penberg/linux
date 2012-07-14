/*
 * Copyright (C) 2000  Kenn Humborg
 *
 * This file contains generic machine vector handlers for the
 * KA43 CPU in the RIGEL-based VAXstation 3100
 *
 * 2000/04/01 Mattias Nordlund
 *            Fixed the cache initializing, added the functions
 *            ka43_cache_disbale/enable/clear and moved some stuff around.
 * atp jun 2001 - machine check implementation
 * atp Jul 2001 - diagmem remap functions
 */

#include <linux/types.h>	/* For NULL */
#include <linux/kernel.h>	/* For printk */
#include <linux/init.h>
#include <linux/device.h>
#include <linux/config.h>

#include <asm/io.h>
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/vaxcpu.h>
#include <asm/tlbflush.h>
#include <asm/ka43.h>
#include <asm/clock.h>		/* For clock_init routines */
#include <asm/bus/vsbus.h>

/* Internal CPU register space */
static volatile struct ka43_cpu_regs __iomem *cpu_regs;

/*
 * We keep the cache page remaps handy incase we want to reset the cache
 * - see the machine check etc..
 * - perhaps we should bung this in the mv too.
 *
 * atp jun 01
 */
static volatile unsigned int __iomem *ka43_ctag_addr;
static volatile unsigned int __iomem *ka43_creg_addr;

#define MC43_MAX	19

static char *ka43_mctype[MC43_MAX + 1] = {
	"no error (0)",                 /* Code 0: No error */
	"FPA: protocol error",          /* Code 1-5: FPA errors */
	"FPA: illegal opcode",
	"FPA: operand parity error",
	"FPA: unknown status",
	"FPA: result parity error",
	"unused (6)",                   /* Code 6-7: Unused */
	"unused (7)",
	"MMU error (TLB miss)",         /* Code 8-9: MMU errors */
	"MMU error (TLB hit)",
	"HW interrupt at unused IPL",   /* Code 10: Interrupt error */
	"MOVCx impossible state",       /* Code 11-13: Microcode errors */
	"undefined trap code (i-box)",
	"undefined control store address",
	"unused (14)",                  /* Code 14-15: Unused */
	"unused (15)",
	"PC tag or data parity error",  /* Code 16: Cache error */
	"data bus parity error",        /* Code 17: Read error */
	"data bus error (NXM)",         /* Code 18: Write error */
	"undefined data bus state",     /* Code 19: Bus error */
};

static void ka43_cache_disable(volatile unsigned int *creg_addr)
{
	__mtpr(KA43_PCS_REFRESH, PR_PCSTS);	/* Disable primary cache */
	__mtpr(__mfpr(PR_PCSTS), PR_PCSTS);	/* Clear error flags */

	/* Disable secondary cache */
	*creg_addr = *creg_addr & ~KA43_SESR_CENB;

	/* Clear error flags */
	*creg_addr = KA43_SESR_SERR | KA43_SESR_LERR | KA43_SESR_CERR;
}

static void ka43_cache_clear(volatile unsigned int *ctag_addr)
{
	int i;

	for (i = 0; i < 256; i++) {
		__mtpr(i * 8, PR_PCIDX);
		__mtpr(KA43_PCTAG_PARITY, PR_PCTAG);
	}

	__mtpr(KA43_PCS_FLUSH | KA43_PCS_REFRESH, PR_PCSTS);

	for (i = 0; i < KA43_CT2_SIZE / sizeof(*ctag_addr); i++)
		ctag_addr[i] = 0xff;
}

static void ka43_cache_enable(volatile unsigned int *creg_addr)
{
	volatile char *membase = (void *) 0x80000000;	/* Physical 0x00000000 */
	int i, val;

	/* Enable primary cache */
	__mtpr(KA43_PCS_FLUSH | KA43_PCS_REFRESH, PR_PCSTS);  /* Flush */

	/* Enable secondary cache */
	*creg_addr = KA43_SESR_CENB;
	for (i=0; i < 128 * 1024; i++)
		val += membase[i];

	__mtpr(KA43_PCS_ENABLE | KA43_PCS_REFRESH, PR_PCSTS); /* Enable */
}

static void ka43_cache_reset(void)
{
	/*
	 * Resetting the cache involves disabling it, then clear
	 * it and enable again.
	 */
	ka43_cache_disable(ka43_creg_addr);
	ka43_cache_clear(ka43_ctag_addr);
	ka43_cache_enable(ka43_creg_addr);
}

/*
 * Don't call ka43_cache_reset before this function (unlikely).
 */
static void ka43_post_vm_init(void)
{
#ifdef CONFIG_DZ
	init_dz11_console(0x200A0000, 3);
	dz_serial_console_init();
#endif
	cpu_regs = ioremap(KA43_CPU_BASE, KA43_CPU_SIZE);
	ka43_creg_addr = ioremap(KA43_CH2_CREG, 1);
	ka43_ctag_addr = ioremap(KA43_CT2_BASE, KA43_CT2_SIZE);

	/*
	 * Disable parity on DMA and CPU memory accesses. Don't know what the
	 * story is with this, but VMS seems do this, too...
	 */
	cpu_regs->parctl = 0;

	/*
	 * Resetting the cache involves disabling it, then clear it and
	 * enable again.
	 */
	ka43_cache_reset();
}


static const char *ka43_cpu_type_str(void)
{
	return "KA43";
}

/*
 * If this seems very similar to the NetBSD implementation, then
 * it is. After all how many ways can you check a sequence of flags?
 */
static void ka43_mcheck(void *stkframe)
{
	/* Map the frame to the stack */
	struct ka43_mcframe *ka43frame = (struct ka43_mcframe *)stkframe;

	/* Tell us all about it */
	printk("KA43: machine check code %d (= 0x%x)\n", ka43frame->mc43_code,
			ka43frame->mc43_code);
	printk("KA43: reason: %s\n", ka43_mctype[ka43frame->mc43_code & 0xff]);
	printk("KA43: at addr %x, pc %x, psl %x\n", ka43frame->mc43_addr,
			ka43frame->mc43_pc, ka43frame->mc43_psl);

	/* FIXME Check restart and first part done flags */
	if ((ka43frame->mc43_code & KA43_MC_RESTART) ||
			(ka43frame->mc43_psl & KA43_PSL_FPDONE)) {
		printk("KA43: recovering from machine-check.\n");
		ka43_cache_reset();
		return;
	}

	/* Unknown error state, panic/halt the machine */
	printk("KA43: Machine Check - unknown error state - halting\n");
	printk("\nStack dump\n");
	hex_dump((void *)(&stkframe), 256);
	dump_cur_regs(1);
	show_cpu_regs();
	machine_halt();
}

/*
 * Slap the KA43_DIAGMEM bit on an area of S0 memory - used by drivers.
 * size is the size of the region in bytes.
 */
void ka43_diagmem_remap(unsigned long int address, unsigned long int size)
{
	int i;
	pte_t *p = GET_SPTE_VIRT(address);

	/*
	 * The KA43 seems to be nicely fscked up...  All physical memory
	 * is accessible from 0x00000000 up (as normal) and also from
	 * 0x28000000 (KA43_DIAGMEM) in IO space.  In order to reliably
	 * share memory with the LANCE, we _must_ read and write to this
	 * shared memory via the DIAGMEM region.  Maybe this bypasses
	 * caches or something...  If you don't do this you get evil
	 * "memory read parity error" machine checks.
	 */

	/*
	 * You MUST remember to clear the DIAGMEM bits in these PTEs
	 * before giving the pages back to free_pages().
	 */

	printk(KERN_DEBUG "KA43: enabling KA43_DIAGMEM for memory from "
			"0x%8lx to 0x%8lx\n", address, address + size);
	for (i = 0; i < (size >> PAGE_SHIFT); i++, p++) {
		set_pte(p, __pte(pte_val(*p) | (KA43_DIAGMEM >> PAGELET_SHIFT)));
		__flush_tlb_one(address + i * PAGE_SIZE);
	}
}

struct vax_mv mv_ka43 = {
	.post_vm_init = ka43_post_vm_init,
	.pre_vm_putchar = ka4x_prom_putchar,
	.pre_vm_getchar = ka4x_prom_getchar,
	.post_vm_putchar = dz11_putchar,
	.post_vm_getchar = dz11_getchar,
	.mcheck = ka43_mcheck,
	.cpu_type_str = ka43_cpu_type_str,
	.clock_init = ka4x_clock_init,
};

static struct cpu_match __CPU_MATCH cpumatch_ka43 = {
        .mv = &mv_ka43,
        .sid_mask = VAX_SID_FAMILY_MASK,
        .sid_match = VAX_RIGEL << VAX_SID_FAMILY_SHIFT,
        .sidex_addr = RIGEL_SIDEX_ADDR,
        .sidex_mask = 0x00000000, /* Don't care */
        .sidex_match = 0x00000000,
};

static struct platform_device ka43_vsbus_device = {
        .name = "ka4x-vsbus"
};

static struct platform_device ka43_diag_led_device = {
	.name = "diag_led"
};

static int __init ka43_platform_device_init(void)
{
        int retval;

        if (!is_ka43())
                return -ENODEV;

        platform_device_register(&ka43_diag_led_device);

        retval = platform_device_register(&ka43_vsbus_device);
        if (!retval) {
#ifdef CONFIG_VSBUS
                vsbus_add_fixed_device(&ka43_vsbus_device.dev, "lance", 0x200e0000, 5);
                vsbus_add_fixed_device(&ka43_vsbus_device.dev, "dz", 0x200a0000, 6);

		/* Register internal SCSI bus */
		vsbus_add_fixed_device(&ka43_vsbus_device.dev, "vax-5380-int", 0x200c0080, 1);

		/* Register external SCSI bus */
		vsbus_add_fixed_device(&ka43_vsbus_device.dev, "vax-5380-ext", 0x200c0180, 0);
#endif
        }

        return retval;
}

arch_initcall(ka43_platform_device_init);


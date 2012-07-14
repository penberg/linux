/*
 *  linux/arch/vax/kernel/cpu_generic.c
 *
 *  Copyright (C) 2000  Kenn Humborg
 *
 *  This file contains generic machine vector handlers that are
 *  useful for multiple CPUs.  For example, all CPUs that use
 *  MTPR-based console I/O can use putchar_mtpr and getchar_mtpr
 *  from here.
 *
 *  Stuff that is specific to a given CPU can be found in cpu_XXX.c
 */

/*
 * Generic reboot and halt functions are in reboot.c
 * CPUs that need to do special stuff in their halt and reboot functions
 * should point to their own functions in their machine vector,
 * otherwise they can leave NULL in the machine vector slots for these
 * functions
 *
 * atp. This holds for machine check functions too. Leave a NULL if you
 *      just want a halt instruction on receipt of a machine check.
 *      See VARM Chapter 5 for details on machine check frames.
 */


#include <asm/dz11.h>
#include <asm/io.h>	/* For ioremap() */
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/pgtable.h>
#include <asm/system.h>	/* For HALT */
#include <asm/vaxcpu.h>

/* This is the main machine vector pointer */
struct vax_mv *mv;

/************************************************************************/
/* These functions can be used by implementations that do console I/O
   via processor registers PR_TXCS, PR_TXDB, PR_RXCS and PR_RXDB */

void mtpr_putchar(int c)
{
	unsigned char xc;
	int delay = 100;

	xc  = (char) (c & 0xff);
	while ((Xmfpr(PR_TXCS) & PR_TXCS_READY) == 0)
		/* busy wait */;

	Xmtpr(xc, PR_TXDB);

	/* If the char just printed was a \n or \r, wait a short while.
	 * Otherwise a printk() followed by a HALT can cause the
	 * console's halt message to overwrite the text just printed */
	if ((c == '\r') || (c == '\n'))
		while (delay--)
			/* busy wait */;
}

int mtpr_getchar(void)
{
	/* Not yet implemented */
	asm("halt");
	return 0;
}

/************************************************************************/
/* These functions can be used by implementations that do console I/O
   via ROM routines at 0x20040058 and 0x20040044 (KA410, KA42 and KA43
   CPUs). These functions can only be used before VM is enabled. */

void ka46_48_49_prom_putchar(int c)
{
	asm(
	"	movzbl %0, %%r2		# zero-extended byte convert.	\n"
	"	jsb 0x20040068						\n"
	: /* nothing */
	: "g"(c)
	: "r2");
}

int ka46_48_49_prom_getchar(void)
{
	/* Not yet implemented */
	asm("halt");
	return 0;
}

void ka4x_prom_putchar(int c)
{
	asm(
	"	movzbl %0, %%r2		# zero-extended byte convert.	\n"
	"	jsb 0x20040058						\n"
	: /* nothing */
	: "g"(c)
	: "r2");
}

int ka4x_prom_getchar(void)
{
	/* Not yet implemented */
	asm("halt");
	return 0;
}

//#ifdef CONFIG_DZ
/************************************************************************/
/* These functions can be used by implementations that do console I/O
   via a DZ11-compatible chip (KA410, KA42 and KA43 CPUs).  These functions can
   only be used after VM is enabled and the DZ11 registers have been
   mapped by map_dz11_regs(). */


volatile struct dz11_regs __iomem *dz11_addr;

/* This is the serial line on the DZ11 that we should use as the
   console.  Normally it is line 3 */
static unsigned int dz11_line;

/*
 * Stuff a char out of a DZ11-compatible serial chip
 */
void dz11_putchar(int c)
{
	u_int txcs, txdb, done;

	/*
	 * During early startup, there might be a printk() call inside
	 * ioremap(), which will be executed while ioremap() hasn't
	 * finished, so the VM addr isn't yet set...
	 */
	if (!dz11_addr)
		return;

	txdb = txcs = done = 0;
	txdb = (c & DZ11_TDR_DATA_MASK);

	/* Stop all I/O activity by clearing MSE */
	dz11_addr->csr = 0;

	/* Enable transmit the relevant line */
	dz11_addr->tcr = DZ11_TCR_LINEENAB0 << dz11_line;

	/* Set line to 9600,8N1 and enable reception */
	dz11_addr->rbuf_lpr.lpr = DZ11_LPR_RXENAB |
		DZ11_SPEED_9600 | DZ11_CHARLGTH_8 | dz11_line;

	/* Set Master Scan Enable to allow I/O */
	dz11_addr->csr = DZ11_CSR_MSE;

	/* Wait for Transmit Ready, then stuff char into TDR register */
	do {
		txcs = (u_short) dz11_addr->csr;
		if (txcs & DZ11_CSR_TRDY) {
			/* We should really check that this TRDY is for
			 * the correct line, and not one of the other lines */
			dz11_addr->msr_tdr.tdr = (u_short) txdb;
			done = 1;
		}
	} while (!done);

	/* Wait again for Transmit Ready */
	while (((txcs = dz11_addr->csr) & DZ11_CSR_TRDY) == 0)
		/* wait */;
}

int dz11_getchar(void)
{
	/* Not yet implemented */
	asm("halt");
	return 0;
}

void init_dz11_console(unsigned long dz11_phys_addr, unsigned int line)
{
	if (dz11_addr != NULL)
		return;

	dz11_addr = ioremap(dz11_phys_addr, sizeof(*dz11_addr));
	dz11_line = line;
}
//#endif /* CONFIG_DZ */

#ifdef CONFIG_CPU_VXT
volatile int __iomem *vxt2694_addr = NULL;

void
vxt2694_putchar (int c)
{
	/* wait for TxRDY */
	while ((vxt2694_addr[1] & 4) == 0)
		/* spin */;

	/* send the character */
	vxt2694_addr[3] = c & 0xff;
}

int
vxt2694_getchar (void)
{
	HALT;
	return 0;
}

void
init_vxt2694_console (unsigned long phys_addr)
{
	if (vxt2694_addr)
		return;

	vxt2694_addr = ioremap (phys_addr, 256);
	return;
}
#endif /* CONFIG_CPU_VXT */


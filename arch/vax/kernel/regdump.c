/*
 * This file contains functions for dumping register and stack
 * contents.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>

#include <asm/call_std.h>
#include <asm/mtpr.h>
#include <asm/psl.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/uaccess.h>

void show_regs(struct pt_regs *regs)
{
	struct psl_fields *psl;
	unsigned int raw_psl;
	static char *modes = "KESU";

	printk("\n   r0  %08lx   r1  %08lx   r2  %08lx   r3  %08lx\n",
			regs->r0, regs->r1, regs->r2, regs->r3);

	printk("   r4  %08lx   r5  %08lx   r6  %08lx   r7  %08lx\n",
			regs->r4, regs->r5, regs->r6, regs->r7);

	printk("   r8  %08lx   r9  %08lx   r10 %08lx   r11 %08lx\n",
			regs->r8, regs->r9, regs->r10, regs->r11);

	printk("   ap  %08lx   fp  %08lx   sp  %08lx   pc  %08lx\n",
			regs->ap, regs->fp, regs->sp, regs->pc);

	raw_psl = RAW_PSL(regs->psl);
	psl = &regs->psl;

	printk("   psl %08x   ipl %d  mode %c (prev %c)  %s%s%s%s%s%s%s%s%s%s%s%s\n",
			raw_psl, psl->ipl,
			modes[psl->accmode], modes[psl->prevmode],
			psl->cm ? "CM " : "",
			psl->tp ? "TP " : "",
			psl->fpd ? "FPD " : "",
			psl->is	? "IS " : "",
			psl->dv ? "DV " : "",
			psl->fu ? "FU " : "",
			psl->iv ? "IV " : "",
			psl->t ? "T " : "",
			psl->n ? "N " : "",
			psl->z ? "Z " : "",
			psl->v ? "V " : "",
			psl->c ? "C " : "");

	if (raw_psl & PSL_MBZ_MASK) {
		printk("   ***  PSL MBZ fields not zero: %08x  ***\n",
			raw_psl & PSL_MBZ_MASK);
	}
}

void show_cpu_regs(void)
{
	unsigned int p0br = __mfpr(PR_P0BR);
	unsigned int p0lr = __mfpr(PR_P0LR);
	unsigned int p1br = __mfpr(PR_P1BR);
	unsigned int p1lr = __mfpr(PR_P1LR);

	unsigned int sbr = __mfpr(PR_SBR);
	unsigned int slr = __mfpr(PR_SLR);
	unsigned int pcbb = __mfpr(PR_PCBB);
	unsigned int scbb = __mfpr(PR_SCBB);

	unsigned int astlvl = __mfpr(PR_ASTLVL);
	unsigned int sisr = __mfpr(PR_SISR);
	unsigned int mapen = __mfpr(PR_MAPEN);
	unsigned int sid = __mfpr(PR_SID);

	unsigned int isp = __mfpr(PR_ISP);
	unsigned int ksp = __mfpr(PR_KSP);
	unsigned int esp = __mfpr(PR_ESP);
	unsigned int ssp = __mfpr(PR_SSP);
	unsigned int usp = __mfpr(PR_USP);

	printk("\n   p0br %08x     sbr  %08x     astlvl %08x\n",
			p0br, sbr, astlvl);

	printk("   p0lr %08x     slr  %08x     sisr   %08x\n",
			p0lr, slr, sisr);

	printk("   p1br %08x     pcbb %08x     mapen  %08x\n",
			p1br, pcbb, mapen);

	printk("   p1lr %08x     scbb %08x     sid    %08x\n\n",
			p1lr, scbb, sid);

	printk("   isp %08x  ksp %08x  esp %08x  ssp %08x  usp %08x\n",
			isp, ksp, esp, ssp, usp);
}

void hex_dump(void *addr, unsigned int bytes)
{
	unsigned int *p = addr;
	unsigned int i;
	unsigned int x;

	for (i=0; i < bytes / 4; i++) {
		if (i % 4 == 0) {
			printk("  %08lx ", (unsigned long)(p+i));
		}
		if ((unsigned int)(p+i)<PAGE_OFFSET) {
			if (get_user(x, p+i)) {
				printk(" --------");
			} else {
				printk(" %08x", x);
			}
		} else {
			x= *(p+i);
			printk(" %08x", x);
		}
		if (i%4 == 3) {
			printk("\n");
		}
	}
}

void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned long addr;

	if (!stack)
		stack = (unsigned long *) &stack;

	printk("Call Trace:\n");

	while (((long) stack & (THREAD_SIZE-1)) != 0) {
		addr = *stack++;
		if (kernel_text_address (addr)) {
			printk(" [<%08lx>] ", addr);
			print_symbol("%s\n", addr);
		}
	}

	printk("\n");

	return;
}

void dump_stack(void)
{
	unsigned long stack;

	vax_dump_stack (0);
	show_stack (current, &stack);
}

void vax_dump_stack(unsigned int frames)
{
	unsigned int reg_count;
	unsigned int save_mask;

	unsigned int *target_sp;

	struct vax_call_frame *fp;
	struct vax_call_frame *target_fp;
	struct vax_arglist *ap;
	struct vax_arglist *target_ap;

	/* Why doesn't asm("fp") on the declaration work
	   as advertised? */
	asm("movl %%fp, %0" : "=g"(fp) : );
	asm("movl %%ap, %0" : "=g"(ap) : );

	/* First frame we look at is our own */
	target_fp = fp;
	target_ap = ap;

	while (frames--) {
		/* Get the saved AP from the current frame */
		target_ap = target_fp->saved_ap;

		/* Then move up to the next frame */
		target_fp = target_fp->saved_fp;
	}

	/* We need to know how many registers were saved
	   in this call frame */
	save_mask = target_fp->save_mask;

	reg_count = 0;
	while (save_mask) {
		if (save_mask&1) {
			reg_count++;
		}
		save_mask >>= 1;
	}

	/* Skip back over the saved registers */
	target_sp = target_fp->saved_reg + reg_count;

	if (target_fp->calls) {
		/* Skip over argument list on stack */
		target_sp += (target_ap->argc + 1);
	}

	hex_dump(target_sp, 256);
}

void dump_cur_regs(unsigned int frames)
{
	struct vax_call_frame *fp = NULL;
	struct pt_regs regs;
	unsigned int num_saved;
	unsigned int reg;
	unsigned int raw_psl;

	/* Grab the current registers */
	asm ("movq %%r0, %0" : "=g"(regs.r0) : );
	asm ("movq %%r2, %0" : "=g"(regs.r2) : );
	asm ("movq %%r4, %0" : "=g"(regs.r4) : );
	asm ("movq %%r6, %0" : "=g"(regs.r6) : );
	asm ("movq %%r8, %0" : "=g"(regs.r8) : );
	asm ("movq %%r10, %0" : "=g"(regs.r10) : );
	asm ("movq %%ap, %0" : "=g"(regs.ap) : );
	asm ("movq %%sp, %0" : "=g"(regs.sp) : );
	asm ("movpsl %0" : "=g"(regs.psl) : );

	asm("movl %%fp, %0" : "=g"(fp) : );

	/* We always pull saved registers from our
	   own stack frame */
	frames++;
	while (frames--) {

		/* Get the saved PSW bits and mergs them into
		   the PSL */
		raw_psl = RAW_PSL(regs.psl);

		raw_psl &= ~0x7ff0;
		raw_psl |= (fp->psw << 4);

		RAW_PSL(regs.psl) = raw_psl;

		regs.ap = (unsigned int)fp->saved_ap;
		regs.fp = (unsigned int)fp->saved_fp;
		regs.pc = (unsigned int)fp->saved_pc;

		/* Now we need to restore any general registers that
		   were saved in this frame */
		num_saved = 0;
		for (reg=0; reg<12; reg++) {
			if (fp->save_mask & (1<<reg)) {
				/* This depends on pt_regs holding
				   the registers in order */
				((unsigned int *)(&regs))[reg] = fp->saved_reg[num_saved];
				num_saved++;
			}
		}

		/* Then move up to the next frame */
		fp = fp->saved_fp;
	}

	show_regs(&regs);
}

/* Little convenience function -- temporary debugging aid - atp */
void vaxpanic(char *reason)
{
	if (reason)
		printk(KERN_CRIT "panic: %s\n", reason);

	printk(KERN_CRIT "\nStack dump\n");
	hex_dump((void *)__mfpr(PR_KSP), 256);
	show_stack (current, NULL);
	dump_cur_regs(1); /* us and parent */
	show_cpu_regs();
	machine_halt();
}


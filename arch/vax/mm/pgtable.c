/*
 * Handle bits of VAX memory management
 * atp 2000
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>

/* Note the factor of 8 in the length registers */
void set_page_dir(struct task_struct * tsk, pgd_t * pgdir)
{
	/* P0BR and P1BR are virtual addresses */
	tsk->thread.pcb.p0br = (pgdir[0]).br;
	tsk->thread.pcb.p0lr = (pgdir[0]).lr * 8;
	tsk->thread.pcb.p1br = (pgdir[1]).br;
	tsk->thread.pcb.p1lr = (pgdir[1]).lr * 8;

	/*
	 * Now if this is the currently running task, update the registers.
	 * This doesn't sound like a great idea... perhaps setipl(31) would
	 * be a good idea here...
	 */
	if (tsk == current) {
		set_vaxmm_regs(pgdir);
		flush_tlb_all();
	}
}

/* Note no factor of 8 in the length registers */
void set_page_dir_kernel(pgd_t * pgdir)
{
	__mtpr((pgdir[2]).br, PR_SBR);
	__mtpr((pgdir[2]).lr, PR_SLR);
	flush_tlb_all();
}


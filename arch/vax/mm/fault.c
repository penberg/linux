/*
 *  linux/arch/alpha/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Copyright (C) 2001 Kenn Humborg, Andy Phillips, David Airlie
 *       (VAX Porting Team)
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <asm/io.h>

#include <asm/pgtable.h>

#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/reboot.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <asm/hardirq.h>

#include <asm/system.h>
#include <asm/uaccess.h>


/*
 * This routine handles page faults and access violations.  It
 * determines the address, and the problem, and then passes
 * it off to handle_mm_fault().
 *
 * reason:
 * reason == 0 means kernel translation not valid fault in SPT.
 * bit	0 = length violation
 * bit	1 = fault during PPTE reference
 * bit	2 = fault-on-read if 0, fault-on-write if 1
 *
 */

#define REASON_LENGTH  (1<<0)
#define REASON_PPTEREF (1<<1)
#define REASON_WRITE   (1<<2)

#undef VAX_MM_DEBUG
#define VAX_MM_DEBUG_USER_FAULTS

static void
do_page_fault(struct accvio_info *info, struct pt_regs *regs)
{
	unsigned long address = info->addr;
	unsigned int reason = info->reason;
	struct vm_area_struct * vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = NULL;
	const struct exception_table_entry *fixup;

#ifdef VAX_MM_DEBUG
	printk("mmfault: pid %d fault at %8x, pc %8x, psl %8x, reason %8x\n",
			current->pid, info->addr, info->pc, info->psl, info->reason);
	printk("mmfault:p0br %8lx, p0lr %8lx, p1br %8lx, p1lr %8lx\n",
			Xmfpr(PR_P0BR), Xmfpr(PR_P0LR), Xmfpr(PR_P1BR), Xmfpr(PR_P1LR));
#endif
	/*
	 * This check, and the mm != NULL checks later, will be removed
	 * later, once we actually have a 'current' properly defined.
	 */
	if (tsk != NULL)
		mm = tsk->mm;

	/*
	 * If we're in an interrupt context, or have no user context,
	 * we must not take the fault.
	 */
	if (in_interrupt() || !mm)
		goto no_context;

	down_read (&mm->mmap_sem);

	vma = find_vma(mm, address);

	if (!vma)
		goto bad_area;

	if (vma->vm_start <= address)
		goto good_area;

	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;

	if (expand_stack(vma, address))
		goto bad_area;

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
good_area:

	if (reason & REASON_WRITE) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		/* Allow reads even for write-only mappings */
		if (!(vma->vm_flags & (VM_READ | VM_WRITE)))
			goto bad_area;
	}
survive:
        switch (handle_mm_fault(mm, vma, address, reason & REASON_WRITE)) {
		case VM_FAULT_MINOR:
			current->min_flt++;
			break;
		case VM_FAULT_MAJOR:
			current->maj_flt++;
			break;
		case VM_FAULT_SIGBUS:
			goto do_sigbus;
		case VM_FAULT_OOM:
			goto out_of_memory;
		default:
			BUG();
	}

	up_read(&mm->mmap_sem);
	return;

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	up_read(&mm->mmap_sem);

        if (user_mode(regs)) {
#ifdef VAX_MM_DEBUG_USER_FAULTS
		printk(KERN_ALERT "Unable to do USER paging request: "
				"pid %d, virtual address %08lx, "
				"reason mask %08x, PC %08x, PSL %08x\n",
				current->pid, address, reason, info->pc,
				info->psl);
		show_regs(regs);
		show_cpu_regs();
		printk("\nStack dump\n");
		hex_dump((void *) (regs->fp & ~0xf), 512);
		printk(KERN_ALERT "do_page_fault: sending SIGSEGV\n");
#endif
		force_sig(SIGSEGV, current);
		return;
	}

no_context:
	/* Are we prepared to handle this fault as an exception? */
	if ((fixup = search_exception_tables(regs->pc)) != NULL) {
		regs->pc = fixup->fixup;
		return;
	}

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	printk(KERN_ALERT "Unable to handle kernel paging request at "
			"virtual address %08lx, reason mask %08x, "
			"PC %08x, PSL %08x\n",
			address, reason, info->pc, info->psl);
	printk("\nStack dump\n");
	hex_dump((void *) regs->sp, 256);
	show_stack(current, NULL);
	show_regs(regs);
	show_cpu_regs();

        machine_halt();

	/*
	 * We ran out of memory, or some other thing happened to us that made
	 * us unable to handle the page fault gracefully.
	 */
out_of_memory:
	if (current->pid == 1) {
		yield();
		goto survive;
	}
	up_read(&mm->mmap_sem);
	if (user_mode(regs)) {
		printk("VM: killing process %s\n", current->comm);
		do_exit(SIGKILL);
	}
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	force_sig(SIGBUS, current);

	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		goto no_context;
}

/*
 * This is the access violation handler.
 */
void accvio_handler(struct pt_regs *regs, void *excep_info)
{
	struct accvio_info *info = (struct accvio_info *) excep_info;
	static int active;

	/*
	 * This active flag is just a temporary hack to help catch
	 * accvios in the page fault handler.  It will have to
	 * go eventually as it's not SMP safe.
	 */
	if (!active) {
		active = 1;
		do_page_fault(info, regs);
		active = 0;
	} else {
		printk("\nNested access violation: reason mask %02x, "
			"addr %08x, PC %08x, PSL %08x\n",
			info->reason, info->addr, info->pc, info->psl);

		printk("\nStack dump\n");
		hex_dump((void *) regs->sp, 256);
		show_stack(current, NULL);
		show_regs(regs);
		show_cpu_regs();

		machine_halt();
	}
}

/*
 * This is the page fault handler.
 */
void page_fault_handler(struct pt_regs *regs, void *excep_info)
{
	struct accvio_info *info = (struct accvio_info *)excep_info;
	static int active;

	/*
	 * This active flag is just a temporary hack to help catch
	 * accvios in the page fault handler.  It will have to
	 * go eventually as it's not SMP safe.
	 */
	if (!active) {
		/* FIXME: Why is this commented out? */
	/*	active = 1;*/
		do_page_fault(info, regs);
#ifdef VAX_MM_DEBUG
		printk("finished fault\n");
#endif
		active = 0;
	} else {
		printk("\nNested page fault: reason mask %02x, "
			"addr %08x, PC %08x, PSL %08x\n",
			info->reason, info->addr, info->pc, info->psl);

		printk("\nStack dump\n");
		hex_dump((void *) regs->sp, 256);
		show_stack(current, NULL);
		show_regs(regs);
		show_cpu_regs();

		machine_halt();
	}
}


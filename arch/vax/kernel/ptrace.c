/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 * Copyright (C) 2001 David Airlie, VAX Porting Project
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/user.h>
#include <linux/security.h>

#include "interrupt.h"

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define VAX_MAX_NUM_VALS_TO_CHECK 5

extern struct irqvector irqvectors[NR_IRQVECTORS];
/*
 * search out does damn regs.. that variable length exception info is evil I
 * tell you evil.
 */
static struct pt_regs *ptrace_find_vax_regs(struct task_struct *child)
{
	struct pt_regs *regs_ptr;
	unsigned long *chk_excep_addr;
	void *stack_top, *excep_addr;
	int num_execp_params;
	int i;

	//	printk("child sp is %8lX\n", child->thread.pcb.ksp);
	stack_top = child->thread_info + 1;
	stack_top -= 4; /* jump down over PC and PSL which are always there */

	/* hack attack - apologies to anyone who has just eaten
	 * this is the worst code I've written since the code this code
         * is replacing ... - Dave.
	 */

	/* start after the PC/PSL, and look for an exception vector
	   if we move to malloced vectors this is screwed */
	chk_excep_addr = (unsigned long *) *(unsigned long *) stack_top;
	for (i = 0; i<VAX_MAX_NUM_VALS_TO_CHECK; i++) {
		excep_addr = ((unsigned long *) stack_top) - i;
		chk_excep_addr = (unsigned long *) *(unsigned long *) excep_addr;
		if (chk_excep_addr > (unsigned long *) irqvectors
				&& chk_excep_addr < (unsigned long *) (irqvectors + NR_IRQVECTORS))
			break;
	}

	if (i == VAX_MAX_NUM_VALS_TO_CHECK) {
		printk("Cannot find exception handler address\n");
		return NULL;
	}

	num_execp_params = *chk_excep_addr;

	regs_ptr = excep_addr - 4 - sizeof(struct pt_regs);

	return regs_ptr;
}

static int putreg(struct task_struct *child, unsigned long regno,
		unsigned long value)
{
	struct pt_regs *regs_ptr;

	regs_ptr = ptrace_find_vax_regs(child);
	if (!regs_ptr)
		return 0;

	if ((regno >> 2) == PT_SP) {
		child->thread.pcb.usp = value;
		return 0;
	}

	switch (regno >> 2) {
		case 0 ... 16:
			//    retval = *(((unsigned long *)regs_ptr) + (regno>>2));
			*(((unsigned long *) regs_ptr) + (regno >> 2)) = value;
			//    *(unsigned long *)((&child->thread.pcb)+4+(regno>>2))=value;
			break;
		default:
			printk("putreg for %lu failed\n", regno);
			break;
	}

	return 0;
}

static unsigned long getreg(struct task_struct *child, unsigned long regno)
{
	unsigned long retval = ~0UL;
	struct pt_regs *regs_ptr;

	/* Call helper function to get registers for the VAX */
	regs_ptr = ptrace_find_vax_regs(child);
	if (!regs_ptr)
		return 0;

	if ((regno >> 2) == PT_SP) {
		retval = child->thread.pcb.usp;
		return retval;
	}

	switch (regno >> 2) {
		case 0 ... 16:
			retval = *(((unsigned long *) regs_ptr) + (regno >> 2));
			break;
		default:
			printk("getreg for %lu failed\n", regno);
			retval = 0;
			break;
	}
//	printk("getreg for %ld returned %8lX\n", regno>>2, retval);
	return retval;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* make sure the single step bit is not set. */
        /* FIXME:  */
}

asmlinkage long sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int res;
	extern void save_fp(void*);

	lock_kernel();
#if 0
	printk("ptrace(r=%d,pid=%d,addr=%08lx,data=%08lx)\n",
	       (int) request, (int) pid, (unsigned long) addr,
	       (unsigned long) data);
#endif
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED) {
			res = -EPERM;
			goto out;
		res = security_ptrace(current->parent, current);
		if (res)
			goto out;
		}
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		res = 0;
		goto out;
	}
	res = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;

	res = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out;

	if (request == PTRACE_ATTACH) {
		if (child == current)
			goto out_tsk;
		if ((!child->mm->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
		    (current->gid != child->egid) ||
		    (current->gid != child->sgid) ||
		    (current->gid != child->gid) ||
		    (!cap_issubset(child->cap_permitted,
		                  current->cap_permitted)) ||
                    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE))
			goto out_tsk;
		/* the same process cannot be attached many times */
		if (child->ptrace & PT_PTRACED)
			goto out_tsk;
		child->ptrace |= PT_PTRACED;

		write_lock_irq(&tasklist_lock);
		if (child->parent != current) {
			REMOVE_LINKS(child);
			child->parent = current;
			SET_LINKS(child);
		}
		write_unlock_irq(&tasklist_lock);

		send_sig(SIGSTOP, child, 1);
		res = 0;
		goto out_tsk;
	}
	res = -ESRCH;
	if (!(child->ptrace & PT_PTRACED))
		goto out_tsk;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out_tsk;
	}
	if (child->parent != current)
		goto out_tsk;
	switch (request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		res = -EIO;
		if (copied != sizeof(tmp))
			break;
		res = put_user(tmp,(unsigned long *) data);

		goto out;
	}

	/* Read the word at location addr in the USER area.  */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

	        res = -EIO;
		if ((addr & 3) || addr < 0 || addr > sizeof(struct user) - 3)
		  break;

		tmp=0;
		if (addr < 16 * sizeof(unsigned long))
		  tmp = getreg(child, addr);

		res = put_user(tmp, (unsigned long *) data);
		goto out;
	}

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		res = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1)
				== sizeof(data))
			break;
		res = -EIO;
		goto out;

	case PTRACE_POKEUSR: {
	  //		struct pt_regs *regs;
		int res = 0;
		res = -EIO;
		if ((addr & 3) || addr < 0 || addr > sizeof(struct user) - 3)
			break;

		if (addr < 17 * sizeof(long)) {
			res = putreg(child, addr, data);
			break;
		}
		/* We need to be very careful here.  We implicitly
		   want to modify a portion of the task_struct, and we
		   have to be selective about what portions we allow someone
		   to modify. */
		break;
	}

	case PTRACE_SYSCALL: /* Continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* Restart after signal. */
		res = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

		child->exit_code = data;
		wake_up_process(child);
		res = 0;
		break;
	}

	/*
	 * Make the child exit.  Best I can do is send it a sigkill.
	 * perhaps it should be put in the status that it wants to
	 * exit.
	 */
	case PTRACE_KILL:
		res = 0;
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;

	case PTRACE_SINGLESTEP: {
		unsigned long tmp;
		struct psl_fields *psl;

                res = -EIO;
                if ((unsigned long) data > _NSIG)
		        break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		if ((child->ptrace & PT_DTRACE)==0)
		  child->ptrace |= PT_DTRACE;

		tmp = getreg(child, PT_PSL<<2);
		psl = (struct psl_fields *)&tmp;
		psl->t = 1;
		putreg(child, PT_PSL << 2, *(unsigned long *) psl);
		//		printk("tmp is %8lX, psl is now %8lX\n", tmp, *(unsigned long *)psl);

		child->exit_code=data;
		wake_up_process(child);
		res = 0;
		break;
	}

	case PTRACE_DETACH: /* detach a process that was attached. */
		res = ptrace_detach(child, data);
		break;

	default:
		res = -EIO;
		goto out;
	}
out_tsk:
	put_task_struct(child);
out:
	unlock_kernel();

	return res;
}

asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;

	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				? 0x80 : 0));

	/*
	 * This isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}


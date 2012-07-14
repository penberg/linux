/*
 * This file contains the standard functions that the arch-independent
 * kernel expects for process handling and scheduling
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/current.h>
#include <asm/processor.h>
#include <asm/io.h>

#include <asm/mtpr.h>
#include <asm/ptrace.h>

#include <linux/unistd.h>

#include <asm/elf.h>

#undef VAX_PROCESS_DEBUG

void cpu_idle(void)
{
	/* Endless idle loop with no priority at all */

	while (1) {
		/* Although we are an idle CPU, we do not want to
		   get into the scheduler unnecessarily. */
		if (need_resched()) {
			schedule();
		}
	}
}


void default_idle(void)
{
	/* nothing */
}

struct task_struct *__switch_to(struct task_struct* prev, struct task_struct* next)
{
	unsigned long pcbb;	/* physical address of new pcb */

#ifdef VAX_PROCESS_DEBUG
	printk("vax_switch_to: switching %p, pid %d, state %ld -> %p, pid %d, state %ld, pc %08lx\n",
			prev, prev->pid, prev->state, next, next->pid, next->state, next->thread.pcb.pc);
#endif
	/* We should check that __pa((prev)->thread.pcb) == PR_PCBB */

	/* Get phys address of next process pcb */
	pcbb = virt_to_phys(&next->thread.pcb);

	/*
	 * When 'next' starts running, R0 will hold the task pointer
	 * for the process we just switched away from.  This will end
	 * up in R0 at ret_from_fork, for new processes and will be
	 * the return value from this function for existing processes
	 */
	next->thread.pcb.r0 = (unsigned long) prev;

        /* svpctx should deal with writing the stuff into *prev */
	asm(
	"	movpsl -(%%sp)						\n"
	"	pushab 1f						\n"
	"	mtpr %2, %3	# Raise IPL to 31			\n"
	"	svpctx		# Causes switch to interrupt stack	\n"
	"	mtpr %0, %1	# Load pcbb into PR_PCCB		\n"
	"	ldpctx		# Loads registers and switches back to	\n"
	"			# kernel stack. Also leaves PC/PSL of	\n"
	"			# new process on kernel stack for an	\n"
	"			# immediate REI				\n"
	"	rei							\n"
	"1:	ret		# return now before anything munges R0	\n"
	: /* no outputs */
	: "r"(pcbb), "g"(PR_PCBB), "g"(31), "g"(PR_IPL));

	/* Never get to here because of the RET instruction above */
	return NULL;
}

/*
 * This _must_ match the stack layout in effect at ret_from_syscall
 * in entry.S.
 *
 * We do a bit of a hack here.  The handler_PC (i.e. the saved PC
 * value from the JSB in the irqvector structure) normally points
 * to the excep_info_size member of the irqvector.  When we build
 * the fake stack frame for the new thread, we don't have an
 * irqvector available.  So what we do is pretend we have one longword
 * of exception info, we put the value 1 into this longword and we
 * point the handler_PC field at this 'exception info'.
 */

struct new_thread_stack {
	struct pt_regs regs;
	unsigned long saved_r0;		/* Will be overwritten by regs->r0 */
	unsigned long *excep_info_size;	/* Must point to excep_info */
	unsigned long excep_info;	/* Must contain the value 1 */
	unsigned long saved_pc;		/* Will be overwritten by regs->pc */
	struct psl_fields saved_psl;
};

/* Defined in entry.S */
extern void ret_from_fork(void);

int copy_thread(int unused1, unsigned long clone_flags, unsigned long usp,
		unsigned long unused2,
		struct task_struct *p, struct pt_regs *regs)
{
	struct new_thread_stack *child_stack;
	struct pt_regs *child_regs;
	void *stack_top;

	stack_top = (unsigned char *)(p->thread_info) + THREAD_SIZE;
	stack_top -= 4;

	child_stack = (struct new_thread_stack *)(stack_top) - 1;

#ifdef VAX_PROCESS_DEBUG
	printk("copy_thread: pid %d, task 0x%08lx, kstack_top %p, "
			"usp 0x%08lx, ksp %p\n", p->pid, (unsigned long) p,
			stack_top, usp, child_stack);
#endif

	child_regs = &child_stack->regs;

	*child_regs = *regs;
	child_regs->r0 = 0;	/* fork() returns 0 in child */

	child_stack->excep_info = 1;
	child_stack->excep_info_size = &child_stack->excep_info;
	child_stack->saved_psl = regs->psl;

	p->thread.pcb.ksp = (unsigned long)child_stack;
	p->thread.pcb.usp = usp;
	p->thread.pcb.pc = (unsigned long)ret_from_fork;
	p->thread.pcb.psl = __psl;

	/*
	 * New thread must start with IPL 31 to prevent any interrupts
	 * from occuring between the time it is first scheduled (in __switch_to
	 * above) and when ret_from_fork calls schedule_tail().  If an
	 * interrupt comes in during this time, schedule() might get called
	 * from do_irq_excep() before schedule_tail() has released the
	 * runqueue lock (in finish_task_switch)
	 */
	p->thread.pcb.psl.ipl = 31;

	/*
	 * We could speed this up by loading the register values into
	 * the PCB and start the new thread just before the REI in
	 * entry.S, letting the regular context switching load the
	 * registers from the PCB.  However, once signal and bottom-half
	 * handling go into the ret_from_syscall path, then things might
	 * change.  So I'll stick with this 'obviously correct' method
	 * for now.  KPH 2000-10-30
	 */

	return 0;
}

void flush_thread(void)
{
	/*
	 * I don't really know what's supposed to go in here.  It
	 * gets called just after exec(), so I guess we reset any
	 * VAX-specific thread state here
	 */
}

/*
 * Create a kernel thread
 */
pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	asm(
	"	movl %2,%%r2		\n"
	"	movl %3,%%r3		\n"
	"	clrl -(%%sp)		\n"
	"	movl %0, -(%%sp)	\n"
	"	pushl $0x2		\n"
	"	movl %%sp, %%ap		\n"
	"	chmk %1			\n"
	"	tstl %%r0		\n"
	"	beql child		\n"
	"	ret			\n"
	"child:				\n"
	"	pushl %%r3		\n"
	"	calls $1, *%%r2		\n"
	"	pushl %%r0		\n"
	"	movl %%sp, %%ap		\n"
	"	chmk %4			\n"
	: /* no outputs */
	: "g"(flags | CLONE_VM), "g"(__NR_clone), "g"(fn), "g"(arg), "g"(__NR_exit)
	: "r0", "r2", "r3");

	/*
	 * We never actually get here - there is a RET embedded above which
	 * returns in the parent, and the child exits with the CHMK __NR_exit
	 */
	return 0;
}

int sys_clone(unsigned long clone_flags, unsigned long newsp, struct pt_regs *regs)
{
	int retval;

	if (!newsp)
		newsp = regs->sp;

#ifdef VAX_PROCESS_DEBUG
	printk("sys_clone: calling do_fork(0x%08lx, 0x%08lx, 0x%p)\n",
			clone_flags, newsp, regs);
#endif
	retval = do_fork(clone_flags, newsp, regs, 0, NULL, NULL);

#ifdef VAX_PROCESS_DEBUG
	printk("sys_clone: do_fork() returned pid %d\n", retval);
#endif
	return retval;
}

int sys_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->sp, regs, 0, NULL, NULL);
}

int sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->sp, regs, 0, NULL, NULL);
}

/*
 * sys_execve() executes a new program.
 *
 */
int sys_execve(char *filename, char **argv, char **envp,
	struct pt_regs *regs)
{
	int error;
	char *tmpname;

	tmpname = getname(filename);
	error = PTR_ERR(tmpname);
	if (IS_ERR(tmpname))
		goto out;

	error = do_execve(tmpname, argv, envp, regs);
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(tmpname);
out:
	return error;
}

int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
	/* no FPU support .. YET - D.A. 25 Feb 2001 */
	return 0;
}

void start_thread(struct pt_regs *regs, unsigned long new_pc,
		unsigned long new_sp)
{
#ifdef VAX_PROCESS_DEBUG
	printk("PID %d: starting thread pc=0x%08lx new_sp=0x%08lx regs->sp="
			"0x%08lx\n", current->pid, new_pc, new_sp, regs->sp);
#endif
	set_fs(USER_DS);
	regs->pc = new_pc + 2;
	regs->sp = new_sp;
	regs->ap = new_sp;
	regs->fp = new_sp;
	regs->psl.prevmode = PSL_MODE_USER;
	regs->psl.accmode = PSL_MODE_USER;
	/* write the sp into the user stack pointer register */
	__mtpr(new_sp, PR_USP);
}


/*
 *  linux/arch/vax/kernel/signal.c
 *
 *  From arch/cris/kernel/signal.c
 *
 *  Based on arch/i386/kernel/signal.c by
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *     1997-11-28  Modified for POSIX.1b signals by Richard Henderson *
 *
 *  Ideas also taken from arch/arm.
 *
 *  Copyright (C) 2000 Axis Communications AB
 *
 *  Authors:  Bjorn Wesen (bjornw@axis.com)
 *            VAX port atp@pergamentum.com.
 *                     + David Airlie Copyright (C) 2003
 *	      See syscall.c for details of the call stack layout etc...
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/tty.h>
#include <linux/binfmts.h>

#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>

#undef DEBUG_SIG

/* FIXME: Check this & fixup other regs, like r0 */
#define RESTART_VAX_SYSCALL(regs) { (regs)->pc -= 4; }


#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

int do_signal(sigset_t *oldset, struct pt_regs *regs);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
int
sys_sigsuspend(struct pt_regs *regs, old_sigset_t mask)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->r0 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return -EINTR;
	}
}

/*
 * atp - it is a little confusing, looking at other ports, as to what the arguments to
 * this function are. I'm assuming two args, plus our pushed pt_regs set up by syscall
 */
int
sys_rt_sigsuspend(struct pt_regs *regs,sigset_t *unewset, size_t sigsetsize)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->r0 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return -EINTR;
	}
}

int
sys_sigaction(int sig, const struct old_sigaction *act,
		struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
				__get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
				__get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
				__put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
				__put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

int
sys_sigaltstack(const stack_t *uss, stack_t *uoss)
{
	struct pt_regs *regs = (struct pt_regs *) &uss;

	return do_sigaltstack(uss, uoss, regs->sp);
}


/*
 * Do a signal return; undo the signal stack.
 */
struct sigframe {
	int sig;
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS-1];
	unsigned char retcode[20];  /* trampoline code */
};

struct rt_sigframe {
	int sig;
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	unsigned char retcode[20];  /* trampoline code */
};

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *sc)
{
	unsigned int err = 0;

	/*
	 * Restore the regs from &sc->regs (same as sc, since regs is first)
	 * (sc is already checked for VERIFY_READ since the sigframe was
	 * checked in sys_sigreturn previously).
	 */

	if (__copy_from_user(regs, sc, sizeof(struct pt_regs)))
		goto badframe;

	/* FIXME: check user mode flag in restored regs PSW */

	/*
	 * Restore the old USP as it was before we stacked the sc etc.
	 * (we cannot just pop the sigcontext since we aligned the sp and
	 * stuff after pushing it).
	 */

	/* FIXME: check process stack */

	/*
	 * TODO: the other ports use regs->orig_XX to disable syscall checks
	 * after this completes, but we don't use that mechanism. maybe we can
	 * use it now ?
	 */

	return err;

badframe:
	return 1;
}


asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigframe *frame = (struct sigframe *) (regs->sp);
	sigset_t set;

        /*
         * Since we stacked the signal on a dword boundary,
         * then frame should be dword aligned here. If it's
         * not, then the user is trying to mess with us.
         */
        if (((long) frame) & 3)
                goto badframe;

        if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask)
			|| (_NSIG_WORDS > 1
				&& __copy_from_user(&set.sig[1], &frame->extramask,
					sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->sc))
		goto badframe;

	/* TODO: SIGTRAP when single-stepping as in arm ? */

	return regs->r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}


asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe *frame = (struct rt_sigframe *) (regs->sp-8);
	sigset_t set;
	stack_t st;

        /*
         * Since we stacked the signal on a dword boundary,
         * then frame should be dword aligned here. If it's
         * not, then the user is trying to mess with us.
         */
        if (((long) frame) & 3)
                goto badframe;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
		goto badframe;
	/*
	 * It is more difficult to avoid calling this function than to
	 * call it and ignore errors.
	 */
	do_sigaltstack(&st, NULL, (regs->sp));

	return regs->r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Set up a signal frame.
 */
static int
setup_sigcontext(struct sigcontext *sc, struct pt_regs *regs, unsigned long mask)
{
	int err = 0;

	/* copy the regs. they are first in sc so we can use sc directly */

	err |= __copy_to_user(sc, regs, sizeof(struct pt_regs));

	/* then some other stuff */

	err |= __put_user(mask, &sc->oldmask);

	return err;
}

/* figure out where we want to put the new signal frame - usually on the stack */

static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size)
{
	unsigned long sp = regs->sp;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (! on_sig_stack(sp))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	/* make sure the frame is dword-aligned */

	sp &= ~3;

	return (void *)(sp - frame_size);
}

/* Grab and setup a signal frame.
 *
 * Basically we stack a lot of state info, and arrange for the
 * user-mode program to return to the kernel using either a
 * trampoline which performs the syscall sigreturn, or a provided
 * user-mode trampoline.
 */
static void setup_frame(int sig, struct k_sigaction *ka,
		sigset_t *set, struct pt_regs *regs)
{
	struct sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

#ifdef DEBUG_SIG
	printk("setup_frame: pid %d, sig %d, regs %p, regs->sp %p, frame %p, sigaction %p\n",current->pid,sig,regs,regs->sp,frame,ka);
        show_regs(regs);
#endif
	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

        /* write the signal onto the stack */
        err |= __put_user(sig, (unsigned int *) &frame->sig);
	err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);
	if (err)
		goto give_sigsegv;

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}
	if (err)
		goto give_sigsegv;

	/*
	 * Set up to return from userspace.  If provided, use a stub
	 * already in userspace.
	 *
	 * We do this differently to other ports. Each function has a two
	 * byte RSM (due to the calling convention).  Each sighandler will
	 * expect to be CALLS'd and will RET from that. So we cant just muck
	 * about with PC's on the stack like the i386. So we use the
	 * trampoline code on the stack a bit more. The easiest way to skip
	 * around all this is to calls the signal handler, and then either
	 * calls the restorer, or chmk to sys_sigreturn.
	 */

	/*	 CALLS $1, */
	err |= __put_user(0xfb, (char *) (frame->retcode + 0));
	err |= __put_user(0x01, (char *) (frame->retcode + 1));
	/*             (absolute address)*/
	err |= __put_user(0x9f, (char *) (frame->retcode + 2));
	/*				     sighandler */
	err |= __put_user(((unsigned long) ka->sa.sa_handler),
			(unsigned long *) (frame->retcode + 3));

	if (ka->sa.sa_flags & SA_RESTORER) {
		/*  CALLS $0,*/
		err |= __put_user(0xfb, (char *) (frame->retcode + 7));
		err |= __put_user(0x00, (char *) (frame->retcode + 8));
		/*             (absolute address)*/
		err |= __put_user(0x9f, (char  *) (frame->retcode + 9));
		/*				     restorer */
		err |= __put_user(((unsigned long) ka->sa.sa_restorer),
				(unsigned long *) (frame->retcode + 10));
		/* plus a halt */
		err |= __put_user(0x00, (char *) (frame->retcode + 14));
	} else {
		/*
		 * Perform a syscall to sys_sigreturn. First set up the
		 * argument list to avoid confusing it.
		 */

		/* pushl $0x0 */
		err |= __put_user(0xdd,		  (char *) (frame->retcode + 7));
		err |= __put_user(0x00,		  (char *) (frame->retcode + 8));
	        /* movl sp, ap */
		err |= __put_user(0xd0,		  (char *) (frame->retcode + 9));
		err |= __put_user(0x5e,		  (char *) (frame->retcode + 10));
		err |= __put_user(0x5c,		  (char *) (frame->retcode + 11));
		/* chmk __NR_sigreturn; */
		err |= __put_user(0xbc,		  (char *) (frame->retcode + 12));
		err |= __put_user(0x8f,		  (char *) (frame->retcode + 13));
		err |= __put_user(__NR_sigreturn, (short *) (frame->retcode + 14));
		/* plus a halt */
	        err |= __put_user(0x00,   (char *)(frame->retcode+16));
	}

	if (err)
		goto give_sigsegv;

#ifdef DEBUG_SIG
	printk("setup_frame: pid %d, frame->retcode %p, sa_handler %p\n",
			current->pid,
			frame->retcode,
			ka->sa.sa_handler);
#endif
	/* Set up registers for signal handler. */
	regs->pc = (unsigned long) frame->retcode;  /* What we enter NOW. */
	regs->fp = regs->sp;
	regs->sp = (unsigned int) frame;
	__mtpr(frame,PR_USP); /* and into to the register, ready for REI */

#ifdef DEBUG_SIG
	printk("setup_frame: pid %d, regs->pc %8lx, regs->sp %8lx, regs->ap %8lx\n",
			current->pid,
			regs->pc,
			regs->sp,
			regs->ap);
	{
		unsigned char c[4];
		__get_user(c[0], (char *) &frame->sig + 0);
		__get_user(c[1], (char *) &frame->sig + 1);
		__get_user(c[2], (char *) &frame->sig + 2);
		__get_user(c[3], (char *) &frame->sig + 3);
		printk("setup_frame: %p %1x %p %1x %p %1x %p %1x\n",
				&frame->sig + 0, c[0],
				&frame->sig + 1, c[1],
				&frame->sig + 2, c[2],
				&frame->sig + 3, c[3]);
	}
	{
		unsigned char c[4];
		__get_user(c[0], (char *) frame->retcode + 0);
		__get_user(c[1], (char *) frame->retcode + 1);
		__get_user(c[2], (char *) frame->retcode + 2);
		__get_user(c[3], (char *) frame->retcode + 3);
		printk("setup_frame: %p %1x %p %1x %p %1x %p %1x\n",
				frame->retcode + 0, c[0],
				frame->retcode + 1, c[1],
				frame->retcode + 2, c[2],
				frame->retcode + 3, c[3]);
	}
	{
		unsigned char c[4];
		__get_user(c[0], (char *) frame->retcode + 4);
		__get_user(c[1], (char *) frame->retcode + 5);
		__get_user(c[2], (char *) frame->retcode + 6);
		__get_user(c[3], (char *) frame->retcode + 7);
		printk("setup_frame: %p %1x %p %1x %p %1x %p %1x\n",
				frame->retcode + 4, c[0],
				frame->retcode + 5, c[1],
				frame->retcode + 6, c[2],
				frame->retcode + 7, c[3]);
	}
	{
		unsigned char c[4];
		__get_user(c[0], (char *) frame->retcode + 8);
		__get_user(c[1], (char *) frame->retcode + 9);
		__get_user(c[2], (char *) frame->retcode + 10);
		__get_user(c[3], (char *) frame->retcode + 11);
		printk("setup_frame: %p %1x %p %1x %p %1x %p %1x\n",
				frame->retcode + 8, c[0],
				frame->retcode + 9, c[1],
				frame->retcode + 10, c[2],
				frame->retcode + 11, c[3]);
	}
	{
		unsigned char c[4];
		__get_user(c[0], (char *) frame->retcode + 12);
		__get_user(c[1], (char *) frame->retcode + 13);
		__get_user(c[2], (char *) frame->retcode + 14);
		__get_user(c[3], (char *) frame->retcode + 15);
		printk("setup_frame: %p %1x %p %1x %p %1x %p %1x\n",
				frame->retcode + 12, c[0],
				frame->retcode + 13, c[1],
				frame->retcode + 14, c[2],
				frame->retcode + 15, c[3]);
	}
#endif
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
		sigset_t *set, struct pt_regs * regs)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

#ifdef DEBUG_SIG
	printk("setup_rt_frame: pid %d, sig %d, regs %p, regs->sp %p, frame %p, sigaction %p\n",current->pid,sig,regs,regs->sp,frame,ka);
	show_regs(regs);
#endif
	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	err |= __put_user(sig, (unsigned int *)&frame->sig);
	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);

	err |= copy_siginfo_to_user(&frame->info, info);
	if (err)
		goto give_sigsegv;

	/* Clear all the bits of the ucontext we don't use.  */
        err |= __clear_user(&frame->uc, offsetof(struct ucontext, uc_mcontext));
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	/*
	 * Set up to return from userspace.  If provided, use a stub
	 * already in userspace.
	 */

        /*
	 * We do this differently to other ports. Each function has a two byte RSM.
	 * (due to the calling convention).  Each sighandler will expect to be
	 * CALLS'd and will RET from that. So we cant just muck about with PC's on the
	 * stack like the i386. So we use the trampoline code on the stack a bit more.
	 * The easiest way to skip around all this is to calls the signal
	 * handler, and then either calls the restorer, or chmk to sys_sigreturn.
	 */

	/* CALLS $3, */
	err |= __put_user(0xfb, (char *) (frame->retcode + 0));
	err |= __put_user(0x03, (char *) (frame->retcode + 1));
	/*             (absolute address)*/
	err |= __put_user(0x9f, (char *) (frame->retcode + 2));
	/*				     sighandler */
	err |= __put_user(((unsigned long) ka->sa.sa_handler),
			(unsigned long *) (frame->retcode + 3));

	if (ka->sa.sa_flags & SA_RESTORER) {
		/*  CALLS $0,*/
		err |= __put_user(0xfb, (char *) (frame->retcode + 7));
		err |= __put_user(0x00, (char *) (frame->retcode + 8));
		/*             (absolute address)*/
		err |= __put_user(0x9f, (char  *) (frame->retcode + 9));
		/*				     restorer */
		err |= __put_user(((unsigned long) ka->sa.sa_restorer),
				(unsigned long *) (frame->retcode + 10));
		/* plus a halt */
		err |= __put_user(0x00, (char *) (frame->retcode + 14));
	} else {
		/*
		 * Perform a syscall to sys_sigreturn. First set up the
		 * argument list to avoid confusing it.
		 */

		/* pushl $0x0 */
		err |= __put_user(0xdd,		     (char *) (frame->retcode + 7));
		err |= __put_user(0x00,		     (char *) (frame->retcode + 8));
	        /* movl sp, ap */
		err |= __put_user(0xd0,		     (char *) (frame->retcode + 9));
		err |= __put_user(0x5e,		     (char *) (frame->retcode + 10));
		err |= __put_user(0x5c,		     (char *) (frame->retcode + 11));
		/* chmk __NR_sigreturn; */
		err |= __put_user(0xbc,		     (char *) (frame->retcode + 12));
		err |= __put_user(0x8f,		     (char *) (frame->retcode + 13));
		err |= __put_user(__NR_rt_sigreturn, (short *) (frame->retcode + 14));
		/* plus a halt */
	        err |= __put_user(0x00,		     (char *) (frame->retcode + 16));
	}

	if (err)
		goto give_sigsegv;

	/* TODO what is the current->exec_domain stuff and invmap ? */

#ifdef DEBUG_SIG
	printk("setup_rt_frame: pid %d, frame->retcode %p, sa_handler %p usp %8lX\n",
			current->pid,
			frame->retcode,
			ka->sa.sa_handler,
			__mfpr(PR_USP));
#endif	/* Set up registers for signal handler */

	regs->pc = (unsigned long) frame->retcode;  /* what we enter NOW   */
	regs->fp = regs->sp;
	regs->sp = (unsigned int)frame;             /* what we enter LATER */
	__mtpr(frame, PR_USP);

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

/*
 * OK, we're invoking a handler.
 */
static inline void
handle_signal(int canrestart, unsigned long sig, struct k_sigaction *ka,
		siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
	/* Are we from a system call? */
	if (canrestart) {
		/* If so, check system call restarting.. */
		switch (regs->r0) {
			case -ERESTART_RESTARTBLOCK:
				current_thread_info()->restart_block.fn = do_no_restart_syscall;
				/* fallthrough */

			case -ERESTARTNOHAND:
				/* ERESTARTNOHAND means that the syscall should only be
				   restarted if there was no handler for the signal, and since
				   we only get here if there is a handler, we dont restart */
				regs->r0 = -EINTR;
				break;

			case -ERESTARTSYS:
				/* ERESTARTSYS means to restart the syscall if there is no
				   handler or the handler was registered with SA_RESTART */
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->r0 = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				/* ERESTARTNOINTR means that the syscall should be called again
				   after the signal handler returns. */
				RESTART_VAX_SYSCALL(regs);
		}
	}

	/* Set up the stack frame */
#ifdef DEBUG_SIG
	printk("handle_signal: setup_frame(sig=%d,flags=%d,ka=%p,oldset=%d,regs=%p)\n",sig,ka->sa.sa_flags,ka,oldset,regs);
#endif
        if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	siginfo_t info;
	int signr;
        int canrestart;
	struct k_sigaction ka;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

        /* FIXME: */
	canrestart=regs->r0;
#ifdef DEBUG_SIG
	printk("do_signal: pid %d,canrestart %d, current->sigpending %d,current->blocked %d ", current->pid,canrestart,current->sigpending,current->blocked);
#endif
	if (!oldset)
		oldset = &current->blocked;


	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(canrestart, signr, &ka, &info, oldset, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if (canrestart) {
		/* Restart the system call - no handlers present */
		if (regs->r0 == -ERESTARTNOHAND
				|| regs->r0 == -ERESTARTSYS
				|| regs->r0 == -ERESTARTNOINTR) {
			RESTART_VAX_SYSCALL(regs);
		}
	}

	return 0;
}


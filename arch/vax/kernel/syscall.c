/*
 * This file handles syscalls.
 */

#include <linux/sys.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/utsname.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>
#include <asm/ipc.h>
#include <asm/thread_info.h>

#include "interrupt.h"  /* Local, private definitions */

/* ./arch/vax/kernel/syscall.c */
extern int sys_pipe(unsigned long *fildes);
extern unsigned long sys_mmap(unsigned long addr, size_t len, int prot,
		int flags, int fd, off_t offset);
extern int sys_ipc (uint call, int first, int second, int third, void *ptr,
		long fifth);
extern asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
		unsigned long prot, unsigned long flags, unsigned long fd,
		unsigned long pgoff);
/* ./arch/vax/kernel/process.c */
extern int sys_fork(struct pt_regs regs);
extern int sys_vfork(struct pt_regs *regs);
extern int sys_execve(char *filename, char **argv, char **envp,
		struct pt_regs *regs);
extern int sys_clone(unsigned long clone_flags, unsigned long newsp,
		struct pt_regs *regs);
/* ./arch/vax/kernel/ptrace.c */
extern asmlinkage long sys_ptrace(long request, long pid, long addr, long data);
/* ./arch/vax/kernel/signal.c */
extern int sys_sigaction(int sig, const struct old_sigaction *act,
		struct old_sigaction *oact);
extern int sys_sigsuspend(struct pt_regs *regs, old_sigset_t mask);
extern asmlinkage int sys_sigreturn(struct pt_regs *regs);
extern asmlinkage int sys_rt_sigreturn(struct pt_regs *regs);
extern int sys_rt_sigsuspend(struct pt_regs *regs,sigset_t *unewset,
		size_t sigsetsize);
extern int sys_sigaltstack(const stack_t *uss, stack_t *uoss);
/* ./kernel/signal.c */
extern asmlinkage long sys_rt_sigaction(int sig,
		const struct sigaction __user *act,
		struct sigaction __user *oact, size_t sigsetsize);


static struct {
	unsigned long	*sc_func;
	unsigned int	nr_args;
} syscall[] = {
#define SC(num, func, args)					\
	[num] = {						\
		.sc_func	= (unsigned long *) &func,	\
		.nr_args	= args,				\
	}
	SC (__NR_exit,		sys_exit,		1),
	SC (__NR_fork,		sys_fork,		0),
	SC (__NR_read,		sys_read,		3),
	SC (__NR_write,		sys_write,		3),
	SC (__NR_open,		sys_open,		3),
	SC (__NR_close,		sys_close,		1),
	SC (__NR_waitpid,	sys_waitpid,		3),
	SC (__NR_creat,		sys_creat,		2),
	SC (__NR_link,		sys_link,		2),
	SC (__NR_unlink,	sys_unlink,		1),
	SC (__NR_execve,	sys_execve,		3),
	SC (__NR_chdir,		sys_chdir,		1),
	SC (__NR_time,		sys_time,		1),
	SC (__NR_mknod,		sys_mknod,		3),
	SC (__NR_chmod,		sys_chmod,		2),
	SC (__NR_lchown,	sys_lchown16,		3),
	SC (__NR_lseek,		sys_lseek,		3),
	SC (__NR_getpid,	sys_getpid,		0),
	SC (__NR_mount,		sys_mount,		5),
	SC (__NR_umount,	sys_oldumount,		2),
	SC (__NR_setuid,	sys_setuid16,		1),
	SC (__NR_getuid,	sys_getuid16,		0),
	SC (__NR_stime,		sys_stime,		1),
	SC (__NR_ptrace,	sys_ptrace,		4),
	SC (__NR_alarm,		sys_alarm,		1),
	SC (__NR_pause,		sys_pause,		0),
	SC (__NR_utime,		sys_utime,		2),
	SC (__NR_access,	sys_access,		2),
	SC (__NR_nice,		sys_nice,		1),
	SC (__NR_sync,		sys_sync,		0),
	SC (__NR_kill,		sys_kill,		2),
	SC (__NR_rename,	sys_rename,		2),
	SC (__NR_mkdir,		sys_mkdir,		2),
	SC (__NR_mkdir,		sys_mkdir,		2),
	SC (__NR_rmdir,		sys_rmdir,		1),
	SC (__NR_dup,		sys_dup,		1),
	SC (__NR_pipe,		sys_pipe,		1),
	SC (__NR_times,		sys_times,		1),
	SC (__NR_brk,		sys_brk,		1),
	SC (__NR_setgid,	sys_setgid16,		1),
	SC (__NR_getgid,	sys_getgid16,		0),
	SC (__NR_signal,	sys_signal,		2),
	SC (__NR_geteuid,	sys_geteuid16,		0),
	SC (__NR_getegid,	sys_getegid16,		0),
	SC (__NR_acct,		sys_acct,		1),
	SC (__NR_umount2,	sys_oldumount,		2),
	SC (__NR_ioctl,		sys_ioctl,		3),
	SC (__NR_fcntl,		sys_fcntl,		3),
	SC (__NR_setpgid,	sys_setpgid,		2),
	SC (__NR_umask,		sys_umask,		1),
	SC (__NR_chroot,	sys_chroot,		1),
	SC (__NR_ustat,		sys_ustat,		2),
	SC (__NR_dup2,		sys_dup2,		2),
	SC (__NR_getppid,	sys_getppid,		0),
	SC (__NR_getpgrp,	sys_getpgrp,		0),
	SC (__NR_setsid,	sys_setsid,		0),
	SC (__NR_sigaction,	sys_sigaction,		3),
	SC (__NR_sgetmask,	sys_sgetmask,		0),
	SC (__NR_ssetmask,	sys_ssetmask,		1),
	SC (__NR_setreuid,	sys_setreuid16,		2),
	SC (__NR_setregid,	sys_setregid16,		2),
	SC (__NR_sigsuspend,	sys_sigsuspend,		1),
	SC (__NR_sigpending,	sys_sigpending,		1),
	SC (__NR_sethostname,	sys_sethostname,	2),
	SC (__NR_setrlimit,	sys_setrlimit,		2),
	SC (__NR_old_getrlimit,	sys_old_getrlimit,	2),
	SC (__NR_getrusage,	sys_getrusage,		2),
	SC (__NR_gettimeofday,	sys_gettimeofday,	2),
	SC (__NR_settimeofday,	sys_settimeofday,	2),
	SC (__NR_getgroups,	sys_getgroups16,	2),
	SC (__NR_setgroups,	sys_setgroups16,	2),
	SC (__NR_symlink,	sys_symlink,		2),
	SC (__NR_readlink,	sys_readlink,		3),
	SC (__NR_uselib,	sys_uselib,		1),
	SC (__NR_swapon,	sys_swapon,		2),
	SC (__NR_reboot,	sys_reboot,		4),
	SC (__NR_mmap,		sys_mmap,		6),
	SC (__NR_munmap,	sys_munmap,		2),
	SC (__NR_truncate,	sys_truncate,		2),
	SC (__NR_ftruncate,	sys_ftruncate,		2),
	SC (__NR_fchmod,	sys_fchmod,		2),
	SC (__NR_fchown,	sys_fchown16,		3),
	SC (__NR_getpriority,	sys_getpriority,	2),
	SC (__NR_setpriority,	sys_setpriority,	3),
	SC (__NR_statfs,	sys_statfs,		2),
	SC (__NR_fstatfs,	sys_fstatfs,		2),
	SC (__NR_socketcall,	sys_socketcall,		2),
	SC (__NR_syslog,	sys_syslog,		3),
	SC (__NR_setitimer,	sys_setitimer,		3),
	SC (__NR_getitimer,	sys_getitimer,		2),
	SC (__NR_stat,		sys_newstat,		2),
	SC (__NR_lstat,		sys_newlstat,		2),
	SC (__NR_fstat,		sys_newfstat,		2),
	SC (__NR_vhangup,	sys_vhangup,		0),
	SC (__NR_wait4,		sys_wait4,		4),
	SC (__NR_swapoff,	sys_swapoff,		2),
	SC (__NR_sysinfo,	sys_sysinfo,		1),
	SC (__NR_ipc,		sys_ipc,		6),
	SC (__NR_fsync,		sys_fsync,		1),
	SC (__NR_sigreturn,	sys_sigreturn,		0),
	SC (__NR_clone,		sys_clone,		2),
	SC (__NR_setdomainname,	sys_setdomainname,	2),
	SC (__NR_uname,		sys_newuname,		1),
	SC (__NR_adjtimex,	sys_adjtimex,		1),
	SC (__NR_mprotect,	sys_mprotect,		3),
	SC (__NR_sigprocmask,	sys_sigprocmask,	3),
	SC (__NR_init_module,	sys_init_module,	5),
	SC (__NR_delete_module,	sys_delete_module,	3),
	SC (__NR_quotactl,	sys_quotactl,		4),
	SC (__NR_getpgid,	sys_getpgid,		1),
	SC (__NR_fchdir,	sys_fchdir,		1),
	SC (__NR_bdflush,	sys_bdflush,		2),
	SC (__NR_sysfs,		sys_sysfs,		3),
	SC (__NR_personality,	sys_personality,	1),
	SC (__NR_setfsuid,	sys_setfsuid16,		1),
	SC (__NR_setfsgid,	sys_setfsgid16,		1),
	SC (__NR__llseek,	sys_llseek,		5),
	SC (__NR_getdents,	sys_getdents,		3),
	SC (__NR__newselect,	sys_select,		5),
	SC (__NR_flock,		sys_flock,		2),
	SC (__NR_msync,		sys_msync,		3),
	SC (__NR_readv,		sys_readv,		3),
	SC (__NR_writev,	sys_writev,		3),
	SC (__NR_getsid,	sys_getsid,		1),
	SC (__NR_fdatasync,	sys_fdatasync,		1),
	SC (__NR__sysctl,	sys_sysctl,		1),
	SC (__NR_mlock,		sys_mlock,		2),
	SC (__NR_munlock,	sys_munlock,		2),
	SC (__NR_mlockall,	sys_mlockall,		1),
	SC (__NR_munlockall,	sys_munlockall,		0),
	SC (__NR_nanosleep,	sys_nanosleep,		2),
	SC (__NR_mremap,	sys_mremap,		4),
	SC (__NR_setresuid,	sys_setresuid16,	3),
	SC (__NR_getresuid,	sys_getresuid16,	3),
	SC (__NR_poll,		sys_poll,		3),
	SC (__NR_nfsservctl,	sys_nfsservctl,		3),
	SC (__NR_setresgid,	sys_setresgid16,	3),
	SC (__NR_getresgid,	sys_getresgid16,	3),
	SC (__NR_prctl,		sys_prctl,		5),
	SC (__NR_sched_setparam,	sys_sched_setparam,		2),
	SC (__NR_sched_getparam,	sys_sched_getparam,		2),
	SC (__NR_sched_setscheduler,	sys_sched_setscheduler,		3),
	SC (__NR_sched_getscheduler,	sys_sched_getscheduler,		1),
	SC (__NR_sched_yield,		sys_sched_yield,		0),
	SC (__NR_sched_get_priority_max,sys_sched_get_priority_max,	1),
	SC (__NR_sched_get_priority_min,sys_sched_get_priority_min,	1),
	SC (__NR_sched_rr_get_interval,	sys_sched_rr_get_interval,	2),
	SC (__NR_rt_sigreturn,		sys_rt_sigreturn,		0),
	SC (__NR_rt_sigaction,		sys_rt_sigaction,		4),
	SC (__NR_rt_sigprocmask,	sys_rt_sigprocmask,		4),
	SC (__NR_rt_sigpending,		sys_rt_sigpending,		2),
	SC (__NR_rt_sigtimedwait,	sys_rt_sigtimedwait,		4),
	SC (__NR_rt_sigqueueinfo,	sys_rt_sigqueueinfo,		3),
	SC (__NR_rt_sigsuspend,		sys_rt_sigsuspend,		2),
	SC (__NR_pread64,	sys_pread64,		4),
	SC (__NR_pwrite64,	sys_pwrite64,		4),
	SC (__NR_chown,		sys_chown16,		3),
	SC (__NR_getcwd,	sys_getcwd,		2),
	SC (__NR_capget,	sys_capget,		2),
	SC (__NR_capset,	sys_capset,		2),
	SC (__NR_sigaltstack,	sys_sigaltstack,	2),
	SC (__NR_sendfile,	sys_sendfile,		4),
	SC (__NR_vfork,		sys_vfork,		0),
	SC (__NR_getrlimit,	sys_getrlimit,		2),
	SC (__NR_mmap2,		sys_mmap2,		6),
	SC (__NR_truncate64,	sys_truncate64,		2),
	SC (__NR_ftruncate64,	sys_ftruncate64,	2),
	SC (__NR_stat64,	sys_stat64,		2),
	SC (__NR_lstat64,	sys_lstat64,		2),
	SC (__NR_fstat64,	sys_fstat64,		2),
	SC (__NR_lchown32,	sys_lchown,		3),
	SC (__NR_getuid32,	sys_getuid,		0),
	SC (__NR_getgid32,	sys_getgid,		0),
	SC (__NR_geteuid32,	sys_geteuid,		0),
	SC (__NR_getegid32,	sys_getegid,		0),
	SC (__NR_setreuid32,	sys_setreuid,		2),
	SC (__NR_setregid32,	sys_setregid,		2),
	SC (__NR_getgroups32,	sys_getgroups,		2),
	SC (__NR_setgroups32,	sys_setgroups,		2),
	SC (__NR_fchown32,	sys_fchown,		3),
	SC (__NR_setresuid32,	sys_setresuid,		3),
	SC (__NR_getresuid32,	sys_getresuid,		3),
	SC (__NR_setresgid32,	sys_setresgid,		3),
	SC (__NR_getresgid32,	sys_getresgid,		3),
	SC (__NR_chown32,	sys_chown,		3),
	SC (__NR_setuid32,	sys_setuid,		1),
	SC (__NR_setgid32,	sys_setgid,		1),
	SC (__NR_setfsuid32,	sys_setfsuid,		1),
	SC (__NR_setfsgid32,	sys_setfsgid,		1),
	SC (__NR_pivot_root,	sys_pivot_root,		2),
	SC (__NR_mincore,	sys_mincore,		3),
	SC (__NR_madvise,	sys_madvise,		3),
	SC (__NR_getdents64,	sys_getdents64,		3),
	SC (__NR_fcntl64,	sys_fcntl64,		3),
	SC (__NR_tkill,		sys_tkill,		3),
	SC (__NR_statfs64,	sys_statfs64,		2),
	SC (__NR_fstatfs64,	sys_fstatfs64,		2),
#undef SC
};

void syscall_handler(struct pt_regs *regs, void *excep_info)
{
	unsigned int sc_number;
	unsigned int *user_ap;
	unsigned int nr_args;

	sc_number = *(unsigned int *)(excep_info);

	/*
	 * Check if the called syscall is known at all and that it isn't
	 * a no-longer supported legacy syscall.
	 */
	if (unlikely (sc_number >= ARRAY_SIZE (syscall) ||
				!syscall[sc_number].sc_func)) {
		printk (KERN_DEBUG "%s(%d): syscall %d out of range or not "
				"implemented.\n", current->comm, current->pid,
				sc_number);
		printk (KERN_DEBUG "Please report to "
				"<linux-vax@pergamentum.com>.\n");
		regs->r0 = -ENOSYS;
		return;
	}

	/* Syscall arguments */
	user_ap = (unsigned int *)(regs->ap);

	if (likely (regs->psl.prevmode == PSL_MODE_USER)) {
		/*
		 * User Mode Syscall Handling - check access to arguments.
		 */

		if (user_ap >= (unsigned int *)0x80000000) {
			regs->r0 = -EFAULT;
			return;
		}

		/*
		 * We don't need to deal with the case where AP + nr_args*4
		 * reaches up into S0 space because we've got a guard page
		 * at 0x80000000 that will cause an exception in the movc3
		 * below that copies the argument list.
		 */
		if (get_user(nr_args, user_ap)) {
			regs->r0 = -EFAULT;
			return;
		}

		/*
		 * The SP value in the pt_regs structure should really
		 * be the user stack pointer, not the kernel stack pointer
		 */
		regs->sp = __mfpr(PR_USP);
	} else {
		/*
		 * Kernel Mode Syscall Handling - no need to check access to arguments.
		 */
		nr_args = *user_ap;
	}

#ifdef CONFIG_DEBUG_VAX_CHECK_CHMx_ARGS
	/*
	 * Check number of syscall arguments
	 */
	if (unlikely (syscall[sc_number].nr_args != nr_args)) {
		printk (KERN_DEBUG "%s(%d): stack mismatch (should=%d, caller=%d) on syscall %d\n",
				current->comm, current->pid,
				syscall[sc_number].nr_args, nr_args, sc_number);
		printk (KERN_DEBUG "Please report to "
				"<linux-vax@pergamentum.com>.\n");
#ifdef CONFIG_DEBUG_VAX_CHECK_CHMx_ARGS_ABORT
		regs->r0 = -EFAULT;
		return;
#endif /* CONFIG_DEBUG_VAX_CHECK_CHMx_ARGS_ABORT */
	}
#endif /* CONFIG_DEBUG_VAX_CHECK_CHMx_ARGS */

	/*
	 * We pass all the user-supplied args plus the pointer to the
	 * regs to the syscall function.  If the syscall is implemented
	 * in the core kernel, then it will ignore the additional
	 * argument.
	 */
	__asm__(
	"	pushl %1		\n"
	"	subl2 %2,%%sp		\n"
	"1:	movc3 %2,4(%4),(%%sp)	\n"
	"	calls %3, %5		\n"
	"	brb 3f			\n"
	"2:	movl %6, %%r0		\n"
	"3:	movl %%r0, %0		\n"
	".section ex_table,\"a\"	\n"
	".align 2			\n"
	".long 1b, 2b			\n"
	".text				\n"
	: "=g"(regs->r0)			/* 0 - syscall return value */
	: "g"(regs),				/* 1 - regs ptr */
	  "g"(nr_args * 4),			/* 2 - number of syscall argument bytes to copy */
	  "g"(nr_args + 1),			/* 3 - number of syscall arguments + explicit "regs" ptr*/
	  "r"(user_ap),				/* 4 - source for syscall arguments */
	  "g"(*syscall[sc_number].sc_func),	/* 5 - syscall function ptr */
	  "g"(-EFAULT)				/* 6 - Return -EFAULT if calling the syscall failed */
	: "r0", "r1", "r2", "r3", "r4", "r5");

	return;
}

int sys_pipe(unsigned long *fildes)
{
        int fd[2];
        int error;

        lock_kernel();
        error = do_pipe(fd);
        unlock_kernel();
        if (!error) {
                if (copy_to_user(fildes, fd, 2*sizeof(int)))
                        error = -EFAULT;
        }
        return error;
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
int sys_ipc (uint call, int first, int second, int third, void *ptr,
		long fifth)
{
#ifdef CONFIG_SYSVIPC
        int ret;

        switch (call) {
        case SEMOP:
                return sys_semop (first, (struct sembuf *)ptr, second);

        case SEMGET:
                return sys_semget (first, second, third);

        case SEMCTL: {
                union semun fourth;
                if (!ptr)
                        return -EINVAL;
                if (get_user(fourth.__pad, (void **) ptr))
                        return -EFAULT;
                return sys_semctl (first, second, third, fourth);
        }

        case MSGSND:
                return sys_msgsnd (first, (struct msgbuf *) ptr, second, third);
                break;

        case MSGRCV:
                return sys_msgrcv (first, (struct msgbuf *) ptr, second, fifth,
				third);
        case MSGGET:
                return sys_msgget ((key_t) first, second);

        case MSGCTL:
                return sys_msgctl (first, second, (struct msqid_ds *) ptr);

        case SHMAT: {
                ulong raddr;
                ret = do_shmat (first, (char *) ptr, second, &raddr);
                if (ret)
                        return ret;
                return put_user (raddr, (ulong *) third);
        }

        case SHMDT:
                return sys_shmdt ((char *)ptr);

        case SHMGET:
                return sys_shmget (first, second, third);

        case SHMCTL:
                return sys_shmctl (first, second, (struct shmid_ds *) ptr);

        default:
                return -EINVAL;

        }

        return -EINVAL;
#else /* CONFIG_SYSVIPC */
	return -ENOSYS;
#endif /* CONFIG_SYSVIPC */
}

int sys_uname(struct old_utsname * name)
{
        int err;

        if (!name)
                return -EFAULT;

        down_read(&uts_sem);
        err = copy_to_user(name, &system_utsname, sizeof (*name));
        up_read(&uts_sem);

        return err? -EFAULT: 0;
}

unsigned long sys_mmap(unsigned long addr, size_t len, int prot,
                                  int flags, int fd, off_t offset)
{
        struct file * file = NULL;
        unsigned long error = -EFAULT;

        lock_kernel();
        if (!(flags & MAP_ANONYMOUS)) {
                error = -EBADF;
                file = fget(fd);
                if (!file)
                        goto out;
        }
        flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
        error = do_mmap(file, addr, len, prot, flags, offset);
        if (file)
                fput(file);

out:
        unlock_kernel();
        return error;
}

/* common code for old and new mmaps */
static inline long do_mmap2(
        unsigned long addr, unsigned long len,
        unsigned long prot, unsigned long flags,
        unsigned long fd, unsigned long pgoff)
{
        int error = -EBADF;
        struct file * file = NULL;

        flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
        if (!(flags & MAP_ANONYMOUS)) {
                file = fget(fd);
                if (!file)
                        goto out;
        }

        down_write(&current->mm->mmap_sem);
        error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
        up_write(&current->mm->mmap_sem);

        if (file)
                fput(file);

out:
        return error;
}

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}


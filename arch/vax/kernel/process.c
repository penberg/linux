#include <linux/sched.h>

void machine_restart(char *command)
{
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
}

void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp)
{
}

void exit_thread(void)
{
}

void flush_thread(void)
{
}

int copy_thread(unsigned long clone_flags, unsigned long usp, unsigned long arg, struct task_struct *p)
{
	return 0;
}

unsigned long thread_saved_pc(struct task_struct *tsk)
{
	return 0;
}

void show_regs(struct pt_regs *regs)
{
}

unsigned long get_wchan(struct task_struct *task)
{
	return 0;
}

unsigned long arch_align_stack(unsigned long sp)
{
	return sp;
}

void (*pm_power_off)(void);

EXPORT_SYMBOL(pm_power_off);

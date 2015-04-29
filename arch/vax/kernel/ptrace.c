#include <linux/ptrace.h>

void user_disable_single_step(struct task_struct *child)
{
}

void ptrace_disable(struct task_struct *child)
{
}

long arch_ptrace(struct task_struct *child, long request, unsigned long addr, unsigned long data)
{
	return 0;
}

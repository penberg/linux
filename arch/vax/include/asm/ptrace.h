#ifndef _ASM_VAX_PTRACE_H
#define _ASM_VAX_PTRACE_H

#include <uapi/asm/ptrace.h>

#include <asm/psl.h>

struct task_struct;

static inline int user_mode(struct pt_regs *regs)
{
	return regs->psl.cur_mod == PSL_MODE_USER;
}

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	BUG();
}

#define instruction_pointer(regs)	((regs)->pc)

extern void do_syscall_trace(struct pt_regs *regs, int entryexit);
extern int read_tsk_long(struct task_struct *, unsigned long, unsigned long *);
extern int read_tsk_short(struct task_struct *, unsigned long, unsigned short *);

#define arch_has_single_step()	(0)

#endif /* _ASM_VAX_PTRACE_H */

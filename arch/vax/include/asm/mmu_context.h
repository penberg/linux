#ifndef _ASM_VAX_MMU_CONTEXT_H
#define _ASM_VAX_MMU_CONTEXT_H

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm-generic/mm_hooks.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

extern unsigned long asid_cache;
extern unsigned long pgd_current;

#define TLBMISS_HANDLER_SETUP_PGD(pgd) (pgd_current = (unsigned long)(pgd))

#define TLBMISS_HANDLER_SETUP()				\
do {							\
	BUG();                                          \
} while (0)

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

static inline void get_new_mmu_context(struct mm_struct *mm)
{
	BUG();
}

static inline int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context = 0;
	return 0;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk)
{
	BUG();
}

static inline void destroy_context(struct mm_struct *mm)
{
}

static inline void deactivate_mm(struct task_struct *task, struct mm_struct *mm)
{
}

static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	BUG();
}

#endif /* _ASM_VAX_MMU_CONTEXT_H */

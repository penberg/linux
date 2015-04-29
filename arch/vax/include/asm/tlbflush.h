#ifndef _ASM_VAX_TLBFLUSH_H
#define _ASM_VAX_TLBFLUSH_H

#include <linux/mm.h>

static inline void __flush_tlb(void)
{
	mtpr(IPR_TBIA, IPR_TBIA);
}

static inline void __flush_tlb_one(unsigned long addr)
{
	mtpr(addr, IPR_TBIS);
}

static inline void flush_tlb_all(void)
{
	__flush_tlb();
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->mm)
		__flush_tlb();
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	if (vma->vm_mm == current->mm)
		__flush_tlb();
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	return __flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long addr)
{
	if (vma->vm_mm == current->mm)
		__flush_tlb_one(addr);
}

static inline void flush_tlb_one(unsigned long addr)
{
	__flush_tlb_one(addr);
}

#endif /* _ASM_VAX_TLBFLUSH_H */

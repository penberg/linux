/*
 * Copyright (C) 2000  Kenn Humborg
 *
 * This file contains the functions for mapping specific physical
 * addresses into virtual memory, normally used to memory map
 * device registers in IO space.
 */

/* The i386 code maps a physical range by allocating a new vm area
   (which seems to have a full 3-level page table structure) and
   then fixing the PTEs to point to the specified physical region.

   We can't do this right now in VAXland because we haven't got the
   mm layer implemented far enough yet.  In fact, I don't know if
   we will ever be able to do it that way because we don't have
   sparse enough page tables.  I think that we'll have to statically
   allocate a system page table that is big enough to map all physical
   RAM plus some "spare" page table entries for IO mapping.

   Dynamically expanding the system page table _may_ be possible,
   but would require enough contiguous physical memory to hold the
   complete, larger table while we copy the current PTEs.  I
   suspect that it might not work in the general case, because I
   have a feeling that we won't be able to notify everything that
   needs to know when the SPT base addr changes.  (Scatter/gather
   hardware might be one example.)

   So, here's what we do right now:

   1. When creating the initial system page table, we allocate
      a certain number of spare PTEs at the end of the table to
      be used for mapping IO space.

   2. Each of these PTEs is be marked INVALID.  A PTE in this
      range which is INVALID is available for use for IO space
      mapping, one which is VALID is already in use.

   3. When a driver wants to map a range of IO space, we work
      out how many PTEs we need and try to find a contigous chunk
      of free (i.e. INVALID) PTEs.  We make these PTEs valid and
      point them to the specified physical area.

*/

#include <linux/mm.h>
#include <asm/tlbflush.h>

/* Defined in head.S */
extern pte_t *iomap_base;

/* This array will store the sizes of areas re-mapped by ioremap().
   We need this because iounmap() doesn't take a size arg.
   We store the size as a PTE count. */

static unsigned int iomap_sizes[SPT_PTES_IOMAP];

void *ioremap(unsigned long phys_addr, unsigned long size)
{
	unsigned long phys_start;
	unsigned int offset;
	unsigned long phys_end;
	unsigned int num_ptes;
	void *virt_start;
	unsigned int i;
	pte_t *start_pte;
	pte_t *p;
	unsigned long pfn;

	/* Page align the physical addresses */
	phys_start = PAGE_ALIGN_PREV(phys_addr);
	offset = phys_addr - phys_start;

	phys_end = PAGE_ALIGN(phys_addr + size);

	num_ptes = (phys_end - phys_start) >> PAGE_SHIFT;

	start_pte = NULL;
	p = iomap_base;
	while (p < iomap_base+SPT_PTES_IOMAP) {

		if (pte_val(*p) & _PAGE_VALID) {
			/* PTE in use, start over */
			start_pte = NULL;
		} else {
			/* PTE is available */
			if (start_pte == NULL) {
				start_pte = p;
			}
		}

		p++;

		/* Have we found enough PTEs? */
		if (start_pte != NULL) {
			if ((p - start_pte) == num_ptes) {
				break;
			}
		}
	}

	if ((p - start_pte) != num_ptes) {
		/* Unable to find contiguous chunk of IOMAP PTEs */
		printk("ioremap: cannot find 0x%04x available PTEs\n", num_ptes);
		return NULL;
	}

	/* Stash the size of this IO space mapping */
	iomap_sizes[start_pte - iomap_base] = num_ptes;

	virt_start = SPTE_TO_VIRT(start_pte);

	for (i = 0; i < num_ptes; i++) {
		pfn = (phys_start >> PAGE_SHIFT) + i;
		set_pte(start_pte + i, pfn_pte(pfn, __pgprot(_PAGE_VALID | _PAGE_KW)) );

		/* fixme: tlb flushes for other pagelets */
		__flush_tlb_one(virt_start + (i<<PAGE_SHIFT));
	}

	printk("IO mapped phys addr 0x%08lx, 0x%04x pages at virt 0x%08lx (IOMAP PTE index 0x%04Zx)\n",
			phys_start, num_ptes, (unsigned long) virt_start, start_pte - iomap_base);

	return virt_start + offset;
}

void iounmap(void *addr)
{
	pte_t *p;
	unsigned int num_ptes;

	if (addr < (void *)PAGE_OFFSET) {
		printk("iounmap: virtual addr 0x%08lx not in S0 space\n",
				(unsigned long) addr);
		return;
	}

	p = (pte_t *)GET_HWSPTE_VIRT(addr);

	if ((p < iomap_base) && (p >= (iomap_base + SPT_PTES_IOMAP))) {
		printk("iounmap: virtual addr 0x%08lx not in IOMAP region\n",
				(unsigned long) addr);
		return;
	}

	num_ptes = iomap_sizes[p - iomap_base];

	if (num_ptes == 0) {
		printk("iounmap: virtual addr 0x%08lx not currently IO mapped\n",
				(unsigned long) addr);
		return;
	}

	iomap_sizes[p - iomap_base] = 0;

	printk("IO unmapping 0x%04x pages at PTE index 0x%04Zx\n",
			num_ptes, p - iomap_base);

	while (num_ptes--) {
		pte_val(*p) = 0;
		p++;

		__flush_tlb_one(addr);
		addr += PAGELET_SIZE;
	}

}


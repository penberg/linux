/*
 * pgalloc.c  Routines from include/asm-vax/mm/pgalloc.h
 *            Allocation of page table entries and so forth.
 *
 *            This is the main part of the VAX specific memory layer.
 *
 * Copyright atp Jun 2001 - complete rewrite.
 * atp aug 2001  - add in stuff for vmalloc to work (pmd_alloc_kernel)
 *	    fix mistake in pte_alloc_kernel.
 * atp 21 aug 01 - make TASK_WSMAX what was intended, add in segv stuff.
 *
 * License: GNU GPL
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>

extern void vaxpanic(char *reason);

#undef VAX_MM_PGALLOC_DEBUG

/*
 * Allocate a pgd. We don't - at present - need to worry about
 * maintaining a bitmap as we put pgds that we are finished with
 * on our quicklists pool.
 */
static inline pgd_t *get_pgd_fast(void)
{
	pgd_t *pgd;

	if ((pgd = pgd_free_list.head) != NULL) {
		pgd_free_list.head = pgd->next;
		pgd->next = NULL;
		pgd_free_list.size--;
	}
	return pgd;
}

/* Allocate a pgd */
pgd_t *pgd_alloc(struct mm_struct *mm)
{
	/*
	 * This is rather wasteful, as only a few longwords are
	 * used in the entire 4kb page. Perhaps we can do something
	 * smarter here by using the quicklists to pack the pgds into
	 * a single page.
	 */
	pgd_t *pgd;
	unsigned long taskslot;

	/* Grab a pgd off the cache */
	pgd = get_pgd_fast();

	if (!pgd) {
		/* Check if we have run out of balance slots */
		if (pgd_free_list.slots_used >= TASK_MAXUPRC)
			return NULL;

		pgd = kmalloc(sizeof(pgd_t) * PTRS_PER_PGD, GFP_KERNEL);
		if (!pgd)
			return NULL;

		memset(pgd, 0, sizeof(pgd_t) * PTRS_PER_PGD);

		taskslot = GET_TASKSLOT(pgd_free_list.slots_used);
		/* one more slot used */
		pgd_free_list.slots_used++;

		pgd[0].pmd = 0;  /* These are blank */
		pgd[1].pmd = 0;
	} else {
		/* pgd_clear keeps this */
		taskslot = pgd->slot;
	}

	if (pgd) {

		/* Set the values of the base + length registers */
		pgd[0].br = taskslot + P0PTE_OFFSET; /* skip the PMD */
		pgd[0].lr = 0x0;
		/* This comes in handy later */
		pgd[0].slot = taskslot;
		/* p1br points at what would be page mapping 0x40000000 (i.e. the _end_ of the slot)*/
		pgd[1].br = taskslot+ (P1PTE_OFFSET) - 0x800000 ;
		/* This is the unmapped number of PTEs */
		pgd[1].lr = 0x40000;
		pgd[1].slot = taskslot;

		pgd[0].segment = 0;
		pgd[1].segment = 1;

#ifdef VAX_MM_PGALLOC_DEBUG
		printk(KERN_DEBUG "VAXMM:pgd_alloc: p0: %8lX, %8lX, p1: %8lX, %8lx, slot %ld, taskslot %8lx\n", pgd[0].br, pgd[0].lr, pgd[1].br, pgd[1].lr, pgd_free_list.slots_used-1, pgd[0].slot);
#endif
		/* Set the s0 region, from the master copy in swapper_pg_dir */
		memcpy(pgd + USER_PTRS_PER_PGD, swapper_pg_dir + USER_PTRS_PER_PGD, (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}

	return pgd;
}

void pgd_clear(pgd_t * pgdp)
{
	/* Wipe a pgd structure carefully -- this is probably overkill */
	pgdp->pmd = 0;

	if (pgdp->segment) {
		/* p1br points at what would be page mapping 0x40000000 */
		pgdp->br = pgdp->slot+ (P1PTE_OFFSET) - 0x800000;
		/* This is the unmapped number of PTEs */
		pgdp->lr = 0x40000;
	} else {
		pgdp->br = pgdp->slot+ (P0PTE_OFFSET); /* skip the PMD */
		pgdp->lr = 0x0;
	}
}

/*
 * Remap a given page to be part of a contiguous page table for p0/1 space.
 *
 * This is like remap_pte_range in memory.c but VAX specific.  It's called
 * when we're creating part of a process page table.  A new, blank page
 * has just been allocated and we want to use this page to back part of
 * the process page table.  This will result in this new page being
 * double-mapped.  One mapping will be its 'identity' mapping where
 * VIRT = (PHYS + PAGE_OFFSET).  The other mapping will be in the middle
 * of the process page table.
 *
 * s0addr is the address in S0 space that we need to remap the page
 * pointed at by pte_page to.
 *
 * This is also called to remap the two pages in our page middle directory.
 */
static void remap_pgtable_page(void *s0addr, struct page *page)
{
	pte_t *s0pte;

	/* sanity checks */
	if (!s0addr) {
		vaxpanic("VAXMM: null S0 address in remap_pgtable_page!\n");
		return;
	}
	if (!page) {
		vaxpanic("VAXMM: null pte_page in remap_pgtable_page!\n");
		return;
	}

	/* Locate the S0 pte that describes the page pointed to by s0addr */
	s0pte = GET_SPTE_VIRT(s0addr);

#ifdef VAX_MM_PGALLOC_DEBUG
	/* Is it already pointing somewhere? */
	if (pte_present(*s0pte))
		printk(KERN_DEBUG "VAXMM: S0 pte %8p already valid in "
				"remap_pgtable_page??\n", s0pte);
	printk(KERN_DEBUG "VAXMM: mapping PTE page %p at %p\n", page, s0addr);
#endif
	/* zap the map */
	set_pte(s0pte,mk_pte(page, __pgprot(_PAGE_VALID|_PAGE_KW)));

	flush_tlb_all();
}

/*
 * Invalidate the S0 pte that was remapped to point at this page in the
 * process page table or the page middle directory.
 */
static void unmap_pgtable_page(void *page)
{
	pte_t *s0pte;

	/* Sanity checks */
	if (!page) {
		vaxpanic(KERN_ERR "VAXMM: null S0 address in unmap_pgtable_page!\n");
		return;
	}

	/* Locate the S0 pte that describes the page pointed to by pte_page */
	s0pte = GET_SPTE_VIRT(page);
#ifdef VAX_MM_PGALLOC_DEBUG
	printk(KERN_DEBUG "unmap_pgtable_page: s0addr %p, s0pte %p\n", page, s0pte);
#endif

	 set_pte(s0pte, pte_mkinvalid(*s0pte));
	 /* FIXME: these flush_tlb_alls need replacing with flush_tlb_8 */
	 flush_tlb_all();
	 // __flush_tlb_one(s0addr);
}

/*
 * We used to call this routine pmd_alloc. At v2.4.3 pmd_alloc got removed
 * from include/linux/mm.h, and we have now pgd_populate and pmd_populate.
 * This is pgd_populate
 */
void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pmd_t *pmd)
{
	/*
	 * We have a two page block of memory, allocated via pmd_alloc by
	 * pmd_alloc_one. This needs to be remapped into the appropriate pmd
	 * section in the taskslot in S0 space.
	 * recap:	The taskslot holds all the ptes in a contiguous section
	 *		of S0 address space. The amounts of virtual address
	 *		space are mapped out at boot time, from the constants
	 *		in asm-vax/mm/task.h. The first four pages of this
	 *		region are "pmd" pages, used as the bookkeeping
	 *		information, which is normally done by the pgd page on
	 *		32bit processors. But we have hijacked the pgds to
	 *		represent the four VAX memory segments, and to hold all
	 *		the base/length register information and other related
	 *		stuff.
	 * Updated atp Mar 2002. pgd_populate, remove PGD_SPECIAL botch.
	 */
	unsigned int is_p1 = pgd->segment;
	pmd_t *s0addr;

#ifdef VAX_MM_PGALLOC_DEBUG
	printk(KERN_DEBUG "VAXMM: Calling pgd_populate with (mm=%8p, pgd=%8p, "
			"pmd=%8p\n",mm,pgd,pgd->pmd);
#endif
	/* Sanity check */
	if (pgd->pmd) {
#ifdef VAX_MM_PGALLOC_DEBUG
		printk(KERN_DEBUG "VAXMM: Calling pmd_alloc on already "
				"allocated page (pgd=%8p,pmd=%8p)\n",pgd,pgd->pmd);
#endif
		return;
	}

	/* Calculate which bit of the page table area this page fits into. */
        s0addr = (pmd_t *)pgd->slot;	/* base of the slot */
	s0addr += is_p1? (P1PMD_OFFSET/sizeof(pmd_t)): (P0PMD_OFFSET/sizeof(pmd_t));

	/* Remap and clear the first page */
	clear_page(pmd);
	remap_pgtable_page(s0addr, virt_to_page(pmd));

	/* This is the pointer to our pmd table. */
        pgd->pmd=s0addr;

	/* This is a two page block of memory */
	s0addr += (PAGE_SIZE/sizeof(pmd_t));
	pmd += (PAGE_SIZE/sizeof(pmd_t));

	clear_page(pmd);
	remap_pgtable_page(s0addr, virt_to_page(pmd));

#ifdef VAX_MM_PGALLOC_DEBUG
	printk(KERN_DEBUG "VAXMM: pmd_alloc: pgd %8p, pgd->br %8lx, pgd->lr "
			"%8lx, \n\tpgd->pmd %8p\n",
			pgd,pgd->br, pgd->lr, pgd->pmd);
#endif
	return;
}

/*
 * pmd_populate is called when the MM core wants to make a page in
 * a process page table valid.  The core has already allocated a
 * page for this, and it now wants for us to use this page to
 * hold PTEs for the range corresponding to the PMD entry pointed
 * to by the pmd parameter.
 *
 * It's made a bit trickier by the fact that we need to work out if
 * it's a P0 or P1 page table being populated.  And then we also
 * need to watch for this new page of PTEs being beyond the current
 * P0LR or P1LR and extending P0/1LR as necessary.
 *
 * We used to check against WSMAX and STKMAX here, but we now do this
 * check in pte_alloc_one(), where it's easier to check (since pte_alloc_one()
 * is handed the user address).
 *
 * We make use of the knowledge that the pmd is a single block, to work back
 * to the pgd, which is where the base and length register values are held.
 *
 * FIXMES: page table locking.
 */
/* This function could be simpler if we used system page table
   entries as PMD entries. */
void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte_page)
{
	pmd_t *pmd_base;
	unsigned long pmd_index;
	unsigned int pspace;
	pte_t *pte_addr;
	unsigned long page_index;
	pgd_t *pgd_entry;

	/* Find the start of the page middle directory containing this PMD entry */
	pmd_base = (pmd_t *) ((unsigned long)pmd & PTE_TASK_MASK);  /* base of the pmd */

	/* The process page table page that we want to remap is at offset
	   pmd_index into the relevant page middle directory */
	pmd_index = pmd - pmd_base;

	/* But, is it a P0 or a P1 PMD?  Assume P0 until proven otherwise */
	pspace = 0;

	if (pmd_base == mm->pgd[0].pmd)
		pspace = 0;
	else if (pmd_base == mm->pgd[1].pmd)
		pspace = 1;
	else
		BUG();

	pgd_entry = mm->pgd + pspace;

	/* Now we can work out the system virtual address of the relevant
	   page in the process page table */
	pte_addr = (pte_t *)(pgd_entry->br + (pmd_index << PAGE_SHIFT));

#ifdef VAX_MM_PGALLOC_DEBUG
	printk(KERN_DEBUG "VAXMM: pmd_populate: mm %p br %08lx lr %04lx "
			"pmd %p page %p pte_addr %p reg %d index %04lx\n",
			mm, pgd_entry->br, pgd_entry->lr, pmd, pte_page,
			pte_addr, pspace, pmd_index);
#endif
	/* Double-map the newly-allocated page to this S0 address */
	remap_pgtable_page(pte_addr, pte_page);

	/* And point the PMD entry to this new mapping */
	pmd->pte_page = pte_addr;

	/*
	 * Now adjust the P0LR or P1LR if we we've mapped a new
	 * page at the end of the region
	 */

	/* Calculate how far into the region the newly-added page lives */
	if (pspace == 0) {
		/*
		 * For P0 space, we want to consider the top end of the new
		 * page of PTEs
		 */
		page_index = (pte_addr + PTRS_PER_PTE) - (pte_t *)pgd_entry->br;

		if (pgd_entry->lr < page_index)
			pgd_entry->lr = page_index;
	} else {
		/*
		 * For P1 space, we want to consider the bottom end of the new
		 * page of PTEs
		 */
		page_index = pte_addr - (pte_t *)pgd_entry->br;

		if (pgd_entry->lr > page_index)
			pgd_entry->lr = page_index;
	}

#ifdef VAX_MM_PGALLOC_DEBUG
	printk(KERN_DEBUG "VAXMM: pmd_populate: new lr %04lx\n", pgd_entry->lr);
#endif

	/*
	 * If all this work is for the current process, then we need to
	 * update the hardware registers
	 */
	if (pspace == 0) {
		if (current->thread.pcb.p0br == pgd_entry->br) {
#ifdef VAX_MM_PGALLOC_DEBUG
			printk(KERN_DEBUG "VAXMM: pmd_populate: updating hardware regs\n");
#endif
			current->thread.pcb.p0lr = pgd_entry->lr * 8;
			set_vaxmm_regs_p0(pgd_entry);
		}

	} else {
		if (current->thread.pcb.p1br == pgd_entry->br) {
#ifdef VAX_MM_PGALLOC_DEBUG
			printk(KERN_DEBUG "VAXMM: pmd_populate: updating hardware regs\n");
#endif
			current->thread.pcb.p1lr = pgd_entry->lr * 8;
			set_vaxmm_regs_p1(pgd_entry);
		}
	}
}


/*
 * The pmd argument points to a single PMD entry (which corresponds to
 * a single page in a process page table).  We should invalidate the
 * mapping of this page in the process page table and then clear out
 * the PMD entry itself
 */
void pmd_clear(pmd_t *pmd)
{
	unmap_pgtable_page(pmd->pte_page);
	pmd->pte_page = NULL;
}

/* Find an entry in the third-level page table. */
pte_t * pte_offset(pmd_t *pmd, unsigned long address)
{
	pte_t *pte;
	pte = pmd->pte_page + ((address>>PAGE_SHIFT) & (PTRS_PER_PTE-1));
#ifdef VAX_MM_PGALLOC_DEBUG
	printk(KERN_DEBUG "VAXMM:pte_offset: pmd %p, address %8lx, "
			"pte_pte %p\n", pmd, address, pte);
#endif
	return pte;
}


/*
 * Initialise the VM system.
 * Copyright atp Nov 1998
 * GNU GPL
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bootmem.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/rpb.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

#define VAX_INIT_DEBUG

unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)]
		__attribute__ ((__aligned__(PAGE_SIZE)));

pte_t *pg0;

struct pgd_cache pgd_free_list;

/*
 * We don't use the TLB shootdown stuff yet, but we need this to keep
 * the generic TLB shootdown code happy.
 */
DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

/*
 * This is task 0's PGD structure.  Entries 4 and 5 will be filled with
 * the system page table base and size by head.S.  The remaining
 * entries (0 to 3) will be left at zero as there is no valid user
 * context in task 0.
 */
pgd_t swapper_pg_dir[PTRS_PER_PGD];
pmd_t swapper_pm_dir[2048] __attribute__ ((__aligned__(8192)));  /* two pages for the kernel S0 pmd */

/*
 * In other architectures, paging_init sets up the kernel's page tables.
 * In Linux/VAX, this is already done by the early boot code. For the
 * physical RAM. In this routine we initialise the remaining areas of
 * the memory, and system page table.
 */
void __init paging_init(void)
{
	hwpte_t *pte, *lastpte;
	unsigned int ii;

	/* Sort out page table. */
	pg0 = (pte_t *) SPT_BASE;

	/* Set up pmd */
	swapper_pg_dir[2].pmd  = swapper_pm_dir;

	/* FIXME: This is where the VMALLOC stuff from head.S should go */
	printk("VAXMM: Initialising mm layer for %d tasks of size %dMB\n",
			TASK_MAXUPRC, TASK_WSMAX >> 20);

	/*
	 * Size the process page table slots. See asm/mm/task.h for details
	 * The _START and _END macros are from pgtable.h
	 * This is all in PAGELETS and HWPTES, hence no set_pte
	 */
	pte = (hwpte_t *) GET_SPTE_VIRT(VMALLOC_END);
	lastpte = (hwpte_t *) GET_SPTE_VIRT(TASKPTE_START);
	ii = 0;

	/* Clear this area */
	while (pte < lastpte) {
		*pte++ = __hwpte(0x00000000);
		ii++;
	}
	/* This is stored in hwptes */
	SPT_LEN += ii;

	pte = (hwpte_t *) GET_SPTE_VIRT(TASKPTE_START);
	lastpte = pte + SPT_HWPTES_TASKPTE;
	/* Clear this area */
	while (pte < lastpte)
		*pte++ = __hwpte(0x00000000);

	/* This is stored in hwptes */
	SPT_LEN += SPT_HWPTES_TASKPTE;
	__mtpr(SPT_LEN, PR_SLR);
	flush_tlb();

	printk("VAXMM: system page table base %8lx, length (bytes) %8lx length (PTEs) %8lx\n",
			SPT_BASE, SPT_SIZE, SPT_LEN);
}

#if DEBUG_POISON
static void kill_page(unsigned long pg)
{
	unsigned long *p = (unsigned long *) pg;
	unsigned long i = PAGE_SIZE, v = 0xdeadbeefdeadbeef;

	do {
		p[0] = v;
		p[1] = v;
		p[2] = v;
		p[3] = v;
		p[4] = v;
		p[5] = v;
		p[6] = v;
		p[7] = v;
		i -= 64;
		p += 8;
	} while (i != 0);
}
#else
#define kill_page(pg)
#endif

void mem_init(void)
{
	max_mapnr = num_physpages = max_low_pfn;

	/* Clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* This will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem();
	high_memory = (void *) __va((max_low_pfn) * PAGE_SIZE);

	printk("Memory: %luk/%luk available\n",
			(unsigned long) nr_free_pages() << (PAGE_SHIFT - 10),
			max_mapnr << (PAGE_SHIFT - 10));

	return;
}

static void free_reserved_mem(void *start, void *end)
{
	void *__start = start;

	for (; __start < end; __start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(__start));
		set_page_count(virt_to_page(__start), 1);
		free_page((long) __start);
		totalram_pages++;
	}
}

void free_initmem(void)
{
	extern char __init_begin, __init_end;

	free_reserved_mem(&__init_begin, &__init_end);

	printk("Freeing unused kernel memory: %Zdk freed\n",
			(&__init_end - &__init_begin) >> 10);
}

void
show_mem(void)
{
	long i, free = 0, total = 0, reserved = 0;
	long shared = 0, cached = 0;

	printk("\nMem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages << (PAGE_SHIFT - 10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map + i))
			reserved++;
		else if (PageSwapCache(mem_map + i))
			cached++;
		else if (!page_count(mem_map + i))
			free++;
		else
			shared += page_count(mem_map + i) - 1;
	}
	printk("%ld pages of RAM\n", total);
	printk("%ld free pages\n", free);
	printk("%ld reserved pages\n", reserved);
	printk("%ld pages shared\n", shared);
	printk("%ld pages swap cached\n", cached);
	printk("%ld pages in PGD cache\n", pgd_free_list.size);
}


#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif


/* First C code - started by head.S */
/* Copyright atp 1998-2001 under the GNU GPL */

#include <asm/rpb.h>
#include <asm/mv.h>
#include <linux/mm.h>

/* stuff that is declared in head.S */
extern unsigned long int phys_start;		/* physical address of kernel*/
extern unsigned long int virt_start;		/* virtual address of kernel */
extern unsigned long int boot_ap;		/* argument pointer */
extern unsigned long int boot_r11;		/* rpb pointer */
extern unsigned long int boot_scb;		/* scb pointer */
extern unsigned long int iomap_base;

/* head.S copies the RPB into this structure */
struct rpb_struct boot_rpb;

extern void start_kernel(void);
extern void guard_int_stack(void);  /* arch/vax/kernel/interrupt.c */
extern void enable_early_printk(void);

/*
 * This is a transitionary function. When things are finally sorted
 * the only tasks this function will perform will relate to the interaction
 * with VMB and other stuff that needs to go on early before we start_kernel()
 * like patchable control store, memory bitmap creation on non-ROM based
 * VAXen.
 *   At present its used for testing the early parts of the kernel startup.
 * The other main thing it does is load the rpb and scb global variables,
 * and switch on basic paging. The main paging setup is done later.
 *
 * ok ive changed my mind. We turn on MM in the asm before we hit C code
 *    (keeps stacks simpler) just like the i386, with a default 8mb system
 *    page table setup (with a 1:1 mapping of system space.
 *
 * Things that are temporary have a habit of becoming permanent.
 * I've renamed from tmp_start_kernel to vax_start_kernel, as convenient
 * bit of arch-specific C code before starting the main start_kernel
 *
 * atp aug 2001  - This is now permanent, and has been renamed to startup.c
 */

#define IOMAP_START (PAGE_OFFSET+((iomap_base-swapper_pg_dir[2].br)<<(PAGELET_SHIFT-2)))


void vax_start_kernel(void)
{
	/* Set the number of 4k pages */
	max_pfn = max_hwpfn / 8;

	/* Protect us from interrupt stack overflows */
	guard_int_stack();

	if (mv->post_vm_init)
		mv->post_vm_init();

#ifdef CONFIG_EARLY_PRINTK
	enable_early_printk();
#endif

	printk(KERN_INFO "Linux/VAX <linux-vax@pergamentum.com>\n");

#ifdef __SMP__
	{
		static int boot_cpu = 1;
		/* "current" has been set up, we need to load it now */
		if (!boot_cpu)
			initialize_secondary();
		boot_cpu = 0;
	}
#endif

	printk("RPB info: .l_pfncnt=0x%08x, .l_vmb_version=0x%08x, "
			".l_badpgs=0x%08x\n", boot_rpb.l_pfncnt,
			boot_rpb.l_vmb_version, boot_rpb.l_badpgs);

	printk("Physical memory: 0x%08x HW pagelets, 0x%08lx pages (%dKB)\n",
			max_hwpfn, max_pfn, max_hwpfn/2);

	printk("CPU type: %s, SID: 0x%08x, SIDEX: 0x%08x\n", mv->cpu_type_str(),
			__mfpr(PR_SID), mv->sidex);

	printk("VM: mapped physical from 0x%x to 0x%x, iomap from %lx\n",
			PAGE_OFFSET, PAGE_OFFSET + (max_hwpfn * 512),
			IOMAP_START);
	printk("VM: vmalloc from 0x%lx to 0x%lx\n", VMALLOC_START, VMALLOC_END);
	printk("VM: ptemap from 0x%lx to 0x%lx for %d processes\n",
			TASKPTE_START, TASKPTE_END, TASK_MAXUPRC);
	printk("Calling start_kernel()...\n\n");
	start_kernel();
}


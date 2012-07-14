/*
 *  Copyright (C) 1995  Linus Torvalds
 *  VAX port copyright  atp 1998.
 */

/*
 * Bootup setup stuff.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/root_dev.h>
#include <linux/notifier.h>

#include <asm/rpb.h>
#include <asm/page.h>
#include <asm/mv.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>


extern char *kernel_cmd_line;	/* Kernel command line from head.S */
char command_line[COMMAND_LINE_SIZE];

/* Defined in arch/vax/mm/init.c */
extern void paging_init(void);

/* Linker will put this at the end of the kernel image */
extern char _end;

struct vaxcpu vax_cpu;

/*
 *	Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	seq_printf(m, "cpu\t\t: VAX\n"
			"cpu type\t: %s\n"
			"cpu sid\t\t: 0x%08x\n"
			"cpu sidex\t: 0x%08x\n"
			"page size\t: %ld\n"
			"BogoMIPS\t: %lu.%02lu\n",
			(char *)mv->cpu_type_str(),
			__mfpr(PR_SID),
			mv->sidex,
			PAGE_SIZE,
			loops_per_jiffy / (500000/HZ),
			(loops_per_jiffy / (5000/HZ)) % 100);
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return (void*)(*pos == 0);
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start = c_start,
	.next = c_next,
	.stop = c_stop,
	.show = show_cpuinfo,
};

static int vax_panic_notifier(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	machine_halt();
        return NOTIFY_DONE;
}

static struct notifier_block vax_panic_block = {
	.notifier_call = vax_panic_notifier,
	.priority = INT_MIN /* may not return; must be done last */
};

void __init setup_arch(char **cmdline_p)
{
	unsigned long zones_size[MAX_NR_ZONES] = { 0, 0, 0 };
	unsigned int max_dma;
	unsigned long bootmap_size;
	unsigned long region_start;
	unsigned long region_len;

	notifier_chain_register(&panic_notifier_list, &vax_panic_block);

        /*
	 * Save the command line from the boot block, before it gets
	 * stomped on.
	 */
        memcpy(command_line, kernel_cmd_line,(COMMAND_LINE_SIZE-1));
	*cmdline_p = command_line;
	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, command_line, COMMAND_LINE_SIZE-1);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';
        printk("kernel_cmd_line %8p\n%s\n",kernel_cmd_line,kernel_cmd_line);

	/* Get the SID */
	vax_cpu.sid = __mfpr(PR_SID);

	/* We expand the system page table in paging_init, so
	 * it comes before the bootmem allocator. */
        paging_init();

        /* Initialize bootmem */

	/* We don't have any holes in our physical memory layout,
	   so we throw everything into the bootmem allocator.
           Eventually, we will get smarter and use the bad page lists
	   provided by the console ROM to map out faulty memory.
	   This also has the side effect of placing the bootmem bitmap
	   at the start of physical memory.  init_bootmem() also
	   marks every page as reserved.  We have to explicitly free
	   available memory ourselves.  (max_pfn comes from RPB.) */

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PAGEALIGNUP(x)	(((x) + PAGE_SIZE-1) & ~(PAGE_SIZE-1))
#define PAGEALIGNDN(x)	((x) & ~(PAGE_SIZE-1))
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)

        bootmap_size = init_bootmem(0, max_pfn);

        printk("bootmap size = %8.8lx\n", bootmap_size);

	/*
	 * Available memory is now the region from the end of the
	 * bootmem bitmap to the start of the kernel and from the
	 * end of the SPT to the end of memory
	 */
	region_start = PAGEALIGNUP(bootmap_size);
	region_len = PAGEALIGNDN(KERNEL_START_PHYS) - region_start;

	printk("Calling free_bootmem(start=%08lx, len=%08lx)\n",
			region_start, region_len);
	free_bootmem(region_start, region_len);

        region_start = PAGEALIGNUP(__pa(SPT_BASE + SPT_SIZE));
	region_len = PAGEALIGNDN((max_pfn << PAGE_SHIFT)) - region_start;

	printk("Calling free_bootmem(start=%08lx, len=%08lx)\n",
			region_start, region_len);
	free_bootmem(region_start, region_len);

        max_dma = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;

        /* max_pfn is the number of 4K PTEs */
        if (max_pfn < max_dma) {
		zones_size[ZONE_DMA] = max_pfn;
	} else {
		zones_size[ZONE_DMA] = max_dma;
		zones_size[ZONE_NORMAL] = max_pfn - max_dma;
	}

        free_area_init(zones_size);

	/*
	 * Set up the initial PCB.  We can refer to current because head.S
	 * has already set us up on the kernel stack of task 0.
	 */
	__mtpr(__pa(&current->thread.pcb), PR_PCBB);

	memset(&current->thread.pcb, 0, sizeof(current->thread.pcb));
	current->thread.pcb.astlvl = 4;

        /* swapper_pg_dir is a 4 x pgd_t array */
	SET_PAGE_DIR(current, swapper_pg_dir);

	/* No root filesystem yet */
	ROOT_DEV = Root_NFS;

        /*
	 * Inserted by D.A. - 8 Jun 2001 - THIS IS NECESSARY
	 * if not correct.
	 */
	flush_tlb();

	/*
	 * Identify the flock of penguins.
	 */

#ifdef __SMP__
	setup_smp();
#endif

#ifdef CONFIG_VT
#ifdef CONFIG_DUMMY_CONSOLE
	/*
	 * We need a dummy console up at cons_init time, otherwise there'll
	 * be no VTs allocated for the real fbdev console to later take over.
	 */
	conswitchp = &dummy_con;
#endif
#endif
}


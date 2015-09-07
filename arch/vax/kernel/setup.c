#include <linux/start_kernel.h>
#include <linux/seq_file.h>
#include <linux/init.h>

#include <asm/pgtable-bits.h>
#include <asm/sections.h>
#include <asm/console.h>
#include <asm/setup.h>
#include <asm/rpb.h>
#include <asm/io.h>

extern char command_line[256];

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long n = (unsigned long) v - 1;

	seq_printf(m, "processor\t\t: %ld\n", n);
	seq_printf(m, "\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	unsigned long i = *pos;

	return i < 1 ? (void *) (i + 1) : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

void __init setup_arch(char **cmdline_p)
{
	memcpy(boot_command_line, command_line, 256);

	*cmdline_p = boot_command_line;
}

static void *kernel_phys_start(void)
{
	return (void*) __pa(CONFIG_KERNEL_START);
}

static size_t kernel_size(void)
{
	return _end - _stext;
}

/*
 * Relocate kernel image to higher up in the memory and return address to the
 * start of the new image.
 */
void *__init vax_relocate_kernel(void *start, void *end)
{
	size_t size;
	void *dest;

	dest	= kernel_phys_start();
	size	= kernel_size();

	BUG_ON(dest < start);

	vax_printf("Relocating kernel from %lu to %lu (%lu bytes)\n", start, dest, size);

	memmove(dest, start, size);

	return dest;
}

void __init vax_setup_system_space(struct rpb_struct *rpb)
{
	unsigned long num_pfns = rpb->l_pfncnt;
	unsigned long pfn, *pgtable;

	pgtable = kernel_phys_start() + kernel_size();

	vax_printf("Mapping %lu pages to system space in page table at %lu ...\n", num_pfns, (unsigned long) pgtable);

	for (pfn = 0; pfn < num_pfns; pfn++) {
		pgtable[pfn] = _PAGE_VALID | _PAGE_KW | pfn;
	}

	mtpr((unsigned long) pgtable, IPR_SBR);
	mtpr(num_pfns, IPR_SLR);
}

void __init vax_start_kernel(void)
{
	vax_puts("Linux/VAX booting ...");

	register_console(&vax_console);

	start_kernel();
}

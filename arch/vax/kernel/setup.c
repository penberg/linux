#include <linux/start_kernel.h>
#include <linux/seq_file.h>
#include <linux/init.h>

#include <asm/console.h>
#include <asm/setup.h>
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

/*
 * Relocate kernel image to higher up in the memory and return address to the
 * start of the new image.
 */
void *__init vax_relocate_kernel(void *start, void *end)
{
	size_t size;
	void *dest;

	dest	= (void*) CONFIG_KERNEL_START-PAGE_OFFSET;
	size	= end-start;

	BUG_ON(dest < start);

	vax_printf("Relocating kernel from %lu to %lu (%lu bytes)\n", start, dest, size);

	memmove(dest, start, size);

	return dest;
}

void __init vax_start_kernel(void)
{
	vax_puts("Linux/VAX booting ...");

	start_kernel();
}

#include <linux/string.h>
#include <linux/init.h>

#include <asm/console.h>

static int __init setup_early_printk(char *buf)
{
	if (!buf || early_console)
		return 0;

	early_console = &vax_console;

	return 0;
}
early_param("earlyprintk", setup_early_printk);

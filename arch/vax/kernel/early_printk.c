/*
 * Rudimentary console driver for early printk output.
 * This depends on the CPU's machine vector having a working
 * post_vm_putchar().
 * If a CPU can support early printk, it should call one
 * of the init_early_printk_XXX() functions (at the bottom
 * of this file) from the mv->post_vm_init() function
 */

#include <linux/console.h>
#include <linux/init.h>
#include <asm/mv.h>

static int early_console_enabled;

static void early_console_write(struct console *cons, const char *p,
		unsigned int len)
{
	while (len--) {
		if (*p == '\n')
			mv->post_vm_putchar('\r');

		mv->post_vm_putchar(*p++);
	}
}

struct console early_console = {
	.name	= "VAXcons",
	.write	= early_console_write,
	.flags	= CON_PRINTBUFFER,
};

void __init enable_early_printk(void)
{
	if (!mv->post_vm_putchar)
		/* Cannot support early printk */
		return;

	early_console_enabled = 1;
	register_console(&early_console);

	printk("Early console enabled\n");
}

void __init disable_early_printk(void)
{
	if (early_console_enabled) {
		if (mv->keep_early_console)
			printk (KERN_WARNING "Not disabling early console "
					"because it's still needed!\n");
		else {
			printk (KERN_INFO "Disabling early console. If this "
					"is the last text you see, try to "
					"append \"console=ttyS0\" to the "
					"kernel command line\n");
			unregister_console(&early_console);
			early_console_enabled = 0;
		}
	}
}


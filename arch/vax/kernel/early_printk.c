#include <linux/console.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/ipr-index.h>
#include <asm/ipr.h>

static void vax_early_putchar(unsigned int c)
{
	while ((mfpr(IPR_TXCS) & 0x80) == 0)
		;;

	mtpr(c, IPR_TXDB);
}

static void vax_early_write(struct console *con, const char *s, unsigned n)
{
	while (*s && n-- > 0) {
		unsigned int c = *s;

		if (c == '\n')
			vax_early_putchar('\r');

		vax_early_putchar(c);

		s++;
	}
}

static struct console vax_early_console = {
	.name		= "VAXcons",
	.write		= vax_early_write,
	.flags		= CON_PRINTBUFFER | CON_BOOT,
	.index		= -1,
};

static int __init setup_early_printk(char *buf)
{
	if (!buf || early_console)
		return 0;

	early_console = &vax_early_console;

	return 0;
}
early_param("earlyprintk", setup_early_printk);

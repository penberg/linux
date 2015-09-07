#include <linux/kernel.h>

#include <asm/ipr-index.h>
#include <asm/console.h>
#include <asm/ipr.h>

void vax_putchar(unsigned int c)
{
	while ((mfpr(IPR_TXCS) & 0x80) == 0)
		;;

	mtpr(c, IPR_TXDB);
}

void vax_putstr(const char *s)
{
	while (*s) {
		unsigned int c = *s++;

		if (c == '\n')
			vax_putchar('\r');

		vax_putchar(c);
	}
}

void vax_puts(const char *s)
{
	vax_putstr(s);
	vax_putchar('\r');
	vax_putchar('\n');
}

int vax_printf(const char *fmt, ...)
{
        char printf_buf[64];
        va_list args;
        int printed;

        va_start(args, fmt);
        printed = vsprintf(printf_buf, fmt, args);
        va_end(args);

        vax_putstr(printf_buf);

        return printed;
}

static void vax_console_write(struct console *con, const char *s, unsigned n)
{
	while (*s && n-- > 0) {
		unsigned int c = *s;

		if (c == '\n')
			vax_putchar('\r');

		vax_putchar(c);

		s++;
	}
}

struct console vax_console = {
	.name		= "VAXcons",
	.write		= vax_console_write,
	.flags		= CON_PRINTBUFFER | CON_BOOT,
	.index		= -1,
};


#include <linux/init.h>
#include <linux/types.h>

#include <asm/mv.h>
#include <asm/mtpr.h>

#include "boot_sections.h"

void __boot boot_putchar(unsigned int c)
{
	mv->pre_vm_putchar(c);
}

void __boot boot_crlf(void)
{
	boot_putchar('\r');
	boot_putchar('\n');
}

void __boot boot_printspace(void)
{
	boot_putchar(' ');
}

void __boot boot_printstr(const char *s)
{
	while (*s) {
		boot_putchar(*s);
		s++;
	}
}

void __boot boot_printchar(unsigned int c)
{
	if ((c >= 0x20) && (c <= 0x7e)) {
		boot_putchar(c);
	} else {
		boot_putchar('.');
	}
}

void __boot boot_printint(unsigned int x)
{
	int i;
	int d;

	boot_putchar('0');
	boot_putchar('x');

	for (i = 28; i>= 0; i -= 4) {
		d = (x >> i) & 0x0f;
		if (d > 9) {
			boot_putchar(d - 10 + 'A');
		} else {
			boot_putchar(d + '0');
		}
	}
}



void * __boot boot_memset(void *s, int c, size_t count)
{
	char *xs = (char *) s;

	while (count--)
		*xs++ = c;

	return s;
}

void * __boot boot_memzero(void *s, size_t count)
{
	return boot_memset(s, 0, count);
}


void __boot boot_memmove(void *dest, const void *src, size_t count)
{
	char *d, *s;
	int *di;
	int *si;

	if (dest <= src) {
		si = (int *) src;
		di = (int *) dest;

		while (count & ~3) {
			*di++ = *si++;
			count -= 4;
		}
		d = (char *) di;
		s = (char *) si;

		if (count & 2) {
			*d++ = *s++;
			*d++ = *s++;
			count ++;
			count ++;
		}

		if (count & 1) {
			*d++ = *s++;
			count ++;
		}

	} else {
		d = (char *) dest + count;
		s = (char *) src + count;

		if (count & 1) {
			*--d = *--s;
			count--;
		}

		if (count & 2) {
			*--d = *--s;
			*--d = *--s;
			count--;
			count--;
		}

		si = (int *) s;
		di = (int *) d;
		while (count -= 4)
			*--di = *--si;
	}
}

void __boot boot_print_cpu_id(void)
{
	boot_printstr("CPU type: ");
	boot_printstr(mv->cpu_type_str());

	boot_printstr(" SID: ");
	boot_printint(__mfpr(PR_SID));

	boot_printstr(" SIDEX: ");
	boot_printint(mv->sidex);

	boot_crlf();
}


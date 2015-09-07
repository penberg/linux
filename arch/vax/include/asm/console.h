#ifndef _ASM_VAX_CONSOLE_H
#define _ASM_VAX_CONSOLE_H

#include <linux/console.h>

extern struct console vax_console;

void vax_putchar(unsigned int c);
void vax_puts(const char *s);
int vax_printf(const char *fmt, ...);

#endif /* _ASM_VAX_CONSOLE_H */

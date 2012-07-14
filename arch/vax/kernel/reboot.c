/*
 * This file contains the standard functions that the arch-independent
 * kernel expects for halting, rebooting and powering off the machine.
 * It also contains the machine check dispatcher.
 *
 * The real work will be done by cpu-specific code via the machine
 * vector.  Eventually...
 */

#include <linux/types.h>
#include <linux/reboot.h>

#include <asm/mv.h>
#include <asm/system.h>

extern void show_cpu_regs(void);

void machine_halt(void)
{
	if (!mv->halt) {
		printk("machine_halt: cpu-specific halt not implemented"
			" - HALTing\n");
		HALT;
		while (1)
			/* wait */;
	}

	mv->halt();
	while (1)
		/* wait */;
}

void machine_restart(char *cmd)
{
	if (!mv->reboot) {
		printk("machine_restart: cpu-specific reboot not implemented"
			" - HALTing\n");
		HALT;
	}

	mv->reboot();
	while (1)
		/* wait */;
}

void machine_power_off(void)
{
	if (!mv->halt) {
		printk("machine_power_off: cpu-specific halt not implemented"
			" - HALTing\n");
		HALT;
		while (1)
			/* wait */;
	}

	mv->halt();
	while (1)
		/* wait */;
}

/*
 * This is called directly, from entry.S
 * It checks for a cpu specific machine check handler and hands over to it.
 * Otherwise it will just halt, as there is no way to recover without a
 * sensible cpu specific routine
 */
void machine_check(void *stkframe)
{
	if (!mv->mcheck) {
		printk("Machine Check - CPU specific handler not implemented - halting\n");
		show_cpu_regs();
		machine_halt();
	}

	mv->mcheck(stkframe);
}


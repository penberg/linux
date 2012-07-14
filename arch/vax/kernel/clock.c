/* arch/vax/kernel/clock.c
 *
 * Copyright atp 2002. license: GPL
 *
 * Routines to manipulate the real time clock on VAXen.
 *
 * There are two sorts of battery backed hardware clocks. There is the
 * TODR (time of day register) found on big VAXen, and the familiar
 * Dallas CMOS clock on the desktop VAXen.
 *
 * The init routines are called through the machine vector. See
 * cpu_kaxx.c for details of that. The callers are time_init() and
 * the rtc clock driver (drivers/char/rtc.c), using macros defined
 * in asm/mc146818rtc.h.
 *
 * Prototypes for some of these functions are in asm/mc146818rtc.h
 * and some in asm/clock.h. (The ones that are used in the mv
 * initialisation are in clock.h, and the ones used in mc146818rtc.h
 * are in that file).
 *
 */

#include <linux/config.h>
#include <asm/io.h>		/* For ioremap() */
#include <asm/mtpr.h>
#include <asm/mv.h>
#include <asm/clock.h>		/* for TODR, if anyone feels like implementing it */
#include <asm/bus/vsbus.h>
#include <linux/mc146818rtc.h>	/* includes asm/mc146818rtc.h */
				/*  - needed for offsets in debug output */


/* this does nothing, and is a placeholder */
void generic_clock_init(void)
{
	printk (KERN_WARNING "No RTC used\n");
	return;
}

/* Map the ROM clock page, and put address in mv */
void ka4x_clock_init(void)
{
	mv->clock_base = ioremap(VSA_CLOCK_BASE, 1); /* 1 page */
	printk("Mapped RTC clock page (v %p p %08x )\n", mv->clock_base,
			VSA_CLOCK_BASE);

	printk("RTC date is %2.2d:%2.2d:%4.4d %2.2d:%2.2d:%2.2d\n",
			CMOS_READ(RTC_DAY_OF_MONTH), CMOS_READ(RTC_MONTH),
			CMOS_READ(RTC_YEAR), CMOS_READ(RTC_HOURS),
			CMOS_READ(RTC_MINUTES), CMOS_READ(RTC_SECONDS));

	return;
}

unsigned char ka4x_clock_read(unsigned long offset)
{
	if (mv->clock_base)
		return mv->clock_base[offset] >> 2;

	return 0;
}

void ka4x_clock_write(unsigned char val, unsigned long offset)
{
	if (mv->clock_base)
		mv->clock_base[offset] = val << 2;

	return;
}


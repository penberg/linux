/*
 * Copyright (C) 1995  Linus Torvalds
 * VAX port copyright  atp 1998.
 * (C) 2000  Erik Mouw <J.A.K.Mouw@its.tudelft.nl>
 *
 * 22-oct-2000: Erik Mouw
 *    Added some simple do_gettimeofday() and do_settimeofday()
 *    functions. Not tested due to lack of disk space.
 *
 * 24 Apr 2002: atp. Finally got round to doing this properly.
 *              We now use the CMOS clock.
 */

/*
 * Time handling on VAXen
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/bcd.h>
#include <linux/errno.h>

#include <asm/irq.h>
#include <asm/mtpr.h>
#include <asm/clock.h>
#include <asm/mv.h>
#include <linux/mc146818rtc.h>

u64 jiffies_64;

spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;
extern unsigned long wall_jiffies;	/* kernel/timer.c */

#define TICK_SIZE (tick_nsec / 1000)

/* last time the cmos clock got updated */
static long last_rtc_update;

/* protos */
static int set_rtc_mmss(unsigned long nowtime);
static irqreturn_t do_timer_interrupt(int vec_num, void *dev_id, struct pt_regs *regs);
static unsigned long do_gettimeoffset(void);
void time_init(void);
unsigned long get_cmos_time(void);

void __init time_init(void)
{
	/* Initialise the hardware clock */
	if (mv->clock_init) {
		printk (KERN_DEBUG "Calling mv->clock_init()\n");
		mv->clock_init();
	} else
		printk (KERN_DEBUG "No mv->clock_init(), so not calling it...\n");

	/* Read CMOS time */
	xtime.tv_nsec = 0;
	xtime.tv_sec = get_cmos_time();
	wall_to_monotonic.tv_sec = -xtime.tv_sec;
	wall_to_monotonic.tv_nsec = -xtime.tv_nsec;

	if (request_irq(0x30, do_timer_interrupt, 0, "timer", NULL)) {
		printk("Panic: unable to register timer interrupt handler\n");
		HALT;
	}

	/*
	 * Some VAX CPUs are hardwired to trigger interrupts at 100Hz,
	 * so we need to pay attention to HZ always being 100 for
	 * compatibility reasons. For all other machines, we need to
	 * supply a value (initial counter--an interrupt is triggered upon
	 * overflow while this value is incremented at a 1µs interval)
	 * to get more than one interrupt per hour:-)
	 */
	if (mv->nicr_required)
		__mtpr(0xffffffff - 1000000/HZ, PR_NICR);

	/* Set the clock ticking and enable clock interrupts */
	__mtpr(ICCS_ERROR | ICCS_INTERRUPT |	/* clear error and interrupt bits */
			ICCS_TRANSFER |		/* Load ICR from NICR */
			ICCS_INTENABLE |	/* enable interrupts... */
			ICCS_RUN, PR_ICCS);	/* ...and go */
}


/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 *
 * BUG: This routine does not handle hour overflow properly; it just
 *      sets the minutes. Usually you'll only notice that after reboot!
 */
static int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;

	/* gets recalled with irq locally disabled */
	spin_lock(&rtc_lock);
	save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being set */
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset prescaler */
	CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

	cmos_minutes = CMOS_READ(RTC_MINUTES);
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		BCD_TO_BIN(cmos_minutes);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
			BIN_TO_BCD(real_seconds);
			BIN_TO_BCD(real_minutes);
		}
		CMOS_WRITE(real_seconds,RTC_SECONDS);
		CMOS_WRITE(real_minutes,RTC_MINUTES);
	} else {
		printk(KERN_WARNING
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);
	spin_unlock(&rtc_lock);

	return retval;
}


/* This is the interrupt service routine for the timer interrupt */
static irqreturn_t do_timer_interrupt(int vec_num, void *dev_id, struct pt_regs *regs)
{
	unsigned int iccs;

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);

	iccs = __mfpr(PR_ICCS);
	if (iccs & ICCS_ERROR) {
		printk("Clock overrun\n");
	}

	do_timer(regs);

#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if ((time_status & STA_UNSYNC) == 0
			&& xtime.tv_sec > last_rtc_update + 660
			&& (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2
			&& (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}

	/*
	 * The VARM says we should do this in the clock ISR.  It isn't
	 * actually required on the KA650, as the ICCS register is
	 * not fully implemented.  But I don't know about the other
	 * CPUs yet
	 */
	__mtpr(ICCS_INTERRUPT |    /* Clear interrupt bit */
		ICCS_ERROR |       /* Clear error bit */
		ICCS_TRANSFER |    /* Reload ICR from NICR */
		ICCS_RUN,          /* ... and go again */
		PR_ICCS);

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

/*
 * Function to compensate the time offset caused by calling this
 * function (I think so, yes). This function definatively needs a real
 * implementation, but it works for now. -- Erik
 */
static unsigned long do_gettimeoffset(void)
{
	/* FIXME: do something useful over here */
	return 0;
}

/*
 * do_gettimeofday() and do_settimeofday()
 *
 * Looking at the ARM and i386 implementations, it is very well
 * possible that these functions are not correct, but without hardware
 * documentation I can't think of a way to make the proper
 * corrections -- Erik.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long seq;
	unsigned long usec, sec;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);

		usec = do_gettimeoffset();
		{
			unsigned long lost = jiffies - wall_jiffies;
			if (lost)
				usec += lost * (1000000 / HZ);
		}
		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

int do_settimeofday(struct timespec *tv)
{
	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC) {
		return -EINVAL;
	}

        write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	tv->tv_nsec -= do_gettimeoffset() * NSEC_PER_USEC;
	tv->tv_nsec -= (jiffies - wall_jiffies) * TICK_NSEC;

	while (tv->tv_nsec < 0) {
		tv->tv_nsec += NSEC_PER_SEC;
		tv->tv_sec--;
	}

	xtime.tv_sec = tv->tv_sec;
	xtime.tv_nsec = tv->tv_nsec;
	time_adjust = 0;                /* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_sequnlock_irq(&xtime_lock);

	return 0;
}

/* nicked from the i386 port, but we use the same chip, hee hee */
unsigned long get_cmos_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	int i;

	spin_lock(&rtc_lock);
	/* The Linux interpretation of the CMOS clock register contents:
	 * When the Update-In-Progress (UIP) flag goes from 1 to 0, the
	 * RTC registers show the second which has precisely just started.
	 * Let's hope other operating systems interpret the RTC the same way.
	 */
	/* read RTC exactly on falling edge of update flag */
	for (i = 0 ; i < 1000000 ; i++)	/* may take up to 1 second... */
		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP)
			break;
	for (i = 0 ; i < 1000000 ; i++)	/* must try at least 2.228 ms */
		if (!(CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP))
			break;
	do { /* Isn't this overkill ? UIP above should guarantee consistency */
		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);
	} while (sec != CMOS_READ(RTC_SECONDS));
	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BCD_TO_BIN(sec);
		BCD_TO_BIN(min);
		BCD_TO_BIN(hour);
		BCD_TO_BIN(day);
		BCD_TO_BIN(mon);
		BCD_TO_BIN(year);
	}
	spin_unlock(&rtc_lock);
	if ((year += 1900) < 1970)
		year += 100;
	return mktime(year, mon, day, hour, min, sec);
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies * (1000000000 / HZ);
}


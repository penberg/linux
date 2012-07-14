/*
 * dz.c: Serial port driver for DECStations & VAXstations equiped
 *       with the DZ chipset.
 *
 * Copyright (C) 1998 Olivier A. D. Lebaillif
 *
 * Email: olivier.lebaillif@ifrsys.com
 *
 * [31-AUG-98] triemer
 * Changed IRQ to use Harald's dec internals interrupts.h
 * removed base_addr code - moving address assignment to setup.c
 * Changed name of dz_init to rs_init to be consistent with tc code
 * [13-NOV-98] triemer fixed code to receive characters
 *    after patches by harald to irq code.
 * [09-JAN-99] triemer minor fix for schedule - due to removal of timeout
 *            field from "current" - somewhere between 2.1.121 and 2.1.131
 * [27-JUN-2001] Arnaldo Carvalho de Melo <acme@conectiva.com.br> - cleanups
 *
 * Parts (C) 1999 David Airlie, airlied@linux.ie
 * [07-SEP-99] Bugfixes
 */

/* #define DEBUG_DZ 1 */

#include <linux/module.h>

#include <linux/config.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/param.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/serialP.h>

#ifdef CONFIG_SERIO
#include <linux/serio.h>
#endif /* CONFIG_SERIO */

#include <asm/bus/vsbus.h>

/* for definition of struct console */
#ifdef CONFIG_SERIAL_CONSOLE
#define CONSOLE_LINE (3)
#endif /* ifdef CONFIG_SERIAL_CONSOLE */
#if defined(CONFIG_SERIAL_CONSOLE) || defined(DEBUG_DZ)
#include <linux/console.h>
#endif /* if defined(CONFIG_SERIAL_CONSOLE) || defined(DEBUG_DZ) */

#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <asm/uaccess.h>
#include <asm/irq.h>

#include <asm/mv.h>
#include <asm/dz11.h>

#ifdef DEBUG_DZ
#include <linux/ptrace.h>
#include <linux/fs.h>
extern int (*prom_printf) (char *,...);
#endif
#include "dz.h"

#define DZ_INTR_DEBUG 1

static struct dz_serial *lines[4];
static unsigned char tmp_buffer[256];

#ifdef DEBUG_DZ
/*
 * debugging code to send out chars via prom
 */
static void debug_console( const char *s,int count)
{
	unsigned i;

	for (i = 0; i < count; i++) {
		if (*s == 10)
			prom_printf("%c", 13);
		prom_printf("%c", *s++);
	}
}
#endif

/*
 * ------------------------------------------------------------
 * dz_in () and dz_out ()
 *
 * These routines are used to access the registers of the DZ
 * chip, hiding relocation differences between implementation.
 * ------------------------------------------------------------
 */

static inline unsigned short dz_in (struct dz_serial *info, unsigned offset)
{
	volatile u16 *addr = (volatile u16 *)(info->port + offset);

	return *addr;
}

static inline void dz_out (struct dz_serial *info, unsigned offset,
                           unsigned short value)
{
	volatile u16 *addr = (volatile u16 *)(info->port + offset);
	*addr = value;
}

/*
 * ------------------------------------------------------------
 * rs_stop () and rs_start ()
 *
 * These routines are called before setting or resetting
 * tty->stopped. They enable or disable transmitter interrupts,
 * as necessary.
 * ------------------------------------------------------------
 */

static void dz_stop (struct tty_struct *tty)
{
	struct dz_serial *info;
	unsigned short mask, tmp;

	if (!tty)
		return;

	info = (struct dz_serial *)tty->driver_data;

	mask = 1 << info->line;
	tmp = dz_in (info, DZ_TCR);       /* read the TX flag */

	tmp &= ~mask;                   /* clear the TX flag */
	dz_out (info, DZ_TCR, tmp);
}

static void dz_start (struct tty_struct *tty)
{
	struct dz_serial *info = (struct dz_serial *)tty->driver_data;
	unsigned short mask, tmp;

	mask = 1 << info->line;
	tmp = dz_in (info, DZ_TCR);      /* read the TX flag */

	tmp |= mask;                   /* set the TX flag */
	dz_out (info, DZ_TCR, tmp);
}

/*
 * ------------------------------------------------------------
 * Here starts the interrupt handling routines.  All of the
 * following subroutines are declared as inline and are folded
 * into dz_interrupt.  They were separated out for readability's
 * sake.
 *
 * Note: rs_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * rs_interrupt() should try to keep the interrupt handler as fast as
 * possible.  After you are done making modifications, it is not a bad
 * idea to do:
 *
 * gcc -S -DKERNEL -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer dz.c
 *
 * and look at the resulting assemble code in serial.s.
 *
 * ------------------------------------------------------------
 */

/*
 * ------------------------------------------------------------
 * dz_sched_event ()
 *
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 * ------------------------------------------------------------
 */
static inline void dz_sched_event (struct dz_serial *info, int event)
{
	info->event |= 1 << event;
	schedule_work(&info->tqueue);
}

/*
 * ------------------------------------------------------------
 * receive_char ()
 *
 * This routine deals with inputs from any lines.
 * ------------------------------------------------------------
 */
static inline void receive_chars (struct dz_serial *info_in)
{
	struct dz_serial *info;
	struct tty_struct *tty = 0;
	struct async_icount *icount;
	int ignore = 0;
	unsigned short status, tmp;
	unsigned char ch;

	/*
	 * This code is going to be a problem...  the call to tty_flip_buffer
	 * is going to need to be rethought...
	 */
	do {
		status = dz_in (info_in, DZ_RBUF);
		info = lines[LINE(status)];

		/* punt so we don't get duplicate characters */
		if (!(status & DZ_DVAL))
			goto ignore_char;

		ch = UCHAR(status);			/* grab the char */

#if 0
		if (info->is_console) {
			if (ch == 0)
				return;			/* it's a break ... */
		}
#endif

		tty = info->tty;	/* now tty points to the proper dev */
		icount = &info->icount;

		if (!tty)
			break;
		if (tty->flip.count >= TTY_FLIPBUF_SIZE) break;

		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = 0;
		icount->rx++;

		/* keep track of the statistics */
		if (status & (DZ_OERR | DZ_FERR | DZ_PERR)) {
			if (status & DZ_PERR)		/* parity error */
				icount->parity++;
			else if (status & DZ_FERR)	/* frame error */
				icount->frame++;
			if (status & DZ_OERR)		/* overrun error */
				icount->overrun++;

			/*
			 * Check to see if we should ignore the character and
			 * mask off conditions that should be ignored
			 */

			if (status & info->ignore_status_mask) {
				if (++ignore > 100)
					break;
				goto ignore_char;
			}

			/* mask off the error conditions we want to ignore */
			tmp = status & info->read_status_mask;

			if (tmp & DZ_PERR) {
				*tty->flip.flag_buf_ptr = TTY_PARITY;
#ifdef DEBUG_DZ
				debug_console("PERR\n",5);
#endif /* DEBUG_DZ */
			} else if (tmp & DZ_FERR) {
				*tty->flip.flag_buf_ptr = TTY_FRAME;
#ifdef DEBUG_DZ
				debug_console("FERR\n",5);
#endif /* DEBUG_DZ */
			} if (tmp & DZ_OERR) {
#ifdef DEBUG_DZ
				debug_console("OERR\n",5);
#endif /* DEBUG_DZ */
				if (tty->flip.count < TTY_FLIPBUF_SIZE) {
					tty->flip.count++;
					tty->flip.flag_buf_ptr++;
					tty->flip.char_buf_ptr++;
					*tty->flip.flag_buf_ptr = TTY_OVERRUN;
				}
			}
		}
	tty->flip.flag_buf_ptr++;
	tty->flip.char_buf_ptr++;
	tty->flip.count++;
ignore_char:
	;
	} while (status & DZ_DVAL);

	if (tty)
		tty_flip_buffer_push(tty);
}

/*
 * ------------------------------------------------------------
 * transmit_char ()
 *
 * This routine deals with outputs to any lines.
 * ------------------------------------------------------------
 */
static inline void transmit_chars (struct dz_serial *info)
{
	unsigned char tmp;

	if (info->x_char) {           /* XON/XOFF chars */
		dz_out(info, DZ_TDR, info->x_char);
		info->icount.tx++;
		info->x_char = 0;
		return;
	}

	/* if nothing to do or stopped or hardware stopped */
	if ((info->xmit_cnt <= 0) || info->tty->stopped ||
	    info->tty->hw_stopped) {
		dz_stop(info->tty);
		return;
	}

	/*
	 * If something to do ... (rember the dz has no output fifo so we go
	 * one char at a time :-<
	 */
	tmp = (unsigned short) info->xmit_buf[info->xmit_tail++];
	dz_out(info, DZ_TDR, tmp);
	info->xmit_tail = info->xmit_tail & (DZ_XMIT_SIZE - 1);
	info->icount.tx++;

	if (--info->xmit_cnt < WAKEUP_CHARS)
	dz_sched_event(info, DZ_EVENT_WRITE_WAKEUP);

	/* Are we done */
	if (info->xmit_cnt <= 0)
		dz_stop(info->tty);
}

/*
 * ------------------------------------------------------------
 * check_modem_status ()
 *
 * Only valid for the MODEM line duh !
 * ------------------------------------------------------------
 */
static inline void check_modem_status (struct dz_serial *info)
{
	unsigned short status;

	/* if not ne modem line just return */
	if (info->line != DZ_MODEM)
		return;

	status = dz_in(info, DZ_MSR);

	/* it's easy, since DSR2 is the only bit in the register */
	if (status)
		info->icount.dsr++;
}

/*
 * ------------------------------------------------------------
 * dz_interrupt ()
 *
 * this is the main interrupt routine for the DZ chip.
 * It deals with the multiple ports.
 * ------------------------------------------------------------
 */
/* VAX has separate RX/TX interrupts */

static irqreturn_t dz_interrupt_rx (int irq, void *dev, struct pt_regs *regs)
{
	struct dz_serial *info;
	unsigned short status;

	status = dz_in ((struct dz_serial *)dev, DZ_CSR); /* get the reason why we just got an irq */
	info = lines[LINE(status)];     /* re-arrange info the proper port */

	if (status & DZ_RDONE)
		receive_chars (info);          /* the receive function */

	return IRQ_HANDLED;
}

static irqreturn_t dz_interrupt_tx (int irq, void *dev, struct pt_regs *regs)
{
	struct dz_serial *info;
	unsigned short status;

	status = dz_in ((struct dz_serial *)dev, DZ_CSR); /* get the reason why we just got an irq */
	info = lines[LINE(status)];     /* re-arrange info the proper port */

	if (status & DZ_TRDY)
		transmit_chars (info);

	return IRQ_HANDLED;
}

/*
 * -------------------------------------------------------------------
 * Here ends the DZ interrupt routines.
 * -------------------------------------------------------------------
 */

static void do_softint (void *private_data)
{
	struct dz_serial *info = (struct dz_serial *) private_data;
	struct tty_struct *tty = info->tty;

	if (!tty)
		return;

	if (test_and_clear_bit(DZ_EVENT_WRITE_WAKEUP, &info->event)) {
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup) (tty);
		wake_up_interruptible (&tty->write_wait);
	}
}

/*
 * -------------------------------------------------------------------
 * This routine is called from the scheduler tqueue when the interrupt
 * routine has signalled that a hangup has occurred.  The path of
 * hangup processing is:
 *
 *      serial interrupt routine -> (scheduler tqueue) ->
 *      do_serial_hangup() -> tty->hangup() -> rs_hangup()
 * -------------------------------------------------------------------
 */
static void do_serial_hangup (void *private_data)
{
	struct dz_serial *info = (struct dz_serial *) private_data;
	struct tty_struct *tty = info->tty;

	if (!tty)
		return;

	tty_hangup(tty);
}

/*
 * -------------------------------------------------------------------
 * startup ()
 *
 * various initialization tasks
 * -------------------------------------------------------------------
 */
static int startup (struct dz_serial *info)
{
	unsigned long page, flags;
	unsigned short tmp;

	if (info->is_initialized)
		return 0;

	save_and_cli(flags);

	if (!info->port) {
		if (info->tty) set_bit (TTY_IO_ERROR, &info->tty->flags);
		restore_flags (flags);
		return -ENODEV;
	}

	if (!info->xmit_buf) {
		page = get_zeroed_page(GFP_KERNEL);
		if (!page) {
			restore_flags (flags);
		return -ENOMEM;
		}
		info->xmit_buf = (unsigned char *)page;
	}

	if (info->tty)
		clear_bit (TTY_IO_ERROR, &info->tty->flags);

	/* enable the interrupt and the scanning */
	tmp = dz_in (info, DZ_CSR);
	tmp |= (DZ_RIE | DZ_TIE | DZ_MSE);
	dz_out (info, DZ_CSR, tmp);

	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	change_speed (info);			/* set up the speed */

	/*
	 * Clear the line transmitter buffer I can't figure out why I need to
	 * do this - but its necessary - in order for the console portion and
	 * the interrupt portion to live happily side by side.
	 */

	info->is_initialized = 1;

	restore_flags (flags);

	return 0;
}

/*
 * -------------------------------------------------------------------
 * shutdown ()
 *
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 * -------------------------------------------------------------------
 */
static void shutdown (struct dz_serial *info)
{
	unsigned long flags;
	unsigned short tmp;

	if (!info->is_initialized)
		return;

	save_and_cli(flags);

	dz_stop (info->tty);

	info->cflags &= ~DZ_CREAD;	/* turn off receive enable flag */
	dz_out(info, DZ_LPR, info->cflags);

	if (info->xmit_buf) {               /* free Tx buffer */
		free_page((unsigned long)info->xmit_buf);
		info->xmit_buf = 0;
	}

	if (!info->tty || (info->tty->termios->c_cflag & HUPCL)) {
		tmp = dz_in(info, DZ_TCR);
		if (tmp & DZ_MODEM_DTR) {
			tmp &= ~DZ_MODEM_DTR;
			dz_out(info, DZ_TCR, tmp);
		}
	}

	if (info->tty)
		set_bit (TTY_IO_ERROR, &info->tty->flags);

	info->is_initialized = 0;

	restore_flags (flags);
}

/*
 * -------------------------------------------------------------------
 * change_speed ()
 *
 * set the baud rate.
 * -------------------------------------------------------------------
 */
static void change_speed (struct dz_serial *info)
{
	unsigned long flags;
	unsigned cflag;
	int baud;

	if (!info->tty || !info->tty->termios)
		return;

	save_and_cli(flags);

	info->cflags = info->line;

	cflag = info->tty->termios->c_cflag;

	switch (cflag & CSIZE) {
		case CS5:
			info->cflags |= DZ_CS5;
			break;
		case CS6:
			info->cflags |= DZ_CS6;
			break;
		case CS7:
			info->cflags |= DZ_CS7;
			break;
		case CS8:
		default:
			info->cflags |= DZ_CS8;
	}

	if (cflag & CSTOPB)
		info->cflags |= DZ_CSTOPB;
	if (cflag & PARENB)
		info->cflags |= DZ_PARENB;
	if (cflag & PARODD)
		info->cflags |= DZ_PARODD;

	baud = tty_get_baud_rate(info->tty);
	switch (baud) {
	case 50:
		info->cflags |= DZ_B50;
		break;
	case 75:
		info->cflags |= DZ_B75;
		break;
	case 110:
		info->cflags |= DZ_B110;
		break;
	case 134:
		info->cflags |= DZ_B134;
		break;
	case 150:
		info->cflags |= DZ_B150;
		break;
	case 300:
		info->cflags |= DZ_B300;
		break;
	case 600:
		info->cflags |= DZ_B600;
		break;
	case 1200:
		info->cflags |= DZ_B1200;
		break;
	case 1800:
		info->cflags |= DZ_B1800;
		break;
	case 2000:
		info->cflags |= DZ_B2000;
		break;
	case 2400:
		info->cflags |= DZ_B2400;
		break;
	case 3600:
		info->cflags |= DZ_B3600;
		break;
	case 4800:
		info->cflags |= DZ_B4800;
		break;
	case 7200:
		info->cflags |= DZ_B7200;
		break;
	case 9600:
	default:
		info->cflags |= DZ_B9600;
	}

	info->cflags |= DZ_RXENAB;
	dz_out(info, DZ_LPR, info->cflags);

	/* setup accept flag */
	info->read_status_mask = DZ_OERR;
	if (I_INPCK(info->tty))
		info->read_status_mask |= (DZ_FERR | DZ_PERR);

	/* characters to ignore */
	info->ignore_status_mask = 0;
	if (I_IGNPAR(info->tty))
		info->ignore_status_mask |= (DZ_FERR | DZ_PERR);

	restore_flags(flags);
}

/*
 * -------------------------------------------------------------------
 * dz_flush_char ()
 *
 * Flush the buffer.
 * -------------------------------------------------------------------
 */
static void dz_flush_chars (struct tty_struct *tty)
{
	struct dz_serial *info = (struct dz_serial *)tty->driver_data;
	unsigned long flags;

	if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !info->xmit_buf)
		return;

	save_and_cli(flags);
	dz_start (info->tty);
	restore_flags(flags);
}


/*
 * -------------------------------------------------------------------
 * dz_write ()
 *
 * main output routine.
 * -------------------------------------------------------------------
 */
static int dz_write (struct tty_struct *tty,
                     const unsigned char *buf, int count)
{
	struct dz_serial *info;
	unsigned long flags;
	int c, ret = 0;

	if (!tty)
		return ret;
	info = (struct dz_serial *) tty->driver_data;

	if (!info->xmit_buf)
		return ret;
	if (!tmp_buf)
		tmp_buf = tmp_buffer;

	while (1) {
		save_and_cli(flags);

		c = MIN(count, MIN(DZ_XMIT_SIZE - info->xmit_cnt - 1,
		                   DZ_XMIT_SIZE - info->xmit_head));
		if (c <= 0) {
			restore_flags (flags);
			break;
		}
		memcpy(info->xmit_buf + info->xmit_head, buf, c);
		info->xmit_head = ((info->xmit_head + c) & (DZ_XMIT_SIZE-1));
		info->xmit_cnt += c;
		restore_flags(flags);

		buf += c;
		count -= c;
		ret += c;
	}

	if (info->xmit_cnt) {
		if (!tty->stopped) {
			if (!tty->hw_stopped) {
				dz_start (info->tty);
			}
		}
	}

	return ret;
}

/*
 * -------------------------------------------------------------------
 * dz_write_room ()
 *
 * compute the amount of space available for writing.
 * -------------------------------------------------------------------
 */
static int dz_write_room (struct tty_struct *tty)
{
	struct dz_serial *info = (struct dz_serial *)tty->driver_data;
	int ret;

	ret = DZ_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;

	return ret;
}

/*
 * -------------------------------------------------------------------
 * dz_chars_in_buffer ()
 *
 * compute the amount of char left to be transmitted
 * -------------------------------------------------------------------
 */
static int dz_chars_in_buffer (struct tty_struct *tty)
{
	struct dz_serial *info = (struct dz_serial *)tty->driver_data;

	return info->xmit_cnt;
}

/*
 * -------------------------------------------------------------------
 * dz_flush_buffer ()
 *
 * Empty the output buffer
 * -------------------------------------------------------------------
 */
static void dz_flush_buffer (struct tty_struct *tty)
{
	struct dz_serial *info = (struct dz_serial *)tty->driver_data;

	cli();
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	sti();

	wake_up_interruptible (&tty->write_wait);

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	     tty->ldisc.write_wakeup)
		tty->ldisc.write_wakeup(tty);
}

/*
 * ------------------------------------------------------------
 * dz_throttle () and dz_unthrottle ()
 *
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled (or not).
 * ------------------------------------------------------------
 */
static void dz_throttle (struct tty_struct *tty)
{
	struct dz_serial *info = (struct dz_serial *)tty->driver_data;

	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);
}

static void dz_unthrottle (struct tty_struct *tty)
{
	struct dz_serial *info = (struct dz_serial *)tty->driver_data;

	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}
}

static void dz_send_xchar (struct tty_struct *tty, char ch)
{
	struct dz_serial *info = (struct dz_serial *)tty->driver_data;

	info->x_char = ch;

	if (ch)
		dz_start(info->tty);
}

/*
 * ------------------------------------------------------------
 * rs_ioctl () and friends
 * ------------------------------------------------------------
 */
static int get_serial_info(struct dz_serial *info,
                           struct serial_struct *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;

	memset (&tmp, 0, sizeof(tmp));

	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = info->port;
	tmp.irq = 0;
	tmp.flags = info->flags;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;

	return copy_to_user(retinfo, &tmp, sizeof(*retinfo)) ? -EFAULT : 0;
}

static int set_serial_info (struct dz_serial *info,
                            struct serial_struct *new_info)
{
	struct serial_struct new_serial;
	struct dz_serial old_info;
	int retval = 0;

	if (!new_info)
		return -EFAULT;

	if (copy_from_user(&new_serial, new_info, sizeof(new_serial)))
		return -EFAULT;

	old_info = *info;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (info->count > 1)
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	info->baud_base = new_serial.baud_base;
	info->type = new_serial.type;
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;

	retval = startup(info);

	return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *          is emptied.  On bus types like RS485, the transmitter must
 *          release the bus after transmitting. This must be done when
 *          the transmit shift register is empty, not be done when the
 *          transmit holding register is empty.  This functionality
 *          allows an RS485 driver to be written in user space.
 */
static int get_lsr_info (struct dz_serial *info, unsigned int *value)
{
	unsigned short status = dz_in (info, DZ_LPR);

	return put_user (status, value);
}

/*
 * This routine sends a break character out the serial port.
 */
static void send_break (struct dz_serial *info, int duration)
{
	unsigned long flags;
	unsigned short tmp, mask;

	if (!info->port)
		return;

	mask = 1 << info->line;
	tmp = dz_in (info, DZ_TCR);
	tmp |= mask;

	current->state = TASK_INTERRUPTIBLE;

	save_and_cli(flags);
	dz_out(info, DZ_TCR, tmp);
	schedule_timeout(duration);
	tmp &= ~mask;
	dz_out(info, DZ_TCR, tmp);
	restore_flags(flags);
}

static int dz_ioctl(struct tty_struct *tty, struct file *file,
                    unsigned int cmd, unsigned long arg)
{
	struct dz_serial * info = (struct dz_serial *)tty->driver_data;
	int retval;

	if (cmd != TIOCGSERIAL && cmd != TIOCSSERIAL &&
	    cmd != TIOCSERCONFIG && cmd != TIOCSERGWILD  &&
	    cmd != TIOCSERSWILD && cmd != TIOCSERGSTRUCT) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}

	switch (cmd) {
	case TCSBRK:		/* SVID version: non-zero arg --> no break */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		if (!arg)
			send_break(info, HZ/4); /* 1/4 second */
		return 0;

	case TCSBRKP:		/* support for POSIX tcsendbreak() */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		send_break(info, arg ? arg*(HZ/10) : HZ/4);
		return 0;

	case TIOCGSOFTCAR:
		return put_user(C_CLOCAL(tty) ? 1 : 0, (unsigned long *)arg);

	case TIOCSSOFTCAR:
		if (get_user (arg, (unsigned long *)arg))
			return -EFAULT;

		tty->termios->c_cflag = (tty->termios->c_cflag & ~CLOCAL) |
		                        (arg ? CLOCAL : 0);
		return 0;

	case TIOCGSERIAL:
		return get_serial_info(info, (struct serial_struct *)arg);

	case TIOCSSERIAL:
		return set_serial_info(info, (struct serial_struct *) arg);

	case TIOCSERGETLSR:		/* Get line status register */
		return get_lsr_info (info, (unsigned int *)arg);

	case TIOCSERGSTRUCT:
		return copy_to_user((struct dz_serial *)arg, info,
		                    sizeof(struct dz_serial)) ? -EFAULT : 0;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static void dz_set_termios (struct tty_struct *tty,
			    struct termios *old_termios)
{
	struct dz_serial *info = (struct dz_serial *)tty->driver_data;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;

	change_speed (info);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		dz_start(tty);
	}
}

/*
 * ------------------------------------------------------------
 * dz_close()
 *
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we turn off
 * the transmit enable and receive enable flags.
 * ------------------------------------------------------------
 */
static void dz_close(struct tty_struct *tty, struct file *filp)
{
	struct dz_serial * info = (struct dz_serial *)tty->driver_data;
	unsigned long flags;

	if (!info)
		return;

	save_and_cli(flags);

	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
		return;
	}

	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty structure
		 * will be freed.  Info->count should always be one in these
		 * conditions.  If it's greater than one, we've got real
		 * problems, since it means the serial port won't be shutdown.
		 */
		printk("dz_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}

	if (--info->count < 0) {
		printk("ds_close: bad serial port count for ttyS%02d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}

	if (info->count) {
		restore_flags(flags);
		return;
	}
	info->flags |= DZ_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify the line
	 * discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;

	if (info->closing_wait != DZ_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);

	/*
	 * At this point we stop accepting input.  To do this, we disable the
	 * receive line status interrupts.
	 */
	shutdown(info);

	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer (tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer (tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;

	tty_ldisc_flush(tty);

	if (info->blocked_open) {
		if (info->close_delay) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(info->close_delay);
		}
		wake_up_interruptible(&info->open_wait);
	}

	info->flags &= ~(DZ_NORMAL_ACTIVE | DZ_CLOSING);
	wake_up_interruptible(&info->close_wait);

	restore_flags(flags);
}

/*
 * dz_hangup () --- called by tty_hangup() when a hangup is signaled.
 */
static void dz_hangup (struct tty_struct *tty)
{
	struct dz_serial *info = (struct dz_serial *) tty->driver_data;

	dz_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~DZ_NORMAL_ACTIVE;
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * rs_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file *filp,
                           struct dz_serial *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int retval;
	int do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & DZ_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
		return -EAGAIN;
	}

	/*
	 * If non-blocking mode is set, or the port is not enabled, then make
	 * the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		info->flags |= DZ_NORMAL_ACTIVE;

		return 0;
	}

	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;

	/*
	 * Block waiting for the carrier detect and the line to become free
	 * (i.e., not in use by the callout).  While we are in this loop,
	 * info->count is dropped by one, so that dz_close() knows when to free
	 * things.  We restore it upon exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);

	info->count--;
	info->blocked_open++;
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p (filp) || !(info->is_initialized)) {
			retval = -EAGAIN;
			break;
		}
		if (!(info->flags & DZ_CLOSING) && do_clocal)
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue (&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;

	if (retval)
		return retval;
	info->flags |= DZ_NORMAL_ACTIVE;
	return 0;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port. It also performs the
 * serial-specific initialization for the tty structure.
 */
static int dz_open (struct tty_struct *tty, struct file *filp)
{
	struct dz_serial *info;
	int retval, line;

	line = tty->index;

	if ((line < 0) || (line >= DZ_NB_PORT))
		return -ENODEV;

	info = lines[line];
	info->count++;

	tty->driver_data = info;
	info->tty = tty;

	/*
	 * Start up serial port
	 */
	retval = startup (info);
	if (retval)
		return retval;

	retval = block_til_ready (tty, filp, info);
	if (retval)
		return retval;

	return 0;
}

static void show_serial_version (void)
{
	printk("%s%s\n", dz_name, dz_version);
}

#ifdef CONFIG_SERIO
static spinlock_t dz_serio_lock = SPIN_LOCK_UNLOCKED;

static int
dz_serio_write (struct serio *serio, unsigned char one_byte)
{
	struct dz_serial *info = serio->port_data;
	unsigned long flags;

	spin_lock_irqsave (&dz_serio_lock, flags);
	dz_write (info->tty, &one_byte, sizeof (one_byte));
	spin_unlock_irqrestore (&dz_serio_lock, flags);

	return 0;
}

static int
dz_serio_open (struct serio *serio)
{
	struct dz_serial *info = serio->port_data;
	unsigned long flags;
	int ret;

	spin_lock_irqsave (&dz_serio_lock, flags);
	if (info->serio_opened == 0) {
		info->serio_opened = 1;
		ret = 0;
	} else
		ret = -EBUSY;
	spin_unlock_irqrestore (&dz_serio_lock, flags);

	return ret;
}

static void
dz_serio_close (struct serio *serio)
{
	struct dz_serial *info = serio->port_data;
	unsigned long flags;

	spin_lock_irqsave (&dz_serio_lock, flags);
	info->serio_opened = 0;
	spin_unlock_irqrestore (&dz_serio_lock, flags);
}
#endif /* CONFIG_SERIO */

static struct tty_driver *serial_driver;

static struct tty_operations serial_ops = {
	.open = dz_open,
	.close = dz_close,
	.write = dz_write,
	.flush_chars = dz_flush_chars,
	.write_room = dz_write_room,
	.chars_in_buffer = dz_chars_in_buffer,
	.flush_buffer = dz_flush_buffer,
	.ioctl = dz_ioctl,
	.throttle = dz_throttle,
	.unthrottle = dz_unthrottle,
	.send_xchar = dz_send_xchar,
	.set_termios = dz_set_termios,
	.stop = dz_stop,
	.start = dz_start,
	.hangup = dz_hangup,
};

static int __init dz_probe(struct vsbus_device *vsbus_dev)
{
	int i, flags;
	unsigned long base_addr;
	struct dz_serial *info;

        printk("dz_probe: name = %s, base = 0x%08x, irqindex = %d\n",
                vsbus_dev->dev.bus_id, vsbus_dev->phys_base, vsbus_dev->vsbus_irq);

	serial_driver = alloc_tty_driver(DZ_NB_PORT);
	if (!serial_driver)
		return -ENOMEM;

	show_serial_version();

	serial_driver->owner = THIS_MODULE;
	serial_driver->devfs_name = "tts/";
	serial_driver->name = "ttyS";
	serial_driver->major = TTY_MAJOR;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL |
	                                     CLOCAL;
	serial_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	tty_set_operations(serial_driver, &serial_ops);

	if (tty_register_driver(serial_driver))
		panic("Couldn't register serial driver\n");

	/* FIXME: check this for NULL */
	base_addr = (unsigned long) ioremap(vsbus_dev->phys_base, 16);

	local_irq_save(flags);
	for (i=0; i < DZ_NB_PORT;  i++) {
		info = &multi[i];
		lines[i] = info;
		info->magic = SERIAL_MAGIC;

		info->port = base_addr;
		info->line = i;
		info->tty = 0;
		info->close_delay = 50;
		info->closing_wait = 3000;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		INIT_WORK(&info->tqueue, do_softint, info);
		INIT_WORK(&info->tqueue_hangup, do_serial_hangup, info);
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);

		/*
		 * If we are pointing to address zero then punt - not correctly
		 * set up in setup.c to handle this.
		 */
		if (! info->port)
			return 0;

		printk("ttyS%02d at 0x%04x\n", info->line, info->port);

		tty_register_device(serial_driver, info->line, NULL);

#ifdef CONFIG_SERIO
		memset (&info->serio, 0, sizeof (struct serio));
		info->serio.port_data = info;
		info->serio.id.type = SERIO_RS232;

		switch (i) {
			case DZ_KEYBOARD:
				info->serio.id.type |= SERIO_LKKBD;
				sprintf (info->serio.name, "dzkbd");
				sprintf (info->serio.phys, "dz/line0");
				break;
			case DZ_MOUSE:
				info->serio.id.type |= SERIO_VSXXXAA;
				sprintf (info->serio.name, "dzmouse");
				sprintf (info->serio.phys, "dz/line1");
				break;
			case DZ_MODEM:
				sprintf (info->serio.name, "dz");
				sprintf (info->serio.phys, "dz/line2");
				break;
			case DZ_PRINTER:
				sprintf (info->serio.name, "dz");
				sprintf (info->serio.phys, "dz/line3");
				break;
		}

		info->serio.open = dz_serio_open;
		info->serio.close = dz_serio_close;
		info->serio.write = dz_serio_write;

		serio_register_port (&info->serio);
#endif /* CONFIG_SERIO */
	}

	/* Reset the chip */
#ifndef CONFIG_SERIAL_CONSOLE
	{
		int tmp;
		dz_out(info, DZ_CSR, DZ_CLR);
		while ((tmp = dz_in(info,DZ_CSR)) & DZ_CLR);

		/* Enable scanning */
		dz_out(info, DZ_CSR, DZ_MSE);
	}
#endif

	/*
	 * Order matters here... the trick is that flags is updated... in
	 * request_irq - to immediatedly obliterate it is unwise.
	 */
	local_irq_restore(flags);

	/* The bus-specific IRQ we are handed in the vsbus_dev
	 * structure is the TX interrupt.  The RX is always the
	 * next one up.
	 */
	printk("dz.c: using irq rx %d(bus %d), irq tx %d(bus %d)\n",
		vsbus_irqindex_to_irq(vsbus_dev->vsbus_irq + 1),
		vsbus_dev->vsbus_irq + 1,
		vsbus_irqindex_to_irq(vsbus_dev->vsbus_irq),
		vsbus_dev->vsbus_irq);

	if (vsbus_request_irq (vsbus_dev->vsbus_irq, dz_interrupt_tx, SA_INTERRUPT, "DZ-TX", lines[0]))
		panic ("Unable to register DZ TX interrupt\n");
	if (vsbus_request_irq (vsbus_dev->vsbus_irq + 1, dz_interrupt_rx, SA_INTERRUPT, "DZ-RX", lines[0]))
		panic ("Unable to register DZ RX interrupt\n");

	/* enable scanning */
	dz_out(info, DZ_CSR, DZ_MSE);

	return 0;
}

#ifdef CONFIG_SERIAL_CONSOLE

static void dz_console_put_char (unsigned char ch)
{
	unsigned long flags;
	int  loops = 2500;
	unsigned short tmp = ch;
        unsigned short tcr, mask;
	/*
	 * this code sends stuff out to serial device - spinning its wheels and
	 * waiting.
	 */

	/* force the issue - point it at lines[3]*/
	dz_console = &multi[CONSOLE_LINE];

	save_and_cli(flags);

	mask = 1 << dz_console->line;
	tcr = dz_in (dz_console, DZ_TCR);       /* read the TX flag */
	dz_out(dz_console, DZ_TCR, mask);

	/* spin our wheels */
	while (((dz_in(dz_console, DZ_CSR) & DZ_TRDY) != DZ_TRDY) &&  loops--)
		;

	/* Actually transmit the character. */
	dz_out(dz_console, DZ_TDR, tmp);

	/* spin our wheels */
	while (((dz_in(dz_console,DZ_CSR) & DZ_TRDY) != DZ_TRDY) &&  loops--)
		;
	dz_out(dz_console, DZ_TCR, tcr);

	restore_flags(flags);
}

/*
 * -------------------------------------------------------------------
 * dz_console_print ()
 *
 * dz_console_print is registered for printk.
 * The console must be locked when we get here.
 * -------------------------------------------------------------------
 */
static void dz_console_print (struct console *cons,
			      const char *str,
			      unsigned int count)
{
#ifdef DEBUG_DZ
	prom_printf((char *)str);
#endif
	while (count--) {
		if (*str == '\n')
			dz_console_put_char('\r');
		dz_console_put_char(*str++);
	}
}

static struct tty_driver *dz_console_device(struct console *c, int *index)
{
	*index = c->index;
	return serial_driver;
}

static int __init dz_console_setup(struct console *co, char *options)
{
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int cflag = CREAD | HUPCL | CLOCAL;
	char *s;
	unsigned short mask,tmp;

	if (options) {
		baud = simple_strtoul(options, NULL, 10);
		s = options;
		while (*s >= '0' && *s <= '9')
			s++;
		if (*s)
			parity = *s++;
		if (*s)
			bits   = *s - '0';
	}

	/*
	 * Now construct a cflag setting.
	 */
	switch (baud) {
	case 1200:
		cflag |= DZ_B1200;
		break;
	case 2400:
		cflag |= DZ_B2400;
		break;
	case 4800:
		cflag |= DZ_B4800;
		break;
	case 9600:
	default:
		cflag |= DZ_B9600;
		break;
	}
	switch (bits) {
	case 7:
		cflag |= DZ_CS7;
		break;
	default:
	case 8:
		cflag |= DZ_CS8;
		break;
	}
	switch(parity) {
	case 'o':
	case 'O':
		cflag |= DZ_PARODD;
		break;
	case 'e':
	case 'E':
		cflag |= DZ_PARENB;
		break;
	}
	co->cflag = cflag;

	/* TOFIX: force to console line */
	dz_console = &multi[CONSOLE_LINE];
	dz_console->port = (unsigned long)dz11_addr;
	dz_console->line = CONSOLE_LINE;

	/* This line locks up Dave Airlie's VS3100m38 after HALT */
	/* dz_out(dz_console, DZ_CSR, DZ_CLR); */

	while ((tmp = dz_in(dz_console,DZ_CSR)) & DZ_CLR)
		;

	/* enable scanning */
	dz_out(dz_console, DZ_CSR, DZ_MSE);

        /*  Set up flags... */
	dz_console->cflags = 0;
	dz_console->cflags |= DZ_B9600;
	dz_console->cflags |= DZ_CS8;
	dz_console->cflags |= DZ_PARENB;
	dz_out (dz_console, DZ_LPR, dz_console->cflags);

	mask = 1 << dz_console->line;
	tmp = dz_in (dz_console, DZ_TCR);       /* read the TX flag */
	if (!(tmp & mask)) {
		tmp |= mask;                   /* set the TX flag */
		dz_out (dz_console, DZ_TCR, tmp);
	}

	return 0;
}

static struct console dz_sercons = {
	.name	= "ttyS",
	.write	= dz_console_print,
	.device	= dz_console_device,
	.setup	= dz_console_setup,
	.flags	= CON_CONSDEV,
	.index	= CONSOLE_LINE,
};

void __init dz_serial_console_init(void)
{
	register_console(&dz_sercons);
}

#endif /* ifdef CONFIG_SERIAL_CONSOLE */

static struct vsbus_driver dz_driver = {
	.probe	= dz_probe,
	.drv	= {
		.name	 = "dz",
	},
};

static int __init dz_init_new(void)
{
	return vsbus_register_driver(&dz_driver);
}

module_init(dz_init_new);

MODULE_LICENSE("GPL");


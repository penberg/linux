/*
 * UART driver for the internal console port in most VAX CPUs
 *
 * Most VAX CPU implementations have a serial console port which
 * can be driven via 4 internal processor registers (IPRs), without
 * having to care about the underlying hardware implementation.
 * The are very simple devices, without modem control and no variable
 * baud rates or character formats.
 *
 * This driver is derived from the ARM AMBA serial driver by
 * <rmk@arm.linux.org.uk>
 *
 * BTW - this driver doesn't actually work yet.  I don't get any
 * boot-time output in SIMH - KPH 2003-10-13
 */

#include <linux/config.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#include <asm/mtpr.h>

/* FIXME: this should go into serial_core.h */
#define PORT_VAX_IPR 100

/* Need to think about major/minor numbers for this driver */
#define SERIAL_VAX_IPR_MAJOR       4
#define SERIAL_VAX_IPR_MINOR       64

/* Register definitions */

#define PR_RXCS_RDY   0x0080
#define PR_RXCS_IE    0x0040

#define PR_RXDB_ERROR 0x8000
#define PR_RXDB_ID    0x0f00
#define PR_RXDB_DATA  0x00ff

#define PR_TXCS_RDY   0x0080
#define PR_TXCS_IE    0x0040

#define PR_TXDB_ID    0x0f00
#define PR_TXDB_DATA  0x00ff

/* These vectors are defined by the VAX Architecture Reference Manual */
#define IPRCONS_RX_VECTOR 0x3e
#define IPRCONS_TX_VECTOR 0x3f

/*******************************************************************
 *
 * First we have the hardware handling code
 *
 */

static inline void iprcons_enable_rx_interrupts(void)
{
	__mtpr(PR_RXCS_IE, PR_RXCS);
}

static inline void iprcons_disable_rx_interrupts(void)
{
	__mtpr(0, PR_RXCS);
}

static inline void iprcons_enable_tx_interrupts(void)
{
	__mtpr(PR_TXCS_IE, PR_TXCS);
}

static inline void iprcons_disable_tx_interrupts(void)
{
	__mtpr(0, PR_TXCS);
}

static inline void iprcons_rx_char(struct uart_port *port,
	unsigned int rxcs, unsigned int rxdb)
{
	struct tty_struct *tty = port->info->tty;
	unsigned char ch = rxdb & PR_RXDB_DATA;

	if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
		tty->flip.work.func((void *)tty);
		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			printk(KERN_WARNING "iprcons_rx_char: TTY_DONT_FLIP set\n");
			return;
		}
	}

	*tty->flip.char_buf_ptr = ch;
	*tty->flip.flag_buf_ptr = TTY_NORMAL;
	port->icount.rx++;

	/* FIXME: properly record receive errors signalled in RXCS */

	if (port->ignore_status_mask == 0) {
		tty->flip.flag_buf_ptr++;
		tty->flip.char_buf_ptr++;
		tty->flip.count++;
	}

	tty_flip_buffer_push(tty);
}

static irqreturn_t iprcons_rx_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	unsigned int rxcs = __mfpr(PR_RXCS);
	unsigned int rxdb = __mfpr(PR_RXDB);
	struct uart_port *port = dev;

	if (rxcs & PR_RXCS_RDY) {
		iprcons_rx_char(port, rxcs, rxdb);
	}

	return IRQ_HANDLED;
}

static inline void iprcons_tx_char(unsigned int c)
{
	__mtpr(c, PR_TXDB);
}

static inline void iprcons_tx_ready(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	if (port->x_char) {
		iprcons_tx_char(port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		return;
	}

	iprcons_tx_char(xmit->buf[xmit->tail]);

	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	port->icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static irqreturn_t iprcons_tx_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	unsigned int txcs = __mfpr(PR_TXCS);
	struct uart_port *port = dev;

	if (txcs & PR_TXCS_RDY) {
		iprcons_tx_ready(port);
	}

	return IRQ_HANDLED;
}


/*******************************************************************
 *
 * Next comes the plumbing to hook us into the serial core
 *
 */

static void iprcons_stop_tx(struct uart_port *port, unsigned int tty_stop)
{
	/* Nothing to do - our "FIFO" is only 1 character deep */
}

static void iprcons_start_tx(struct uart_port *port, unsigned int tty_start)
{
	iprcons_disable_tx_interrupts();
	iprcons_enable_tx_interrupts();
}

static void iprcons_stop_rx(struct uart_port *port)
{
	/* Nothing to do - our "FIFO" is only 1 character deep */
}

static void iprcons_enable_ms(struct uart_port *port)
{
	/* Nothing to do - no modem control lines */
}

static unsigned int iprcons_tx_empty(struct uart_port *port)
{
	if (__mfpr(PR_TXCS) & PR_TXCS_RDY) {
		return TIOCSER_TEMT;
	} else {
		return 0;
	}
}

static unsigned int iprcons_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_CTS | TIOCM_DSR;
}

static void iprcons_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* Nothing to do - no modem control lines */
}

static void iprcons_break_ctl(struct uart_port *port, int break_state)
{
	/* Cannot generate BREAK */
}

static int iprcons_startup(struct uart_port *port)
{
	unsigned int retval;

	retval = request_irq(IPRCONS_TX_VECTOR, iprcons_tx_interrupt, 0, "iprcons-tx", port);
	if (retval) {
		printk("iprcons: unable to acquire TX interrupt vector\n");
	} else {
		retval = request_irq(IPRCONS_RX_VECTOR, iprcons_rx_interrupt, 0, "iprcons-rx", port);
		if (retval) {
			free_irq(IPRCONS_TX_VECTOR, port);
			printk("iprcons: unable to acquire RX interrupt vector\n");
		}
	}

	if (!retval) {
		iprcons_enable_rx_interrupts();
		iprcons_enable_tx_interrupts();
	}

	return retval;
}


static void iprcons_shutdown(struct uart_port *port)
{
	iprcons_disable_rx_interrupts();
	iprcons_disable_tx_interrupts();

	free_irq(IPRCONS_RX_VECTOR, port);
	free_irq(IPRCONS_TX_VECTOR, port);
}

static void iprcons_set_termios(struct uart_port *port, struct termios *termios,
	struct termios *old)
{
	/* This port is not software configurable.  It is fixed in
	   hardware to 9600, 8 bits, no parity, one stop bit.
	   (Actually - not completely true.  The KA650 console has a
	   physical rotary switch for selecting the baud rate.  But
	   we'll ignore this for now. */

	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, CS8, 9600);

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	port->ignore_status_mask = 0;
	if ((termios->c_cflag & CREAD) == 0) {
		port->ignore_status_mask = 1;
	}

	spin_unlock_irqrestore(&port->lock, flags);
}


static const char *iprcons_type(struct uart_port *port)
{
	if (port->type == PORT_VAX_IPR) {
		return "VAX CPU Console";
	} else {
		return NULL;
	}
}

static void iprcons_release_port(struct uart_port *port)
{
	/* No memory or IO regions used */
}

static int iprcons_request_port(struct uart_port *port)
{
	/* No memory or IO regions used */
	return 0;
}

/*
 * Configure/autoconfigure the port.
 */
static void iprcons_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_VAX_IPR;
	}
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).  We don't let the
 * user attempt to change IRQ or baud rate.
 */
static int iprcons_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_VAX_IPR)
		ret = -EINVAL;
	if (ser->irq != IPRCONS_RX_VECTOR)
		ret = -EINVAL;
	if (ser->baud_base != 9600)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops iprcons_pops = {
	.tx_empty	= iprcons_tx_empty,
	.set_mctrl	= iprcons_set_mctrl,
	.get_mctrl	= iprcons_get_mctrl,
	.stop_tx	= iprcons_stop_tx,
	.start_tx	= iprcons_start_tx,
	.stop_rx	= iprcons_stop_rx,
	.enable_ms	= iprcons_enable_ms,
	.break_ctl	= iprcons_break_ctl,
	.startup	= iprcons_startup,
	.shutdown	= iprcons_shutdown,
	.set_termios	= iprcons_set_termios,
	.type		= iprcons_type,
	.release_port	= iprcons_release_port,
	.request_port	= iprcons_request_port,
	.config_port	= iprcons_config_port,
	.verify_port	= iprcons_verify_port,
};

static struct uart_port iprcons_port = {
	.membase	= 0,
	.mapbase	= 0,
	.iotype		= SERIAL_IO_PORT,
	.irq		= IPRCONS_RX_VECTOR,
	.uartclk	= 0,
	.fifosize	= 1,
	.ops		= &iprcons_pops,
	.flags		= 0,
	.line		= 0,
	.type		= PORT_VAX_IPR,
};

static struct uart_driver iprcons_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "ttyS",
	.dev_name	= "ttyS",
	.major		= SERIAL_VAX_IPR_MAJOR,
	.minor		= SERIAL_VAX_IPR_MINOR,
	.nr		= 1,
};

#ifdef CONFIG_SERIAL_CONSOLE

static void iprcons_console_write(struct console *co, const char *p, unsigned int count)
{
	unsigned int old_inten_rx;
	unsigned int old_inten_tx;

	/*
	 *	First save the interrupt enable flag, then disable interrupts
	 */

	old_inten_rx = __mfpr(PR_RXCS) & PR_RXCS_IE;
	old_inten_tx = __mfpr(PR_TXCS) & PR_TXCS_IE;

	iprcons_disable_rx_interrupts();
	iprcons_disable_tx_interrupts();

	/*
	 *	Now, do each character
	 */
	while (count--) {

		/* Ensure bits 31..8 are all 0 */
		unsigned int c = *p++;

		while ((__mfpr(PR_TXCS) & PR_TXCS_RDY) == 0) {
			/* Busy wait */
		}

		__mtpr(c, PR_TXDB)

		if (c == '\n') {
			while ((__mfpr(PR_TXCS) & PR_TXCS_RDY) == 0) {
				/* Busy wait */
			}
			__mtpr('\r', PR_TXDB)
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the interrupt enables
	 */
	while ((__mfpr(PR_TXCS) & PR_TXCS_RDY) == 0) {
		/* Busy wait */
	}

	__mtpr(old_inten_rx, PR_RXCS);
	__mtpr(old_inten_tx, PR_TXCS);
}

static struct console iprcons_console = {
	.name	= "ttyS",
	.write	= iprcons_console_write,
	.device	= uart_console_device,
	.index	= -1,
	.data	= &iprcons_uart_driver,
};

static int __init iprcons_console_init(void)
{
	iprcons_uart_driver.cons = &iprcons_console;
	register_console(&iprcons_console);
	return 0;
}
console_initcall(iprcons_console_init);

#endif /* CONFIG_SERIAL_CONSOLE */

static void __exit iprcons_exit(void)
{
	/*
	 * FIXME: this is probably very broken.  How should
	 * we handled module removal with the driver model
	 * and the serial core involved?
	 */
	uart_remove_one_port(&iprcons_uart_driver, &iprcons_port);
	uart_unregister_driver(&iprcons_uart_driver);
}

static int __init iprcons_probe(struct device *busdev)
{
	int ret;

	printk(KERN_INFO "Serial: VAX IPR CPU console driver $Revision: 1.11 $\n");

	/*
	 * We are a platform device.  We'll only get probed if
	 * the per-cpu init code registers a platform device called
	 * 'iprcons'.  So it's safe to go ahead and register the
	 * UART driver here without checking the presence of any
	 * hardware.
	 */

	ret = uart_register_driver(&iprcons_uart_driver);
	if (ret == 0) {
		uart_add_one_port(&iprcons_uart_driver, &iprcons_port);
	}
	return ret;
}

static struct device_driver iprcons_driver = {
	.name	= "iprcons",
	.bus	= &platform_bus_type,
	.probe	= iprcons_probe,
};

static int __init iprcons_init(void)
{
	return driver_register(&iprcons_driver);
}

module_init(iprcons_init);
module_exit(iprcons_exit);

MODULE_AUTHOR("Kenn Humborg <kenn@linux.ie>");
MODULE_DESCRIPTION("VAX IPR CPU Console Driver $Revision: 1.11 $");
MODULE_LICENSE("GPL");


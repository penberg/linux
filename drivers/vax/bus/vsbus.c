/*
 * Support for the VSBUS pseudo-bus type.
 *
 * As far as I can make out, VAXstation 3100, MicroVAX 3100 and
 * VAXstation 4000 series machines have a chunk of bus interface
 * circuitry between the main CPU-memory bus and the on-board
 * peripheral chips (ethernet controller, SCSI controller, etc).
 * I imagine that this 'bus adapter' is responsible for mapping
 * device interrupt lines to VAX SCB vectors, doing address
 * line decoding to locate these devices in I/O space and
 * provide DMA facilities to main memory.
 *
 * It would be real nice to see a datasheet or tech manual
 * for one of these boards.
 *
 * This file implements the drivel model 'bus type' for the VSBUS
 * and the common features of all the VSBUS implementations.
 *
 * Differences in logic due to differences in the hardware are in
 * vsbus-ka*.c
 */

#include <linux/init.h>
#include <linux/device.h>

#include <asm/io.h>
#include <asm/bus/vsbus.h>

#define VSBUS_DEBUG 1

static struct vsbus_registers __iomem *vs_cpu_ptr;

static unsigned int vsbus_rom_vectors[VSBUS_NR_IRQS];

int init_vsbus_adapter(unsigned int *vectors, unsigned long registers)
{
	if (vs_cpu_ptr) {
		printk("vsbus: already initialized\n");
		return -EBUSY;
	}

	memcpy(vsbus_rom_vectors, vectors, VSBUS_NR_IRQS * sizeof(unsigned int));

	vs_cpu_ptr = ioremap(registers, 0x80);
	if (!vs_cpu_ptr) {
		return -EAGAIN;
	}

	return 0;
}

/* Interrupt vector handling on VSBUS devices is a bit unusual (for
   a VAX).  There are up to 8 interrupt sources.  Each source has
   a bit in the INTREQ/INTCLR register and the INTMSK register.

   Bit 7 is the highest priority interrupt, bit 0 is the lowest.
   The assignment of bits to devices varies from model to model.

   In order for interrupts from a device to be delivered to the VAX
   CPU, the relevant bit in INTMSK must be set.  When the hardware
   device requests an interrupt, the relevant bit in INTREQ is set.
   If (INTMSK & INTREQ) is non-zero, an interrupt is delivered to
   the CPU.  When the CPU acknowledges this, it expects to be fed
   an interrupt vector on the data bus.  (This interrupt vector is
   then used to index into the SCB to find the interrupt handler.)

   During the interrupt acknowledge cycle, the hardware finds the
   highest bit set in (INTMSK & INTREQ) and generates a read from
   the firmware ROM at address 0x20040020 + (bit_num * 4).  This
   causes the ROM to place the longword at that address on the data
   bus, which the CPU picks up as the interrupt vector.

   So, in summary, the firmware ROM contains an 8-longword table
   at physical address 0x20040020, which contains the interrupt
   vectors.  The hardware-specific driver fills in this table. */


void vsbus_enable_int(int bit_nr)
{
	vs_cpu_ptr->vc_intmsk |= 1<<bit_nr;
}

void vsbus_clear_int(int bit_nr)
{
	vs_cpu_ptr->vc_intclr = 1<<bit_nr;
}

void vsbus_disable_int(int bit_nr)
{
	vs_cpu_ptr->vc_intmsk &= ~(1<<bit_nr);
}

unsigned int vsbus_irqindex_to_irq(unsigned int irqindex)
{
	if (irqindex < VSBUS_NR_IRQS) {
		return vsbus_rom_vectors[irqindex] >> 2;
	} else {
		return 0;
	}
}

struct vsbus_irqinfo {
        irqreturn_t	(*handler)(int, void *, struct pt_regs *);
	unsigned int	irqindex;
	void *		dev_id;
};

static struct vsbus_irqinfo irqinfo[VSBUS_NR_IRQS];

static irqreturn_t vsbus_irq_handler(int irq, void *data, struct pt_regs *regs)
{
	struct vsbus_irqinfo *info = (struct vsbus_irqinfo *)data;

	vsbus_clear_int(info->irqindex);
	return info->handler(irq, info->dev_id, regs);
}

int vsbus_request_irq(unsigned int vsbus_irqindex,
        irqreturn_t (*handler)(int, void *, struct pt_regs *),
        unsigned long irqflags, const char *devname, void *dev_id)
{
	struct vsbus_irqinfo *info;
	int irq;
	int retval;

	if (vsbus_irqindex >= VSBUS_NR_IRQS) {
		return -EINVAL;
	}

	info = irqinfo + vsbus_irqindex;

	/* FIXME: need a semaphore here */

	if (info->handler) {
		return -EBUSY;
	}

	info->handler = handler;
	info->dev_id = dev_id;
	info->irqindex = vsbus_irqindex;

	irq = vsbus_irqindex_to_irq(info->irqindex);

	retval = request_irq(irq, vsbus_irq_handler, irqflags, devname, info);
	if (!retval) {
		vsbus_clear_int(vsbus_irqindex);
		vsbus_enable_int(vsbus_irqindex);
	} else {
		info->handler = NULL;
	}

	return retval;
}

void vsbus_free_irq(unsigned int vsbus_irqindex)
{
	struct vsbus_irqinfo *info;
	int irq;

	if (vsbus_irqindex >= VSBUS_NR_IRQS) {
		return;
	}

	info = irqinfo + vsbus_irqindex;

	/* FIXME: need a semaphore here */

	if (info->handler) {
		vsbus_disable_int(vsbus_irqindex);

		irq = vsbus_irqindex_to_irq(info->irqindex);
		free_irq(irq, info);

		/* FIXME: do we need to synchronize with this interrupt? */

		info->handler = NULL;
	}
}


void vsbus_add_fixed_device(struct device *parent, char *name,
	unsigned int phys_base, unsigned int irqindex)
{
	struct vsbus_device *vsbus_dev;

	vsbus_dev = kmalloc(sizeof(*vsbus_dev), GFP_KERNEL);
	if (vsbus_dev == NULL) {
		printk("vsbus_add_fixed_device: cannot allocate "
			"device structure for addr 0x%08x irqindex %d\n", phys_base, irqindex);
		return;
	}

	memset(vsbus_dev, 0, sizeof(*vsbus_dev));

	vsbus_dev->phys_base = phys_base;
	vsbus_dev->vsbus_irq = irqindex;
	vsbus_dev->dev.bus = &vsbus_bus_type;
	vsbus_dev->dev.parent = parent;

	snprintf(vsbus_dev->dev.bus_id, sizeof(vsbus_dev->dev.bus_id),
		"%s", name);

	vsbus_register_device(vsbus_dev);
}



/* These functions support the VSBUS bus type for the driver model */

static int vsbus_drv_remove(struct device *dev)
{
	struct vsbus_device *vsbus_dev = VSBUS_DEV(dev);
	struct vsbus_driver *vsbus_drv = VSBUS_DRV(dev->driver);

	vsbus_drv->remove(vsbus_dev);

	return 0;
}

int vsbus_register_device(struct vsbus_device *vsbus_dev)
{
	return device_register(&vsbus_dev->dev);
}

int vsbus_register_driver(struct vsbus_driver *drv)
{
	drv->drv.bus = &vsbus_bus_type;
	drv->drv.remove = vsbus_drv_remove;

	return driver_register(&drv->drv);
}


void vsbus_unregister_driver(struct vsbus_driver *drv)
{
	return driver_unregister(&drv->drv);
}

/* This gets called for each device when a new driver is
   registered.  Since the set of devices that can appear on
   the VSBUS is very limited, we can get away with a very simple
   name-based match. */

int vsbus_bus_match(struct device *dev, struct device_driver *drv)
{
	struct vsbus_device *vsbus_dev = VSBUS_DEV(dev);
	struct vsbus_driver *vsbus_drv = VSBUS_DRV(drv);

#if VSBUS_DEBUG
	printk("vsbus_match: called dev %s, drv %s\n", dev->bus_id, drv->name);
#endif

	if (!strncmp(dev->bus_id, drv->name, strlen(drv->name)) &&
		(vsbus_drv->probe(vsbus_dev) == 0)) {

		/* Found a driver that is willing to handle this device */
		return 1;
	}

	return 0;
}

struct bus_type vsbus_bus_type = {
	.name  = "vsbus",
	.match = vsbus_bus_match,
};

static int __init vsbus_bus_init(void)
{
	return bus_register(&vsbus_bus_type);
}

postcore_initcall(vsbus_bus_init);


EXPORT_SYMBOL(vsbus_register_driver);
EXPORT_SYMBOL(vsbus_unregister_driver);
EXPORT_SYMBOL(vsbus_bus_type);


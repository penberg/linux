/*
 * Support for the QBUS bus type.  This represents the common
 * features of all QBUS devices, for the purposes of the driver
 * model.
 *
 * Note that there is no support for actual QBUS bus adapters
 * (such as the CQBIC - the adapter that connects a CVAX chip
 * to a QBUS).  The QBUS bus type is not a driver.  Individual
 * bus adapters have their own fully-fledged drivers
 *
 */

#include <linux/init.h>
#include <linux/device.h>

#include <asm/bus/qbus.h>

#define QBUS_DEBUG 0


/* These DMA, memory mapping and IRQ handling functions isolate
   drivers for QBUS devices from the details of how the QBUS
   is hooked up to the rest of the system */

struct vax_dmamap *qbus_alloc_mapregs(struct device *busdev, void *start, unsigned int len)
{
	struct qbus_ops *ops = *(struct qbus_ops **)busdev->driver_data;
	return ops->dma_map(busdev, start, len);
}

void qbus_unmap(struct device *busdev, struct vax_dmamap *map)
{
	struct qbus_ops *ops = *(struct qbus_ops **)busdev->driver_data;
	ops->dma_unmap(busdev, map);
}

void qbus_dumpmap(struct device *busdev)
{
	struct qbus_ops *ops = *(struct qbus_ops **)busdev->driver_data;
	ops->dma_dumpmap(busdev);
}

int qbus_vector_to_irq(struct device *busdev, unsigned int vector)
{
	struct qbus_ops *ops = *(struct qbus_ops **)busdev->driver_data;
	return ops->vector_to_irq(busdev, vector);
}

int qbus_request_irq(struct device *busdev, unsigned int vector,
	irqreturn_t (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags,
	const char * devname,
	void *dev_id)
{
	struct qbus_ops *ops = *(struct qbus_ops **)busdev->driver_data;
	return ops->request_irq(busdev, vector, handler, irqflags, devname, dev_id);
}

unsigned int qbus_reserve_vector(struct device *busdev, unsigned int vector)
{
	struct qbus_ops *ops = *(struct qbus_ops **)busdev->driver_data;
	return ops->reserve_vector(busdev, vector);
}

unsigned int qbus_alloc_vector(struct device *busdev)
{
	struct qbus_ops *ops = *(struct qbus_ops **)busdev->driver_data;
	return ops->alloc_vector(busdev);
}

void qbus_free_vector(struct device *busdev, unsigned int vector)
{
	struct qbus_ops *ops = *(struct qbus_ops **)busdev->driver_data;
	ops->free_vector(busdev, vector);
}

void *qbus_ioremap(struct device *busdev, unsigned int bus_addr, unsigned int size)
{
	struct qbus_ops *ops = *(struct qbus_ops **)busdev->driver_data;
	return ops->ioremap(busdev, bus_addr, size);
}



/* These functions support the QBUS device type for the driver model */

static ssize_t qbus_show_csr(struct device *dev, char *buf)
{
	struct qbus_device *qbus_dev = QBUS_DEV(dev);
	return sprintf(buf, "0x%04x\n", qbus_dev->csr);
}

static DEVICE_ATTR(csr, S_IRUGO, qbus_show_csr, NULL);

static ssize_t qbus_show_csr_octal(struct device *dev, char *buf)
{
	struct qbus_device *qbus_dev = QBUS_DEV(dev);
	return sprintf(buf, "%o\n", QBUS_OCTAL_CSR(qbus_dev->csr));
}

static DEVICE_ATTR(csr_octal, S_IRUGO, qbus_show_csr_octal, NULL);


/* These functions support the QBUS bus type for the driver model */

int qbus_register_device(struct qbus_device *qbus_dev)
{
	int error = device_register(&qbus_dev->dev);

	if (!error) {
	        device_create_file(&qbus_dev->dev, &dev_attr_csr);
	}

	if (!error) {
	        device_create_file(&qbus_dev->dev, &dev_attr_csr_octal);
	}
	return error;
}

int qbus_register_driver(struct qbus_driver *drv)
{
	drv->drv.bus = &qbus_bus_type;

	return driver_register(&drv->drv);
}

void qbus_unregister_driver(struct qbus_driver *drv)
{
	return driver_unregister(&drv->drv);
}

/* This gets called for each device when a new driver is
   registered */

int qbus_bus_match(struct device *dev, struct device_driver *drv)
{
	struct qbus_device *qbus_dev = QBUS_DEV(dev);
	struct qbus_driver *qbus_drv = QBUS_DRV(drv);

#if QBUS_DEBUG
	printk("qbus_match: called dev %p, CSR %o, drv %p\n", dev, QBUS_OCTAL_CSR(qbus_dev->csr), drv);
#endif

	if (qbus_drv->probe &&
		(qbus_drv->probe(qbus_dev) == 0)) {

		/* Found a driver that is willing to handle this device */
		return 1;
	}

	return 0;
}

struct bus_type qbus_bus_type = {
	.name  = "qbus",
	.match = qbus_bus_match,
};

static int __init qbus_bus_init(void)
{
	return bus_register(&qbus_bus_type);
}

postcore_initcall(qbus_bus_init);


EXPORT_SYMBOL(qbus_register_driver);
EXPORT_SYMBOL(qbus_unregister_driver);
EXPORT_SYMBOL(qbus_bus_type);


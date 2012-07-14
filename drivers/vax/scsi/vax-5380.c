/*
 * Driver for NCR5380 SCSI controller on KA42 and KA43 CPU boards.
 *
 * Copyright 2000, 2004 Kenn Humborg <kenn@linux.ie>
 *
 * Based on ARM SCSI drivers by Russell King
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>

#include <asm/bus/vsbus.h>

#include "../../scsi/scsi.h"
#include <scsi/scsi_host.h>

/* See NCR5380.c for the options that can be set */
#define AUTOSENSE

#define NCR5380_implementation_fields \
	unsigned volatile char *base

#define NCR5380_local_declare() \
	unsigned volatile char *base

#define NCR5380_setup(instance) \
	base = (unsigned volatile char *)((instance)->base)

#define VAX_5380_address(reg) (base + ((reg) * 0x04))

#if !(VDEBUG & VDEBUG_TRANSFER)
#define NCR5380_read(reg) (*(VAX_5380_address(reg)))
#define NCR5380_write(reg, value) (*(VAX_5380_address(reg)) = (value))
#else
#define NCR5380_read(reg)                                               \
	(((unsigned char) printk("scsi%d : read register %d at address %08x\n"\
	, instance->hostno, (reg), VAX_5380_address(reg))), *(VAX_5380_address(reg)))

#define NCR5380_write(reg, value) {                                     \
	printk("scsi%d : write %02x to register %d at address %08x\n",      \
		instance->hostno, (value), (reg), VAX_5380_address(reg));   \
	*(VAX_5380_address(reg)) = (value);                                 \
}
#endif


#include "../../scsi/NCR5380.h"
#include "../../scsi/NCR5380.c"


const char *vax_5380_info (struct Scsi_Host *spnt)
{
        return "";
}

static Scsi_Host_Template vax_5380_template = {
	.name				= "VAXstation 3100/MicroVAX 3100 NCR5380 SCSI",
	.info				= vax_5380_info,
	.queuecommand			= NCR5380_queue_command,
	.eh_abort_handler		= NCR5380_abort,
	.eh_bus_reset_handler		= NCR5380_bus_reset,
	.eh_device_reset_handler	= NCR5380_device_reset,
	.eh_host_reset_handler		= NCR5380_host_reset,
	.can_queue			= 32,
	.this_id			= 6,
	.sg_tablesize			= SG_ALL,
	.cmd_per_lun			= 2,
	.use_clustering			= DISABLE_CLUSTERING,
	.proc_name			= "vax-5380",
	.proc_info			= NCR5380_proc_info,
};

static int __init
vax_5380_probe(struct vsbus_device *vsbus_dev)
{
	struct Scsi_Host *host;
	int retval = -ENOMEM;

	printk("vax_5380_probe: name = %s, base = 0x%08x, irqindex = %d\n",
		vsbus_dev->dev.bus_id, vsbus_dev->phys_base, vsbus_dev->vsbus_irq);

	host = scsi_host_alloc(&vax_5380_template, sizeof(struct NCR5380_hostdata));
	if (!host)
		goto out;

	host->base = (unsigned long) ioremap(vsbus_dev->phys_base, 0x80);
	if (!host->base)
		goto out_unreg;

	NCR5380_init(host, 0);

	host->irq = vsbus_dev->vsbus_irq;

	retval = vsbus_request_irq(host->irq, NCR5380_intr, SA_INTERRUPT, "vax-5380", host);
	if (retval) {
                printk("scsi%d: IRQ%d not free: %d\n",
                    host->host_no, host->irq, retval);
                goto out_iounmap;
	}

	printk("scsi%d: at virt 0x%08lx VSBUS irq %d",
		host->host_no, host->base, host->irq);
	printk(" options CAN_QUEUE=%d  CMD_PER_LUN=%d",
		host->can_queue, host->cmd_per_lun);
	printk("\nscsi%d:", host->host_no);
	NCR5380_print_options(host);
	printk("\n");

	retval = scsi_add_host(host, &vsbus_dev->dev);
	if (retval)
		goto out_free_irq;

	scsi_scan_host(host);
	goto out;

out_free_irq:
	vsbus_free_irq(host->irq);
out_iounmap:
	iounmap((void *)host->base);
out_unreg:
	scsi_host_put(host);
out:
	return retval;
}

static void __devexit vax_5380_remove(struct vsbus_device *vsbus_dev)
{
	struct Scsi_Host *host = dev_get_drvdata(&vsbus_dev->dev);

	scsi_remove_host(host);

	vsbus_free_irq(host->irq);
	NCR5380_exit(host);
	iounmap((void *)host->base);

	scsi_host_put(host);
}

static struct vsbus_driver vax_5380_driver = {
	.probe          = vax_5380_probe,
	.remove		= __devexit_p(vax_5380_remove),
	.drv = {
                .name   = "vax-5380",
        },
};

static int __init vax_5380_init(void)
{
	return vsbus_register_driver(&vax_5380_driver);
}

static void __exit vax_5380_exit(void)
{
	vsbus_unregister_driver(&vax_5380_driver);
}

module_init(vax_5380_init);
module_exit(vax_5380_exit);

MODULE_AUTHOR("Kenn Humborg");
MODULE_DESCRIPTION("VAX NCR5380 SCSI driver for KA42,KA43");
MODULE_LICENSE("GPL");


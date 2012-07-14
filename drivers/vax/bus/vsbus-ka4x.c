/*
 * Support for the VSBUS pseudo bus adapter in the KA410, KA42,
 * KA43, KA46 and KA48 CPUs.
 *
 * The best documentation I've been able to find so far is the
 * VAXstation 2000 and MicroVAX 2000 Technical Manual (EK-VTTAA-TM)
 * This manual covers the KA410.  However, the newer VAXstations
 * seem to be very similar.  NetBSD and VMS's LIB.REQ have been
 * further sources of information.
 *
 */

#include <linux/init.h>

#include <asm/io.h>
#include <asm/bus/vsbus.h>

static int __init vsbus_ka4x_probe(struct device *busdev)
{
	unsigned int __iomem *vectors;
	int retval;

        /*
         * Map the area where we expect to find our device
         * interrupt vectors so that we can copy them somewhere
	 * more convenient
         */

	vectors = ioremap(0x20040020, 0x20);
	if (!vectors) {
		return -EAGAIN;
	}

	retval = init_vsbus_adapter(vectors, VSA_BASE_REGS);

	iounmap(vectors);

	return retval;
}

static struct device_driver vsbus_ka4x_driver = {
	.name	= "ka4x-vsbus",
	.bus	= &platform_bus_type,
	.probe	= vsbus_ka4x_probe,
};

int __init vsbus_ka4x_init(void)
{
	return driver_register(&vsbus_ka4x_driver);
}


subsys_initcall(vsbus_ka4x_init);


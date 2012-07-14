/*
 * Support for the CQBIC (which I guess stands for something like
 * CVAX QBus Interface Chip).
 *
 * This is the QBUS bus adapter used in the KA640/650/655.
 * Documentation is in the KA655 Technical manual
 * (EK-KA655-TM-001, available online via http://vt100.net/manx).
 *
 * The CQBIC maps the 8K QBUS I/O space into physical memory at
 * physical address 0x20000000.  It provides 8192 mapping registers
 * that can each map one 512-byte page between VAX physical
 * memory and QBUS memory space for DMA transfers.
 *
 */

#include <asm/io.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/device.h>

#include <asm/bus/qbus.h>
#include <asm/ioprobe.h>

#define CQBIC_DEBUG 0

#define CQBIC_MAPREGPHYS 0x20088000
#define CQBIC_NUMMAPREGS 8192

#define CQBIC_IOSPACE_BASE 0x20000000
#define CQBIC_IOSPACE_SIZE 0x00002000

static struct device_driver cqbic_driver;

struct cqbic_private {
	struct qbus_ops	*	bus_ops;

	DECLARE_BITMAP(vector_bitmap, QBUS_NUM_VECTORS);

	unsigned int __iomem *	mapregbase;
	unsigned long		iospace_phys_base;
	unsigned int		scb_offset;
};

/* Given a (start, len), how many pagelets does this span? */

static unsigned int num_pagelets(void *start, unsigned int len)
{
	unsigned int start_pagelet;
	unsigned int end_pagelet;

	start_pagelet = (unsigned int)start >> PAGELET_SHIFT;
	end_pagelet = ((unsigned int)start + len - 1) >> PAGELET_SHIFT;

	return end_pagelet - start_pagelet + 1;
}

static int find_n_free(struct cqbic_private *cqbic, unsigned int n)
{
	int i;
	int j;

	i = 0;
	while (i < (8192 - n)) {
		for (j=0; j<n; j++) {
			if (cqbic->mapregbase[i+j]) {
				/* This reg in use */
				break;
			}
		}
		if (j == n) {
			/* Found N contiguous free entries at offset I */
			return i;
		}
		i += j+1;
	}
	return -1;
}

/* Allocate a bunch of map registers sufficient to map 'len' bytes
   at address 'start'.

   This is a very dumb allocator - does linear searches for available
   registers.  Need a better way to do this.  My first thought was to
   use bits 30:0 in invalid map registers to contain forward and
   backward links to maintain a list of free registers.  However, bits
   30:20 are reserved (read as zero and should be written as zero),
   so that only leaves us with 20 bits for links.  This would be
   OK if we allow the allocation granularity to be 8 registers. - KPH
*/

static struct vax_dmamap *cqbic_alloc_mapregs(struct device *busdev, void *start, unsigned int len)
{
	struct vax_dmamap *map;
	struct cqbic_private *cqbic = (struct cqbic_private *)busdev->driver_data;

	map = kmalloc(sizeof(struct vax_dmamap), GFP_ATOMIC);
	if (map != NULL) {
		int reg;
		unsigned int pagelets;

		pagelets = num_pagelets(start, len);

		reg = find_n_free(cqbic, pagelets);
		if (reg != -1) {
			unsigned int pfn;
			unsigned int reg_value;
			int i;

			pfn = virt_to_phys(start) >> PAGELET_SHIFT;
			reg_value = (pfn & 0xfffff) | 0x80000000;

			for (i = reg; i < reg + pagelets; i++, reg_value++) {
				cqbic->mapregbase[i] = reg_value;
			}

			map->reg = reg;
			map->pagelets = pagelets;
			map->virtaddr = start;
			map->busaddr = (reg * PAGELET_SIZE) + ((unsigned int)start & ~PAGELET_MASK);
#if CQBIC_DEBUG
			printk("Using map registers 0x%04x to 0x%04x to map virt %p to %p (bus %08x)\n",
					reg, reg + pagelets - 1,
					start, (char *)start + len - 1, map->busaddr);
#endif

		} else {
			kfree(map);
			map = NULL;
		}
	}
	return map;
}

static void cqbic_unmap(struct device *busdev, struct vax_dmamap *map)
{
	struct cqbic_private *cqbic = (struct cqbic_private *)busdev->driver_data;

#if CQBIC_DEBUG
	printk("Zapping map registers 0x%04x to 0x%04x\n", map->reg, map->reg + map->pagelets - 1);
#endif
	while (map->pagelets--) {
		cqbic->mapregbase[map->reg] = 0;
		map->reg++;
	}
	kfree(map);
}

static void cqbic_dumpmap(struct device *busdev)
{
	int i;
	struct cqbic_private *cqbic = (struct cqbic_private *)busdev->driver_data;

	for (i=0; i<CQBIC_NUMMAPREGS; i++) {
		if (cqbic->mapregbase[i] != 0) {
			printk("CQBIC map reg %04x = %08x (-> %08x)\n", i,
				cqbic->mapregbase[i],
				(cqbic->mapregbase[i] & 0xfffff) << PAGELET_SHIFT);
		}
	}
}


/* Traditionally, QBUS interrupt vectors are multiples of 4. */

static int cqbic_vector_to_irq(struct device *busdev, unsigned int vector)
{
	struct cqbic_private *cqbic = (struct cqbic_private *)busdev->driver_data;

	return (vector / 4) + cqbic->scb_offset;
}

static int cqbic_request_irq(struct device *busdev, unsigned int vector,
        irqreturn_t (*handler)(int, void *, struct pt_regs *),
        unsigned long irqflags,
        const char * devname,
        void *dev_id)
{
	return request_irq(cqbic_vector_to_irq(busdev, vector),
				handler, irqflags, devname, dev_id);
}

/* Mark a specific QBUS vector as unavailable for dynamic allocation.
   Returns 0 if was previously available, 1 if previously reserved */

static unsigned int cqbic_reserve_vector(struct device *busdev, unsigned int vector)
{
	struct cqbic_private *cqbic = (struct cqbic_private *)busdev->driver_data;

	return test_and_set_bit(vector / 4, cqbic->vector_bitmap);
}

/* Locate an available interrupt vector and mark it reserved.  Return 0
   if none available.  */

static unsigned int cqbic_alloc_vector(struct device *busdev)
{
	struct cqbic_private *cqbic = (struct cqbic_private *)busdev->driver_data;
	unsigned int vector;

	do {
		vector = 4 * find_first_zero_bit(cqbic->vector_bitmap, QBUS_NUM_VECTORS);
		if (!vector) {
			return 0;
		}
	} while (cqbic_reserve_vector(busdev, vector));

	return vector;
}

/* Mark an interrupt vector as available again */

static void cqbic_free_vector(struct device *busdev, unsigned int vector)
{
	struct cqbic_private *cqbic = (struct cqbic_private *)busdev->driver_data;

	vector = vector / 4;
	if (vector) {
		clear_bit(vector, cqbic->vector_bitmap);
	}
}

static void *cqbic_ioremap(struct device *busdev, unsigned int bus_addr, unsigned int size)
{
	struct cqbic_private *cqbic = (struct cqbic_private *)busdev->driver_data;

	return ioremap(cqbic->iospace_phys_base + bus_addr, size);
}

static void __init cqbic_device_detected(struct device *parent, unsigned int csr_offset)
{
	struct qbus_device *qbus_dev;

	qbus_dev = kmalloc(sizeof(*qbus_dev), GFP_KERNEL);
	if (qbus_dev == NULL) {
		printk("qbus_device_detected: cannot allocate "
			"device structure for CSR 0x%x\n", csr_offset);
		return;
	}

	memset(qbus_dev, 0, sizeof(*qbus_dev));

	qbus_dev->csr = csr_offset;
	qbus_dev->dev.bus = &qbus_bus_type;
	qbus_dev->dev.parent = parent;

	snprintf(qbus_dev->dev.bus_id, sizeof(qbus_dev->dev.bus_id),
		"%s-%o", parent->bus_id, QBUS_OCTAL_CSR(csr_offset));

	qbus_register_device(qbus_dev);
}

static struct qbus_ops cqbic_bus_ops = {
	.dma_map	= cqbic_alloc_mapregs,
	.dma_unmap	= cqbic_unmap,
	.dma_dumpmap	= cqbic_dumpmap,
	.vector_to_irq	= cqbic_vector_to_irq,
	.request_irq	= cqbic_request_irq,
	.reserve_vector	= cqbic_reserve_vector,
	.alloc_vector	= cqbic_alloc_vector,
	.free_vector	= cqbic_free_vector,
	.ioremap	= cqbic_ioremap,
};

static int __init cqbic_probe(struct device *busdev)
{
	int i;
	void __iomem *cqbic_iospace;
	struct cqbic_private *cqbic;

	cqbic = kmalloc(sizeof(*cqbic), GFP_KERNEL);
	if (!cqbic) {
		return -ENOMEM;
	}

	memset(cqbic, 0, sizeof(*cqbic));

	busdev->driver_data = cqbic;

	cqbic->bus_ops = &cqbic_bus_ops;

	cqbic->iospace_phys_base = CQBIC_IOSPACE_BASE;

	/* The CQBIC maps QBUS interrupts to the second page of the SCB
	   (each page of the SCB contains 128 vectors). */
	cqbic->scb_offset = 128;

	/* Mark vector 0 as reserved */
	set_bit(0, cqbic->vector_bitmap);

	cqbic->mapregbase = (unsigned int *)ioremap(CQBIC_MAPREGPHYS,
				CQBIC_NUMMAPREGS * sizeof(unsigned int));
#if CQBIC_DEBUG
	printk("CQBIC map registers mapped at %p\n", cqbic->mapregbase);
#endif

	for (i=0; i<CQBIC_NUMMAPREGS; i++) {
		cqbic->mapregbase[i] = 0;
	}

	/* Now we scan the qbus and look for CSR addresses that have
	   something living there.  When we find a device, create a
	   driver model struct device for it */

	cqbic_iospace = ioremap(cqbic->iospace_phys_base, CQBIC_IOSPACE_SIZE);

	for (i=0; i<CQBIC_IOSPACE_SIZE; i+=8) {
		if (iospace_probew(cqbic_iospace + i)) {
			cqbic_device_detected(busdev, i);
		}
	}

	iounmap(cqbic_iospace);

	return 0;
}

static struct device_driver cqbic_driver = {
	.name   = "cqbic",
	.bus	= &platform_bus_type,
	.probe	= cqbic_probe,
};

int __init cqbic_init(void)
{
	return driver_register(&cqbic_driver);
}

subsys_initcall(cqbic_init);


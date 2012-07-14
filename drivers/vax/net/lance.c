/*
 *	Lance ethernet driver for the VAX station 3100
 *
 *      Adapted from declance.c - Linux MIPS Decstation Team
 *      modified for DS5000/200 + VAX MIPS - Dave Airlie (airlied@linux.ie)
 *
 * I have every intention of remerging this driver with declance.c
 * at some stage, this version is just intermediate I hope :-)
 *                                           - D.A. July 2000
 *
 * I've started to write some more of this.. doesn't do anything
 * extra visibly, just does some static block allocation to use
 * until kmalloc arrives... next on the list are the IRQ and getting
 * the lance pointed at the init block in the right address space.
 *                                           - D.A. 14 Aug 2000
 *
 */

static char *version =
"vaxlance.c: v0.008 by Linux Mips DECstation task force + airlied@linux.ie\n";

static char *lancestr = "LANCE";

#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>

#include <asm/cacheflush.h>         /* for __flush_tlb_one */

#include <asm/bus/vsbus.h>

/* Ugly kludge to deal with KA43 weirdness */
#include <asm/mv.h>
#include <asm/ka43.h>

#define CRC_POLYNOMIAL_BE 0x04c11db7UL  /* Ethernet CRC, big endian */
#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */

#define LE_CSR0 0
#define LE_CSR1 1
#define LE_CSR2 2
#define LE_CSR3 3

#define LE_MO_PROM      0x8000  /* Enable promiscuous mode */

#define	LE_C0_ERR	0x8000	/* Error: set if BAB, SQE, MISS or ME is set */
#define	LE_C0_BABL	0x4000	/* BAB:  Babble: tx timeout. */
#define	LE_C0_CERR	0x2000	/* SQE:  Signal quality error */
#define	LE_C0_MISS	0x1000	/* MISS: Missed a packet */
#define	LE_C0_MERR	0x0800	/* ME:   Memory error */
#define	LE_C0_RINT	0x0400	/* Received interrupt */
#define	LE_C0_TINT	0x0200	/* Transmitter Interrupt */
#define	LE_C0_IDON	0x0100	/* IFIN: Init finished. */
#define	LE_C0_INTR	0x0080	/* Interrupt or error */
#define	LE_C0_INEA	0x0040	/* Interrupt enable */
#define	LE_C0_RXON	0x0020	/* Receiver on */
#define	LE_C0_TXON	0x0010	/* Transmitter on */
#define	LE_C0_TDMD	0x0008	/* Transmitter demand */
#define	LE_C0_STOP	0x0004	/* Stop the card */
#define	LE_C0_STRT	0x0002	/* Start the card */
#define	LE_C0_INIT	0x0001	/* Init the card */

#define	LE_C3_BSWP	0x4     /* SWAP */
#define	LE_C3_ACON	0x2	/* ALE Control */
#define	LE_C3_BCON	0x1	/* Byte control */

/* Receive message descriptor 1 */
#define LE_R1_OWN       0x80    /* Who owns the entry */
#define LE_R1_ERR       0x40    /* Error: if FRA, OFL, CRC or BUF is set */
#define LE_R1_FRA       0x20    /* FRA: Frame error */
#define LE_R1_OFL       0x10    /* OFL: Frame overflow */
#define LE_R1_CRC       0x08    /* CRC error */
#define LE_R1_BUF       0x04    /* BUF: Buffer error */
#define LE_R1_SOP       0x02    /* Start of packet */
#define LE_R1_EOP       0x01    /* End of packet */
#define LE_R1_POK       0x03    /* Packet is complete: SOP + EOP */

#define LE_T1_OWN       0x80    /* Lance owns the packet */
#define LE_T1_ERR       0x40    /* Error summary */
#define LE_T1_EMORE     0x10    /* Error: more than one retry needed */
#define LE_T1_EONE      0x08    /* Error: one retry needed */
#define LE_T1_EDEF      0x04    /* Error: deferred */
#define LE_T1_SOP       0x02    /* Start of packet */
#define LE_T1_EOP       0x01    /* End of packet */
#define LE_T1_POK	0x03	/* Packet is complete: SOP + EOP */

#define LE_T3_BUF       0x8000  /* Buffer error */
#define LE_T3_UFL       0x4000  /* Error underflow */
#define LE_T3_LCOL      0x1000  /* Error late collision */
#define LE_T3_CLOS      0x0800  /* Error carrier loss */
#define LE_T3_RTY       0x0400  /* Error retry */
#define LE_T3_TDR       0x03ff  /* Time Domain Reflectometry counter */

/* Define: 2^4 Tx buffers and 2^4 Rx buffers */

#ifndef LANCE_LOG_TX_BUFFERS
#define LANCE_LOG_TX_BUFFERS 4
#define LANCE_LOG_RX_BUFFERS 4
#endif

#define TX_RING_SIZE			(1 << (LANCE_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK		(TX_RING_SIZE - 1)

#define RX_RING_SIZE			(1 << (LANCE_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK		(RX_RING_SIZE - 1)

#define PKT_BUF_SZ		1536
#define RX_BUFF_SIZE            PKT_BUF_SZ
#define TX_BUFF_SIZE            PKT_BUF_SZ

#undef VAX_LANCE_DEBUG
#undef VAX_LANCE_DEBUG_BUFFERS


struct lance_rx_desc {
	unsigned short rmd0;        /* low address of packet */
	unsigned char  rmd1_hadr;   /* high address of packet */
	unsigned char  rmd1_bits;   /* descriptor bits */
	short    length;	    /* This length is 2s complement (negative)!
				     * Buffer length
				     */
	unsigned short mblength;    /* This is the actual number of bytes received */
};

struct lance_tx_desc {
	unsigned short tmd0;        /* low address of packet */
	unsigned char  tmd1_hadr;   /* high address of packet */
	unsigned char  tmd1_bits;   /* descriptor bits */
	short length;		    /* Length is 2s complement (negative)! */
	unsigned short misc;
};


/* First part of the LANCE initialization block, described in databook. */
struct lance_init_block {
	unsigned short mode;		/* Pre-set mode (reg. 15) */

	unsigned char phys_addr[6];    /* Physical ethernet address
					 * only 0, 1, 4, 5, 8, 9 are valid
					 * 2, 3, 6, 7, 10, 11 are gaps
					 */
	unsigned short filter[4];	/* Multicast filter.
					 * only 0, 2, 4, 6 are valid
					 * 1, 3, 5, 7 are gaps
					 */

	/* Receive and transmit ring base, along with extra bits. */
	unsigned short rx_ptr;		/* receive descriptor addr */
	unsigned short rx_len;		/* receive len and high addr */
	unsigned short tx_ptr;		/* transmit descriptor addr */
	unsigned short tx_len;		/* transmit len and high addr */
	short gap0[4];

	/* The buffer descriptors */
	struct   lance_rx_desc  brx_ring[RX_RING_SIZE];
	struct   lance_tx_desc  btx_ring[TX_RING_SIZE];
};


#define BUF_OFFSET_CPU (offsetof(struct lance_shared_mem, rx_buf))
#define BUF_OFFSET_LNC BUF_OFFSET_CPU


/* This is how our shared memory block is layed out */

struct lance_shared_mem {
	struct lance_init_block init_block;  /* Includes RX/TX descriptors */
	char rx_buf[RX_RING_SIZE][RX_BUFF_SIZE];
	char tx_buf[RX_RING_SIZE][RX_BUFF_SIZE];
};


struct lance_private {
	char *name;

	/* virtual addr of registers */
	volatile struct lance_regs *ll;

	/* virtual addr of shared memory block */
	volatile struct lance_shared_mem *lance_mem;

	/* virtual addr of block inside shared mem block */
	volatile struct lance_init_block *init_block;

        unsigned char vsbus_int;
	spinlock_t	lock;

	int rx_new, tx_new;
	int rx_old, tx_old;

	struct net_device_stats	stats;

	unsigned short busmaster_regval;

	struct net_device *dev;	/* Backpointer        */
	struct lance_private *next_module;
	struct timer_list       multicast_timer;
};

#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			lp->tx_old+TX_RING_MOD_MASK-lp->tx_new:\
			lp->tx_old - lp->tx_new-1)

/* The lance control ports are at an absolute address, machine dependent.
 * VAXstations align the two 16-bit registers on 32-bit boundaries
 * so we have to give the structure an extra member making rap pointing
 * at the right address
 */
struct lance_regs {
	volatile unsigned short rdp;			/* register data port */
	unsigned short pad;
	volatile unsigned short rap;			/* register address port */
};



/* Communication with the LANCE takes place via four channels:

     1. The RDP and RAP ports (which live at 200e0000 physical on
        VS3100-family machines).  Through these two ports we can
        access the LANCE's 4 registers: CSR0, CSR1, CSR2 and CSR3
        (very imaginatively named...)

     2. The LANCE init block which we allocate.  We tell the LANCE where the
        init block lives in memory via the CSR1 and CSR2 registers.  The init
        block contains the ethernet address, multi-cast address filter and
        contains the physical addresses of the RX and TX buffer descriptors.
        The init block must be word aligned.

     3. The RX and TX buffer descriptors are pointed to by the init block and
        in turn contain the physical addresses of the RX and TX buffers.
        The buffer descriptors must be quadword aligned.

     4. The RX and TX buffers themselves.  These buffers have no alignment
        requirement.

   To keep things simple, we allocate a single 64K chunk of memory which
   contains the init block, followed by the buffer descriptors and then
   the buffers.

   For most CPUs, the physical addresses used by the LANCE and the
   virtual addresses used by the CPU follow the usual virt = phys+0x80000000
   convention.

   However, the KA43 has an unusual requirement.  Physical memory on the
   KA43 is accessible from address 0 upwards as normal, but is also visible
   in the region starting a 0x28000000.  This region is called the DIAGMEM
   region.  What's different about it, I don't know, but it's probably
   something to do with caching.

   So, after allocating the 64KB chunk, but before we tell the LANCE
   about it, we tweak the PTEs behind these pages to map to physical
   addresses in the DIAGMEM region.

   As of 2001-03-06, the closest data sheet I can find is the AM79C90 (aka
   C-LANCE) on AMD's site at http://www.amd.com/products/npd/techdocs/17881.pdf.
*/



static inline void writereg(volatile unsigned short *regptr, short value)
{
	*regptr = value;
}

static inline void writecsr0(volatile struct lance_regs *ll, unsigned short value)
{
	writereg(&ll->rap, LE_CSR0);
	writereg(&ll->rdp, value);
}

static inline void lance_stop(volatile struct lance_regs *ll)
{
	writecsr0(ll, LE_C0_STOP);

	/* Is this needed?  NetBSD does it sometimes  */
	udelay(100);
}

/* Load the CSR registers */
static void load_csrs(struct lance_private *lp)
{
	volatile struct lance_regs *ll = lp->ll;
	unsigned long leptr;

	leptr = virt_to_phys(lp->init_block);

	writereg(&ll->rap, LE_CSR1);
	writereg(&ll->rdp, (leptr & 0xFFFF));
	writereg(&ll->rap, LE_CSR2);
	writereg(&ll->rdp, (leptr >> 16) & 0xFF);
	writereg(&ll->rap, LE_CSR3);
	writereg(&ll->rdp, lp->busmaster_regval);

	/* Point back to csr0 */
	writereg(&ll->rap, LE_CSR0);
}


/*
 * Our specialized copy routines
 *
 */
static inline void cp_to_buf(void *to, const void *from, __kernel_size_t len)
{
	memcpy(to, from, len);
}

static inline void cp_from_buf(void *to, unsigned char *from, int len)
{
	memcpy(to, from, len);
}

/* Setup the Lance Rx and Tx rings */
static void lance_init_ring(struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	unsigned long leptr;
	int i;

	/* Lock out other processes while setting up hardware */

	netif_stop_queue(dev);
	lp->rx_new = lp->tx_new = 0;
	lp->rx_old = lp->tx_old = 0;

	/* Copy the ethernet address to the lance init block.
	 * XXX bit 0 of the physical address registers has to be zero
	 */
	ib->phys_addr[0] = dev->dev_addr[0];
	ib->phys_addr[1] = dev->dev_addr[1];
	ib->phys_addr[2] = dev->dev_addr[2];
	ib->phys_addr[3] = dev->dev_addr[3];
	ib->phys_addr[4] = dev->dev_addr[4];
	ib->phys_addr[5] = dev->dev_addr[5];
	/* Setup the initialization block */

	/* Setup rx descriptor pointer */

	/* Calculate the physical address of the first receive descriptor */
	leptr = virt_to_phys(ib->brx_ring);
	ib->rx_len = (LANCE_LOG_RX_BUFFERS << 13) | (leptr >> 16);
	ib->rx_ptr = leptr;

#ifdef VAX_LANCE_DEBUG
	printk("RX ptr: %8.8lx(%8.8x)\n", leptr, ib->brx_ring);
#endif
	/* Setup tx descriptor pointer */

	/* Calculate the physical address of the first transmit descriptor */
	leptr = virt_to_phys(ib->btx_ring);
	ib->tx_len = (LANCE_LOG_TX_BUFFERS << 13) | (leptr >> 16);
	ib->tx_ptr = leptr;

#ifdef VAX_LANCE_DEBUG
	printk("TX ptr: %8.8lx(%8.8x)\n", leptr, ib->btx_ring);

	printk("TX rings:\n");
#endif
	/* Setup the Tx ring entries */
	for (i = 0; i < TX_RING_SIZE; i++) {
		leptr = virt_to_phys(lp->lance_mem->tx_buf[i]) & 0xffffff;

		ib->btx_ring[i].tmd0 = leptr;
		ib->btx_ring[i].tmd1_hadr = leptr >> 16;
		ib->btx_ring[i].tmd1_bits = 0;
		ib->btx_ring[i].length = 0xf000;	/* The ones required by tmd2 */
		ib->btx_ring[i].misc = 0;

#ifdef VAX_LANCE_DEBUG
		if (i < 3)
			printk("%d: 0x%8.8lx(0x%8.8x)\n", i, leptr, (int) lp->lance_mem->tx_buf[i]);
#endif
	}

	/* Setup the Rx ring entries */
#ifdef VAX_LANCE_DEBUG
	printk("RX rings:\n");
#endif
	for (i = 0; i < RX_RING_SIZE; i++) {
		leptr = virt_to_phys(lp->lance_mem->rx_buf[i]) & 0xffffff;

		ib->brx_ring[i].rmd0 = leptr;
		ib->brx_ring[i].rmd1_hadr = leptr >> 16;
		ib->brx_ring[i].rmd1_bits = LE_R1_OWN;
		ib->brx_ring[i].length = -RX_BUFF_SIZE | 0xf000;
		ib->brx_ring[i].mblength = 0;
#ifdef VAX_LANCE_DEBUG
		if (i < 3)
			printk("%d: 0x%8.8lx(0x%8.8x)\n", i, leptr, (int) lp->lance_mem->rx_buf[i]);
#endif
	}
}

static int init_restart_lance(struct lance_private *lp)
{
	volatile struct lance_regs *ll = lp->ll;
	int i;

	/* Is this needed?  NetBSD does it.  */
	udelay(100);

	writecsr0(ll, LE_C0_INIT);

	/* Wait for the lance to complete initialization */
	for (i = 0; (i < 100) && !(ll->rdp & LE_C0_IDON); i++) {
#ifdef VAX_LANCE_DEBUG
		printk("LANCE opened maybe %d\n", i);
#endif
		udelay(10);
	}
	if ((i == 100) || (ll->rdp & LE_C0_ERR)) {
#ifdef VAX_LANCE_DEBUG
		printk("LANCE unopened after %d ticks, csr0=%4.4x.\n", i, ll->rdp);
#endif
		return -1;
	}
	if ((ll->rdp & LE_C0_ERR)) {
#ifdef VAX_LANCE_DEBUG
		printk("LANCE unopened after %d ticks, csr0=%4.4x.\n", i, ll->rdp);
#endif
		return -1;
	}
#ifdef VAX_LANCE_DEBUG
	printk("LANCE opened maybe\n");
#endif
	writecsr0(ll, LE_C0_IDON);
	writecsr0(ll, LE_C0_INEA | LE_C0_STRT);

	/* AM79C90 datasheet describes a problem in the original AM7990
	   whereby INEA cannot be set while STOP is set.  What is not
	   clear is if setting INEA at the same time is STRT is OK.
	   So, just in case, we might need to  set INEA again */
	/* writecsr0(ll, LE_C0_INEA); */

	return 0;
}

static int lance_rx(struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_rx_desc *rd = 0;
	unsigned char bits;
	int len = 0;
#ifdef VAX_LANCE_DEBUG_BUFFERS
	int i;
#endif
	struct sk_buff *skb = 0;

#ifdef VAX_LANCE_DEBUG_BUFFERS

	printk("[");
	for (i = 0; i < RX_RING_SIZE; i++) {
		if (i == lp->rx_new)
			printk("%s",
			       ib->brx_ring[i].rmd1_bits & LE_R1_OWN ? "_" : "X");
		else
			printk("%s",
			       ib->brx_ring[i].rmd1_bits & LE_R1_OWN ? "." : "1");
	}
	printk("]");
#endif

	for (rd = &ib->brx_ring[lp->rx_new];
	     !((bits = rd->rmd1_bits) & LE_R1_OWN);
	     rd = &ib->brx_ring[lp->rx_new]) {

		/* We got an incomplete frame? */
		if ((bits & LE_R1_POK) != LE_R1_POK) {
			lp->stats.rx_over_errors++;
			lp->stats.rx_errors++;
		} else if (bits & LE_R1_ERR) {
			/* Count only the end frame as a rx error,
			 * not the beginning
			 */
			if (bits & LE_R1_BUF)
				lp->stats.rx_fifo_errors++;
			if (bits & LE_R1_CRC)
				lp->stats.rx_crc_errors++;
			if (bits & LE_R1_OFL)
				lp->stats.rx_over_errors++;
			if (bits & LE_R1_FRA)
				lp->stats.rx_frame_errors++;
			if (bits & LE_R1_EOP)
				lp->stats.rx_errors++;
		} else {
			len = (rd->mblength & 0xfff) - 4;
			skb = dev_alloc_skb(len + 2);

			if (skb == 0) {
				printk("%s: Memory squeeze, deferring packet.\n",
					dev->name);
				lp->stats.rx_dropped++;
				rd->mblength = 0;
				rd->rmd1_bits = LE_R1_OWN;
				lp->rx_new = (lp->rx_new + 1) & RX_RING_MOD_MASK;
				return 0;
			}
			lp->stats.rx_bytes += len;

			skb->dev = dev;
			skb_reserve(skb, 2);	/* 16 byte align */
			skb_put(skb, len);	/* make room */
			cp_from_buf(skb->data,
				 (char *) lp->lance_mem->rx_buf[lp->rx_new],
					 len);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx=jiffies;
			lp->stats.rx_packets++;
		}

		/* Return the packet to the pool */
		rd->mblength = 0;
		rd->length = -RX_BUFF_SIZE | 0xf000;
		rd->rmd1_bits = LE_R1_OWN;
		lp->rx_new = (lp->rx_new + 1) & RX_RING_MOD_MASK;
	}
	return 0;
}

static void lance_tx(struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_tx_desc *td;
	int i, j;
	int status;
	j = lp->tx_old;

	spin_lock(&lp->lock);

	for (i = j; i != lp->tx_new; i = j) {
		td = &ib->btx_ring[i];
		/* If we hit a packet not owned by us, stop */
		if (td->tmd1_bits & LE_T1_OWN)
			break;

		if (td->tmd1_bits & LE_T1_ERR) {
			status = td->misc;

			lp->stats.tx_errors++;
			if (status & LE_T3_RTY)
				lp->stats.tx_aborted_errors++;
			if (status & LE_T3_LCOL)
				lp->stats.tx_window_errors++;

			if (status & LE_T3_CLOS) {
				lp->stats.tx_carrier_errors++;
				printk("%s: Carrier Lost", dev->name);

				lance_stop(ll);
				lance_init_ring(dev);
				load_csrs(lp);
				init_restart_lance(lp);
				goto out;
			}
			/* Buffer errors and underflows turn off the
			 * transmitter, restart the adapter.
			 */
			if (status & (LE_T3_BUF | LE_T3_UFL)) {
				lp->stats.tx_fifo_errors++;

				printk("%s: Tx: ERR_BUF|ERR_UFL, restarting\n",
					dev->name);

				lance_stop(ll);
				lance_init_ring(dev);
				load_csrs(lp);
				init_restart_lance(lp);
				goto out;
			}
		} else if ((td->tmd1_bits & LE_T1_POK) == LE_T1_POK) {
			/*
			 * So we don't count the packet more than once.
			 */
			td->tmd1_bits &= ~(LE_T1_POK);

			/* One collision before packet was sent. */
			if (td->tmd1_bits & LE_T1_EONE)
				lp->stats.collisions++;

			/* More than one collision, be optimistic. */
			if (td->tmd1_bits & LE_T1_EMORE)
				lp->stats.collisions += 2;

			lp->stats.tx_packets++;
		}
		j = (j + 1) & TX_RING_MOD_MASK;
	}
	lp->tx_old = j;
out:
	if (netif_queue_stopped(dev) &&
	    TX_BUFFS_AVAIL > 0)
		netif_wake_queue(dev);

	spin_unlock(&lp->lock);
}

static irqreturn_t lance_interrupt(const int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_regs *ll = lp->ll;
	int csr0;

	writereg(&ll->rap, LE_CSR0);
	csr0 = ll->rdp;

	if ((csr0 & LE_C0_INTR) == 0) {
		/* Hmmm, not for us... */
		return IRQ_HANDLED;
	}

	/* According to NetBSD, we need to temporarily disable the
	   interrupts here to get things to work properly all the
	   time */

	/* temporarily disable interrupts from LANCE */
	csr0 &= ~LE_C0_INEA;

	/* Acknowledge all the interrupt sources */
	writecsr0(ll, csr0);

	/* re-enable interrupts from LANCE */
	writecsr0(ll, LE_C0_INEA);

	if ((csr0 & LE_C0_ERR)) {
		/* Clear the error condition */
		writecsr0(ll, LE_C0_BABL | LE_C0_ERR | LE_C0_MISS |
				LE_C0_CERR | LE_C0_MERR);
	}
	if (csr0 & LE_C0_RINT)
		lance_rx(dev);

	if (csr0 & LE_C0_TINT)
		lance_tx(dev);

	if (csr0 & LE_C0_BABL)
		lp->stats.tx_errors++;

	if (csr0 & LE_C0_MISS)
		lp->stats.rx_errors++;

	if (csr0 & LE_C0_MERR) {
		printk("%s: Memory error, status %04x", dev->name, csr0);

		lance_stop(ll);

		lance_init_ring(dev);
		load_csrs(lp);
		init_restart_lance(lp);
		netif_wake_queue(dev);
	}

	/* FIXME: why is this really needed? */
	writecsr0(ll, LE_C0_INEA);

	return IRQ_HANDLED;
}

struct net_device *last_dev = 0;

static int lance_open(struct net_device *dev)
{
        struct lance_private *lp = (struct lance_private *) dev->priv;
        volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_regs *ll = lp->ll;

	last_dev = dev;

	/* Associate IRQ with lance_interrupt */
	if (vsbus_request_irq(lp->vsbus_int, &lance_interrupt, 0, lp->name, dev)) {
		printk("Lance: Can't get irq %d\n", dev->irq);
		return -EAGAIN;
	}

	lance_stop(ll);

	/* Clear the multicast filter */
	ib->mode=0;
	ib->filter[0] = 0;
	ib->filter[1] = 0;
	ib->filter[2] = 0;
	ib->filter[3] = 0;

	lance_init_ring(dev);
	load_csrs(lp);

	netif_start_queue(dev);

	return init_restart_lance(lp);
}

static int lance_close(struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_regs *ll = lp->ll;

	netif_stop_queue(dev);
	del_timer_sync(&lp->multicast_timer);

	lance_stop(ll);

	vsbus_free_irq(lp->vsbus_int);

	return 0;
}

static inline int lance_reset(struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_regs *ll = lp->ll;
	int status;

	lance_stop(ll);

	lance_init_ring(dev);
	load_csrs(lp);
	dev->trans_start = jiffies;
	status = init_restart_lance(lp);
#ifdef VAX_LANCE_DEBUG
	printk("Lance restart=%d\n", status);
#endif
	return status;
}

static void lance_tx_timeout(struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_regs *ll = lp->ll;

	printk(KERN_ERR "%s: transmit timed out, status %04x, reset\n",
			       dev->name, ll->rdp);
	lance_reset(dev);
	netif_wake_queue(dev);
}

static int lance_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_init_block *ib = lp->init_block;
	int entry, skblen, len;

	skblen = skb->len;

	len = (skblen <= ETH_ZLEN) ? ETH_ZLEN : skblen;

	spin_lock_irq(&lp->lock);

	lp->stats.tx_bytes += len;

	entry = lp->tx_new & TX_RING_MOD_MASK;
	ib->btx_ring[entry].length = (-len) | 0xf000;
	ib->btx_ring[entry].misc = 0;

	cp_to_buf((char *) lp->lance_mem->tx_buf[entry], skb->data, skblen);

	/* Clear the slack of the packet, do I need this? */
	/* For a firewall its a good idea - AC */
/*
	if (len != skblen)
		memset ((char *) &ib->tx_buf [entry][skblen], 0, (len - skblen) << 1);
 */
	/* Now, give the packet to the lance */
	ib->btx_ring[entry].tmd1_bits = (LE_T1_POK | LE_T1_OWN);
	lp->tx_new = (lp->tx_new + 1) & TX_RING_MOD_MASK;

	if (TX_BUFFS_AVAIL <= 0)
		netif_stop_queue(dev);

	/* Kick the lance: transmit now */
	writecsr0(ll, LE_C0_INEA | LE_C0_TDMD);

	spin_unlock_irq(&lp->lock);

	dev->trans_start = jiffies;
	dev_kfree_skb(skb);

	return 0;
}

static struct net_device_stats *lance_get_stats(struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;

	return &lp->stats;
}

static void lance_load_multicast(struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	volatile u16 *mcast_table = (u16 *)&ib->filter;
	struct dev_mc_list *dmi = dev->mc_list;
	char *addrs;
	int i, j, bit, byte;
	u32 crc, poly = CRC_POLYNOMIAL_LE;

	/* set all multicast bits */
	if (dev->flags & IFF_ALLMULTI) {
		ib->filter[0] = 0xffff;
		ib->filter[1] = 0xffff;
		ib->filter[2] = 0xffff;
		ib->filter[3] = 0xffff;
		return;
	}
	/* clear the multicast filter */
	ib->filter[0] = 0;
	ib->filter[1] = 0;
	ib->filter[2] = 0;
	ib->filter[3] = 0;

	/* Add addresses */
	for (i = 0; i < dev->mc_count; i++) {
		addrs = dmi->dmi_addr;
		dmi   = dmi->next;

		/* multicast address? */
		if (!(*addrs & 1))
			continue;

		crc = 0xffffffff;
		for (byte = 0; byte < 6; byte++)
			for (bit = *addrs++, j = 0; j < 8; j++, bit >>= 1) {
				int test;

				test = ((bit ^ crc) & 0x01);
				crc >>= 1;

				if (test) {
					crc = crc ^ poly;
				}
			}

		crc = crc >> 26;
		mcast_table[crc >> 4] |= 1 << (crc & 0xf);
	}
	return;
}

static void lance_set_multicast(struct net_device *dev)
{
	struct lance_private *lp = (struct lance_private *) dev->priv;
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_regs *ll = lp->ll;

	if (!netif_running(dev))
		return;

	if (lp->tx_old != lp->tx_new) {
		mod_timer(&lp->multicast_timer, jiffies + 4);
		netif_wake_queue(dev);
		return;
	}

	netif_stop_queue(dev);

	lance_stop(ll);

	lance_init_ring(dev);

	if (dev->flags & IFF_PROMISC) {
		ib->mode |= LE_MO_PROM;
	} else {
		ib->mode &= ~LE_MO_PROM;
		lance_load_multicast(dev);
	}
	load_csrs(lp);
	init_restart_lance(lp);
	netif_wake_queue(dev);
}

static void lance_set_multicast_retry(unsigned long _opaque)
{
	struct net_device *dev = (struct net_device *) _opaque;

	lance_set_multicast(dev);
}


static int __init vax_lance_init(struct net_device *dev, struct vsbus_device *vsbus_dev)
{
	static unsigned version_printed = 0;
	struct lance_private *lp;
	volatile struct lance_regs *ll;
	int i;
	unsigned char __iomem *esar;

	/* Could these base addresses be different on other CPUs? */
	unsigned long lance_phys_addr=vsbus_dev->phys_base;
	unsigned long esar_phys_addr=KA43_NWA_BASE;

	if (version_printed++ == 0)
		printk(version);

	lp = (struct lance_private *) dev->priv;

	spin_lock_init(&lp->lock);

	/* Need a block of 64KB */
        /* At present, until we figure out the address extension
	 * parity control bit, ask for memory in the DMA zone */
	dev->mem_start = __get_free_pages(GFP_DMA, 4);
	if (!dev->mem_start) {
		return -ENOMEM;
	}

#ifdef CONFIG_CPU_KA43
	if (is_ka43()) {

		/* FIXME:
		   We need to check if this block straddles the 16MB boundary.  If
		   it does, then we can't use it for DMA.  Instead we allocate
		   another 64KB block (which obviously cannot straddle the 16MB
		   boundary as well) and free the first.

		   We also need to set the magic bit in PARCTL if we are above
		   the 16MB boundary.

		 */

		/* KA43 only. */
		ka43_diagmem_remap(dev->mem_start, 65536);
	}
#endif /* CONFIG_CPU_KA43 */


	dev->mem_end = dev->mem_start + 65536;

	/* FIXME: check this for NULL */
	dev->base_addr = (unsigned long) ioremap(lance_phys_addr, 0x8);;

	lp->lance_mem = (volatile struct lance_shared_mem *)(dev->mem_start);
	lp->init_block = &(lp->lance_mem->init_block);

	lp->vsbus_int = vsbus_dev->vsbus_irq;
	dev->irq = vsbus_irqindex_to_irq(vsbus_dev->vsbus_irq);

	ll = (struct lance_regs *) dev->base_addr;

	/* FIXME: deal with failure here */
	esar=ioremap(esar_phys_addr, 0x80);

	/* prom checks */
#ifdef CHECK_ADDRESS_ROM_CHECKSUM
	/* If this is dead code, let's remove it... - KPH 2001-03-04 */
        /* not sure if its dead it might just not work on the VAX I have
           does anyone know if VAX store test pattern in EEPROM */
	/* First, check for test pattern */
	if (esar[0x60] != 0xff && esar[0x64] != 0x00 &&
	    esar[0x68] != 0x55 && esar[0x6c] != 0xaa) {
		printk("Ethernet station address prom not found!\n");
		return -ENODEV;
	}
	/* Check the prom contents */
	for (i = 0; i < 8; i++) {
		if (esar[i * 4] != esar[0x3c - i * 4] &&
		    esar[i * 4] != esar[0x40 + i * 4] &&
		    esar[0x3c - i * 4] != esar[0x40 + i * 4]) {
			printk("Something is wrong with the ethernet "
			       "station address prom!\n");
			return -ENODEV;
		}
	}
#endif
	/* Copy the ethernet address to the device structure, later to the
	 * lance initialization block so the lance gets it every time it's
	 * (re)initialized.
	 */
	printk("Ethernet address in ROM: ");
	for (i = 0; i < 6; i++) {
		dev->dev_addr[i] = esar[i * 4];
		printk("%2.2x%c", dev->dev_addr[i], i == 5 ? '\n' : ':');
	}

	/* Don't need this any more */
	iounmap(esar);

	printk("Using LANCE interrupt vector %d, vsbus irq %d\n", dev->irq, lp->vsbus_int);

        dev->open = &lance_open;
	dev->stop = &lance_close;
	dev->hard_start_xmit = &lance_start_xmit;
	dev->tx_timeout = &lance_tx_timeout;
	dev->watchdog_timeo = 5*HZ;
	dev->get_stats = &lance_get_stats;
	dev->set_multicast_list = &lance_set_multicast;
	dev->dma = 0;

	/* lp->ll is the location of the registers for lance card */
	lp->ll = ll;

	lp->name = lancestr;

	/* busmaster_regval (CSR3) should be zero according to the PMAD-AA
	 * specification.
	 */
	lp->busmaster_regval = 0;
	lp->dev = dev;

	ether_setup(dev);

	/* We cannot sleep if the chip is busy during a
	 * multicast list update event, because such events
	 * can occur from interrupts (ex. IPv6).  So we
	 * use a timer to try again later when necessary. -DaveM
	 */
	init_timer(&lp->multicast_timer);
	lp->multicast_timer.data = (unsigned long) dev;
	lp->multicast_timer.function = &lance_set_multicast_retry;

	SET_NETDEV_DEV(dev, &vsbus_dev->dev);

	return 0;
}


static int vaxlance_probe(struct vsbus_device *vsbus_dev)
{
	struct net_device *netdev;
	int retval;

	printk("vaxlance_probe: name = %s, base = 0x%08x, irqindex = %d\n",
		vsbus_dev->dev.bus_id, vsbus_dev->phys_base, vsbus_dev->vsbus_irq);

	netdev = alloc_etherdev(sizeof(struct lance_private));
	if (!netdev) {
		return -ENOMEM;
	}

	retval = vax_lance_init(netdev, vsbus_dev);
	if (!retval) {
		retval = register_netdev(netdev);
	}

	if (retval) {
		free_netdev(netdev);
	}

	return 0;
}

static struct vsbus_driver vaxlance_driver = {
	.probe          = vaxlance_probe,
	.drv = {
		.name   = "lance",
	},
};

int __init vaxlance_init(void)
{
        return vsbus_register_driver(&vaxlance_driver);
}

device_initcall(vaxlance_init);


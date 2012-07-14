/*
 * Quick-and-dirty driver for DELQA/DESQA (Q-bus ethernet adapters)
 *
 * (C) 2002-2004, Kenn Humborg
 *
 * TODO: Pre-allocate the Q-bus mapping registers for TX at init time and
 *	 re-use them, rather than allocating them for each packet.  This
 *	 would remove the only failure possibility from delqa_start_xmit().
 *
 * TODO: Allow multiple DELQAs at different base addresses.
 *
 * TODO: Reset DELQA on q-bus memory access error (is this the right
 *	 thing to do?).
 *
 * TODO: Implement delqa_tx_timeout().
 *
 * TODO: Handle multicast addresses and PROMISC flag in format_setup_frame().
 *
 * TODO: Implement delqa_close().
 */

#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>

#include <asm/system.h>

#include <asm/bus/qbus.h>
#include "delqa-regs.h"

#define DELQA_DEBUG_REGWR 0
#define DELQA_DEBUG_CSR   0
#define DELQA_DEBUG_DESC  0
#define DELQA_DEBUG_PKT   0

/* Where does the DELQA traditionally live on the bus? */
#define DELQA_CSR_BUS_ADDR 0774440

/* FIXME: Are these numbers OK?  These are what NetBSD uses. */
#define RXDESCS 30
#define TXDESCS 60

#define RXBUFSIZE 2048

struct delqa_bufdesc {
	unsigned short flag;
	unsigned short addr_hi;
	unsigned short addr_lo;
	signed   short buflen;
	unsigned short status1;
	unsigned short status2;
};

struct delqa_descs {
	struct delqa_bufdesc rxdesc[RXDESCS+1];
	struct delqa_bufdesc txdesc[TXDESCS+1];
};

struct delqa_private {
	unsigned char __iomem *base;
	unsigned int qbus_vector;
	struct net_device_stats stats;
	struct delqa_descs *descs;
	struct vax_dmamap *desc_map;        /* DMA mapping for delqa_descs structure */
	struct vax_dmamap *rx_map[RXDESCS]; /* DMA mappings for each RX buffer */
	struct vax_dmamap *tx_map[TXDESCS]; /* DMA mappings for each TX buffer */
	struct sk_buff *tx_skb[TXDESCS];    /* We TX direct from the SKB */
	unsigned char setup_frame[128];
	unsigned char setup_frame_len;
	unsigned int next_tx_free;          /* Only written by mainline code */
	unsigned int next_tx_pending;       /* Only written by init and interrupt code */
	unsigned int next_rx;
	unsigned int last_tdr;              /* Last Time Domain Reflectometer value on TX */
	spinlock_t lock;
	struct device *parent;              /* The QBUS on which we live */
};

#define LOWORD(x) ((int)(x) & 0xffff)
#define HIWORD(x) (((int)(x)>>16) & 0xffff)

static unsigned short int read_reg(struct delqa_private *priv, unsigned int offset)
{
	volatile unsigned short int *p;

	p = (volatile unsigned short *)(priv->base + offset);

	return *p;
}

static void write_reg(struct delqa_private *priv, unsigned int offset, unsigned short int value)
{
	volatile unsigned short int *p;
#if DELQA_DEBUG_REGWR
	char *reg[8] = { "ADDR1", "ADDR2", "RCCL", "RCLH", "XMTL", "XMTH", "VECTOR", "CSR"};
	printk("delqa write_reg: offset %02d(%s) value %04x\n", offset, reg[offset/2], value);
#endif

	p = (volatile unsigned short *)(priv->base + offset);
	*p = value;
}


static void dump_csr(char *msg, struct net_device *dev)
{
	struct delqa_private *priv = (struct delqa_private *)dev->priv;
	unsigned short csr = read_reg(priv, DELQA_CSR);

	printk("%s: %s: CSR %04x: %s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		dev->name,
		msg == NULL ? "" : msg, csr,
		(csr & DELQA_CSR_RCV_INT)     ? " RI" : "",
		(csr & DELQA_CSR_CARRIER)     ? " CA" : "",
		(csr & DELQA_CSR_POWERUP)     ? " OK" : "",
		(csr & DELQA_CSR_STIM_ENABLE) ? " SE" : "",
		(csr & DELQA_CSR_ELOOP)       ? " EL" : "",
		(csr & DELQA_CSR_ILOOP)       ? ""    : " IL", /* active low */
		(csr & DELQA_CSR_XMIT_INT)    ? " XI" : "",
		(csr & DELQA_CSR_INT_ENABLE)  ? " IE" : "",
		(csr & DELQA_CSR_RL_INVALID)  ? " RL" : "",
		(csr & DELQA_CSR_XL_INVALID)  ? " XL" : "",
		(csr & DELQA_CSR_LOAD_ROM)    ? " BD" : "",
		(csr & DELQA_CSR_NEX_MEM_INT) ? " NI" : "",
		(csr & DELQA_CSR_RESET)       ? " SR" : "",
		(csr & DELQA_CSR_RCV_ENABLE)  ? " RE" : "");
}

/*
   . -> Invalid
   + -> Valid, not yet used by DELQA
   * -> Valid, owned by DELQA (in progress)
   - -> Valid, processed by DELQA - no errors
   x -> Valid, processed by DELQA - with errors
   c -> Valid, chain descriptor
*/

static void dump_descs(struct net_device *dev)
{
	unsigned int i;
	struct delqa_private *priv = (struct delqa_private *)dev->priv;
	struct delqa_bufdesc *desc;

	printk("%s: TX free=%02d pending=%02d ", dev->name, priv->next_tx_free, priv->next_tx_pending);
	for (i=0; i<TXDESCS+1; i++) {
		desc = priv->descs->txdesc + i;
		if (desc->addr_hi & DELQA_ADDRHI_CHAIN) {
			printk("c");
		} else if (desc->addr_hi & DELQA_ADDRHI_VALID) {
			/* VALID bit set */
			switch (desc->status1 & (DELQA_TXSTS1_LASTNOT|DELQA_TXSTS1_ERRORUSED)) {
				case 0:
					printk("-");
					break;
				case DELQA_TXSTS1_ERRORUSED:
					printk("x");
					break;
				case DELQA_TXSTS1_LASTNOT:
					if (desc->flag & 0x4000) {
						printk("*");
					} else {
						printk("+");
					}
					break;
				case DELQA_TXSTS1_LASTNOT|DELQA_TXSTS1_ERRORUSED:
					/* Don't expect this, since we never break packets across buffers */
					printk("?");
					break;
			}
		} else {
			printk(".");
		}
	}
	printk("\n");

	printk("%s: RX next=%02d            ", dev->name, priv->next_rx);
	for (i=0; i<RXDESCS+1; i++) {
		desc = priv->descs->rxdesc + i;
		if (desc->addr_hi & DELQA_ADDRHI_CHAIN) {
			printk("c");
		} else if (desc->addr_hi & DELQA_ADDRHI_VALID) {
			/* VALID bit set */
			switch (desc->status1 & (DELQA_RXSTS1_LASTNOT|DELQA_RXSTS1_ERRORUSED)) {
				case 0:
					printk("-");
					break;
				case DELQA_RXSTS1_ERRORUSED:
					printk("x");
					break;
				case DELQA_RXSTS1_LASTNOT:
					if (desc->flag & 0x4000) {
						printk("*");
					} else {
						printk("+");
					}
					break;
				case DELQA_RXSTS1_LASTNOT|DELQA_RXSTS1_ERRORUSED:
					/* Don't expect this, since we never break packets across buffers */
					printk("?");
					break;
			}
		} else {
			printk(".");
		}
	}
	printk("\n");
}

static void delqa_tx_interrupt(struct net_device *dev, struct delqa_private *priv)
{
	struct delqa_bufdesc *desc;
	int desc_freed = 0;
	unsigned int tdr;
	unsigned int collisions;

	/* Get first descriptor waiting to be "taken back" from
	   the DELQA */
	desc = priv->descs->txdesc + priv->next_tx_pending;

	while (desc->status1 != DELQA_NOTYET) {

#if DELQA_DEBUG_PKT
		printk("TX desc %d, status1=%04x, status2=%04x\n", priv->next_tx_pending,
				desc->status1, desc->status2);
#endif

		tdr = (desc->status2 & DELQA_TXSTS2_TDR_MASK) >> DELQA_TXSTS2_TDR_SHIFT;
		collisions = (desc->status1 & DELQA_TXSTS1_COUNT_MASK) >> DELQA_TXSTS1_COUNT_SHIFT;

		if (desc->status1 & DELQA_TXSTS1_ERRORUSED) {

			priv->stats.tx_errors++;

			if (desc->status1 & DELQA_TXSTS1_LOSS) {
				printk(KERN_WARNING "%s: carrier lost on "
						"transmit - ethernet cable "
						"problem?\n", dev->name);
			}
			if (desc->status1 & DELQA_TXSTS1_NOCARRIER) {
				printk(KERN_WARNING "%s: no carrier on transmit"
						" - transceiver or transceiver "
						"cable problem?\n", dev->name);
			}
			if (desc->status1 & DELQA_TXSTS1_ABORT) {
				if (tdr == priv->last_tdr) {
					printk(KERN_WARNING "%s: excessive "
							"collisions on transmit\n",
							dev->name);
				} else {
					printk(KERN_WARNING"%s: excessive "
							"collisions on transmit"
							" - cable fault at "
							"TDR=%d\n", dev->name,
							tdr);
				}
				if (collisions == 0) {
					/* Collision counter overflowed */
					priv->stats.collisions += 16;
				}
			}
		} else {
			if (desc->addr_hi & DELQA_ADDRHI_SETUP) {
				/* Don't count setup frames in stats */
			} else {
				/* Packet got onto the wire */
				priv->stats.tx_packets++;
				priv->stats.tx_bytes += desc->buflen * 2;
			}
		}

		priv->stats.collisions += collisions;

		priv->last_tdr = tdr;

		if (desc->addr_hi & DELQA_ADDRHI_SETUP) {
			/* Setup frame - no associated skb */
		} else {
			dev_kfree_skb_irq(priv->tx_skb[priv->next_tx_pending]);
		}

		/* clear VALID bit */
		desc->addr_hi = 0;

		/* reclaim descriptor */
		desc->flag = DELQA_NOTYET;
		desc->status1 = DELQA_NOTYET;
		desc->status2 = 0;

		/* Free the mapping registers */
		qbus_unmap(priv->parent, priv->tx_map[priv->next_tx_pending]);

		/* At least one descriptor freed up */
		desc_freed = 1;

		priv->next_tx_pending++;
		if (priv->next_tx_pending == TXDESCS) {
			priv->next_tx_pending = 0;
		}
		desc = priv->descs->txdesc + priv->next_tx_pending;
	}

	if (netif_queue_stopped(dev) && desc_freed) {
		netif_wake_queue(dev);
	}
}

static void delqa_rx_interrupt(struct net_device *dev, struct delqa_private *priv)
{
	struct delqa_bufdesc *desc;
	unsigned int len;
	struct sk_buff *skb;
	unsigned int busaddr;

	/* Get first descriptor waiting to be "taken back" from
	   the DELQA */
	desc = priv->descs->rxdesc + priv->next_rx;

	while (desc->status1 != DELQA_NOTYET) {

#if DELQA_DEBUG_PKT
		printk("RX desc %d, status1=%04x, status2=%04x, len=%d\n", priv->next_rx,
				desc->status1, desc->status2, len);
#endif

		if (desc->status1 & DELQA_RXSTS1_ESETUP) {
			/* This is the loopback of a setup frame - ignore */
		} if (desc->status1 & DELQA_RXSTS1_ERRORUSED) {

			/* Error while receiving */
			priv->stats.rx_errors++;

		} else {
			/* Good frame received */

			unsigned int len_hi;
			unsigned int len_lo1;
			unsigned int len_lo2;

			len_hi = (desc->status1 & DELQA_RXSTS1_LEN_HI_MASK)
						>> DELQA_RXSTS1_LEN_HI_SHIFT;

			len_lo1 = (desc->status2 & DELQA_RXSTS2_LEN_LO1_MASK)
						>> DELQA_RXSTS2_LEN_LO1_SHIFT;

			len_lo2 = (desc->status2 & DELQA_RXSTS2_LEN_LO2_MASK)
						>> DELQA_RXSTS2_LEN_LO2_SHIFT;

			if (len_lo1 != len_lo2) {
				printk("%s: DELQA status2 bytes don't match\n", dev->name);
			}

			len = (len_hi << 8) + len_lo1 + 60;

			skb = dev_alloc_skb(len + 2);
			if (skb == NULL) {
				printk("%s: cannot allocate skb, dropping packet\n", dev->name);
				priv->stats.rx_dropped++;
			} else {
				priv->stats.rx_packets++;
				priv->stats.rx_bytes += len;

				skb->dev = dev;
				skb_reserve(skb, 2);
				skb_put(skb, len);
				memcpy(skb->data,
					priv->rx_map[priv->next_rx]->virtaddr, len);
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
			}
		}

		/* reclaim descriptor */
		desc->flag = DELQA_NOTYET;
		desc->status1 = DELQA_NOTYET;
		desc->status2 = 1; /* High and low bytes must be different */

		priv->next_rx++;
		if (priv->next_rx == RXDESCS) {
			priv->next_rx = 0;
		}
		desc = priv->descs->rxdesc + priv->next_rx;
	}

	/* DEQNA manual errata sheet states that we must check for an
	   invalid receive list before leaving the ISR and reset the
	   buffer list if it is invalid */
	if (read_reg(priv, DELQA_CSR) & DELQA_CSR_RL_INVALID) {

		printk("%s: receive list invalid - resetting\n", dev->name);
		/* The descriptor pointed to by next_rx must be the
		   first available descriptor.  This is because:

		   o  The DELQA doesn't touch the receive list when
		      RL_INVALID is set.

		   o  The while() loop above stops when next_rx points
		      to a 'NOTYET' descriptor. */

		busaddr = priv->desc_map->busaddr +
				offsetof(struct delqa_descs, rxdesc[priv->next_rx]);

		write_reg(priv, DELQA_RCLL, LOWORD(busaddr));
		write_reg(priv, DELQA_RCLH, HIWORD(busaddr));
	}

}

static irqreturn_t delqa_interrupt(const int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct delqa_private *priv = (struct delqa_private *)dev->priv;
	unsigned int csr;
	unsigned int newcsr;

	spin_lock(&priv->lock);

	csr = read_reg(priv, DELQA_CSR);

#if DELQA_DEBUG_CSR
	dump_csr("delqa_interrupt entry", dev);
#endif
#if DELQA_DEBUG_DESC
	dump_descs(dev);
#endif

	newcsr = DELQA_CSR_ILOOP | DELQA_CSR_RCV_ENABLE | DELQA_CSR_INT_ENABLE;

	if (csr & DELQA_CSR_XMIT_INT) {
		/* Either memory read error or tx interrupt */
		if (csr & DELQA_CSR_NEX_MEM_INT) {
			dump_csr("Q-bus memory error", dev);
			dump_descs(dev);
			qbus_dumpmap(priv->parent);

			/* FIXME: what should we do here? */
			panic("DELQA bus memory access error");
		} else {
			newcsr |= DELQA_CSR_XMIT_INT;
			delqa_tx_interrupt(dev, priv);
		}
	}

	if (csr & DELQA_CSR_RCV_INT) {
		newcsr |= DELQA_CSR_RCV_INT;
		delqa_rx_interrupt(dev, priv);
	}

	/* Clear RX and TX interrupt bits to allow further interrupts.  We
	   also enable the receiver and turn off the internal loopback at
	   this point.  */
	write_reg(priv, DELQA_CSR, newcsr);

#if DELQA_DEBUG_CSR
	dump_csr("delqa_interrupt exit", dev);
#endif

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static int delqa_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct delqa_private *priv = (struct delqa_private *)dev->priv;
	struct delqa_bufdesc *desc;
	unsigned int len;
	unsigned int i;
	unsigned int busaddr;
	unsigned int csr;
	unsigned int flags;

	if (skb->len < ETH_ZLEN) {
		struct sk_buff *new_skb;

		new_skb = skb_copy_expand(skb, 0, ETH_ZLEN - skb->len, GFP_ATOMIC);
		if (new_skb == NULL) {
			return -ENOMEM;
		}

		memset(skb_put(new_skb, ETH_ZLEN - skb->len), 0,
				ETH_ZLEN - skb->len);

		dev_kfree_skb(skb);

		/* We must not return a failure after this point, since that
		   would result in the caller trying to free the skb that we've
		   just freed. */

		skb = new_skb;
	}

	spin_lock_irqsave(&priv->lock, flags);

	i = priv->next_tx_free;
	priv->next_tx_free++;
	if (priv->next_tx_free == TXDESCS) {
		priv->next_tx_free = 0;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	desc = priv->descs->txdesc + i;

	/* FIXME: These mapping registers MUST be allocated at init time
	   to prevent any possibility of failure here - see above comment */

	priv->tx_map[i] = qbus_alloc_mapregs(priv->parent, skb->data, skb->len);
	if (priv->tx_map[i] == NULL) {
		/* FIXME: What should I do here? */
		panic("delqa_start_xmit: no map reg");
	}

	priv->tx_skb[i] = skb;

	busaddr = priv->tx_map[i]->busaddr;

	desc->addr_lo = LOWORD(busaddr);
	desc->addr_hi = HIWORD(busaddr) | DELQA_ADDRHI_EOMSG;
	desc->flag = DELQA_NOTYET;
	desc->status1 = DELQA_NOTYET;
	desc->status2 = 0;

	/* Work out the length and alignment stuff */

	len = skb->len;
	if ((len & 1) || ((unsigned long)(skb->data) & 1)) {
		len += 2;
	}
	if ((unsigned long)(skb->data) & 1) {
		desc->addr_hi |= DELQA_ADDRHI_ODDBEGIN;
	}
	if ((unsigned long)(skb->data + len) & 1) {
		desc->addr_hi |= DELQA_ADDRHI_ODDEND;
	}
	desc->buflen = -(len/2);

	/* Set the "go" bit on this descriptor */
	desc->addr_hi |= DELQA_ADDRHI_VALID;

#if DELQA_DEBUG_DESC
	dump_descs(dev);
#endif

	spin_lock_irqsave(&priv->lock, flags);

	csr = read_reg(priv, DELQA_CSR);
	if (csr & DELQA_CSR_XL_INVALID) {

		/* Get Q-bus address of first TX descriptor */
		busaddr = priv->desc_map->busaddr + offsetof(struct delqa_descs, txdesc[i]);

		write_reg(priv, DELQA_XMTL, LOWORD(busaddr));
		write_reg(priv, DELQA_XMTH, HIWORD(busaddr));
	}

	/* Check if the 'next' descriptor is actually free */
	desc = priv->descs->txdesc + priv->next_tx_free;

	if (desc->addr_hi & DELQA_ADDRHI_VALID) {
		/* All descriptors in use - stop tx queue */
		netif_stop_queue(dev);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

/* FIXME: implement this */

static void delqa_tx_timeout(struct net_device *dev)
{
	printk("delqa_tx_timeout not implemented\n");
HALT;
}

static void store_setup_address(unsigned int index, unsigned char *addr,
	unsigned char *setup_frame)
{
	unsigned int start;
	unsigned int i;

	if (index < 6) {
		start = index + 1;
	} else {
		start = index + 65;
	}

	for (i=0; i<6; i++) {
		setup_frame[start + i * 8] = addr[i];
	}
}

static void format_setup_frame(struct net_device *dev)
{
	struct delqa_private *priv = (struct delqa_private *)dev->priv;

	/* Fill in hardware, broadcast and multicast addresses here */

	/* Pre-fill with all FF (has side effect of setting all addresses
	   to the broadcast address) */
	memset(priv->setup_frame, 0xff, sizeof(priv->setup_frame));

	/* First address will be our unicast address */
	store_setup_address(0, dev->dev_addr, priv->setup_frame);

	/* FIXME: Store multicast addresses here */

	/* FIXME: Use MULTICAST and PROMISC flags to tweak setup_len */
	priv->setup_frame_len = 128;
}

/* FIXME: pre-allocate mapping registers for this at init time.  Then this
   would be a no-fail function */

static void queue_setup_frame(struct net_device *dev)
{
	struct delqa_private *priv = (struct delqa_private *)dev->priv;
	struct delqa_bufdesc *desc;
	unsigned int busaddr;
	unsigned int csr;
	unsigned int i;
	unsigned int flags;

	/* Point the first available TX descriptor at the setup
	   frame and enable the transmitter */

	spin_lock_irqsave(&priv->lock, flags);

	i = priv->next_tx_free;

	priv->next_tx_free++;
	if (priv->next_tx_free == TXDESCS) {
		priv->next_tx_free = 0;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	desc = priv->descs->txdesc + i;

	priv->tx_map[i] = qbus_alloc_mapregs(priv->parent, priv->setup_frame, priv->setup_frame_len);
	if (priv->tx_map[i] == NULL) {
		panic("delqa queue_setup_frame: dma mapping failed");
	}

	busaddr = priv->tx_map[i]->busaddr;

	desc->addr_lo = LOWORD(busaddr);
	desc->addr_hi = HIWORD(busaddr) | DELQA_ADDRHI_SETUP | DELQA_ADDRHI_EOMSG;
	desc->flag = DELQA_NOTYET;
	desc->status1 = DELQA_NOTYET;
	desc->status2 = 0;
	desc->buflen = -priv->setup_frame_len/2; /* (maybe) bytes, not words like all other uses */
	desc->addr_hi |= DELQA_ADDRHI_VALID;

#if DELQA_DEBUG_DESC
	dump_descs(dev);
#endif

	spin_lock_irqsave(&priv->lock, flags);

	csr = read_reg(priv, DELQA_CSR);

	if (csr & DELQA_CSR_XL_INVALID) {

		/* Get Q-bus address of first TX descriptor */
		busaddr = priv->desc_map->busaddr + offsetof(struct delqa_descs, txdesc[i]);

		write_reg(priv, DELQA_XMTL, LOWORD(busaddr));
		write_reg(priv, DELQA_XMTH, HIWORD(busaddr));
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void delqa_reset(struct net_device *dev)
{
	struct delqa_private *priv = (struct delqa_private *)dev->priv;
	unsigned int csr;

	printk("%s: resetting DELQA... ", dev->name);

	write_reg(priv, DELQA_CSR, DELQA_CSR_RESET);
	udelay(1000);

	csr = read_reg(priv, DELQA_CSR);
	write_reg(priv, DELQA_CSR, csr & ~DELQA_CSR_RESET);
	write_reg(priv, DELQA_VECTOR, priv->qbus_vector);

	printk("done\n");
}

static int delqa_open(struct net_device *dev)
{
	struct delqa_private *priv = (struct delqa_private *)dev->priv;
	struct delqa_bufdesc *desc;
	int i;
	unsigned int busaddr;

	/* Reset the hardware before hooking the interrupt vector
	   to guarantee that we won't get any interrupts until we
	   enable them. */

	delqa_reset(dev);

	if (qbus_request_irq(priv->parent, priv->qbus_vector, delqa_interrupt, 0, "delqa", dev)) {
		printk("delqa_open: cannot get qbus irq %d\n", priv->qbus_vector);
		return -EAGAIN;
	}

	/* Mark the transmit descriptors as not yet owned by
	   the DELQA (and also not VALID). */
	for (i=0; i<TXDESCS; i++) {
		desc = priv->descs->txdesc + i;

		/* Clear VALID bit */
		desc->addr_hi = 0;
		desc->flag = DELQA_NOTYET;
		desc->status1 = DELQA_NOTYET;
		desc->status2 = 0;
	}

	/* Mark the receive descriptors as not yet owned by
	   the DELQA. */
	for (i=0; i<RXDESCS; i++) {
		desc = priv->descs->rxdesc + i;

		desc->flag = DELQA_NOTYET;
		desc->status1 = DELQA_NOTYET;
		desc->status2 = 1;
	}

	/* Tell the DELQA where the receive descriptors live (i.e.
	   which Q-bus addresses are mapped to the descriptor
	   addresses by the mapping registers.  There is no
           point in setting the transmit descriptor address, since
	   there are no valid transmit descriptors yet.  When
	   the card hits an invalid transmit descriptor, it stops
	   the transmit logic, which can only be restarted by
	   setting the transmit descriptor address again. */

	busaddr = priv->desc_map->busaddr + offsetof(struct delqa_descs, rxdesc[0]);

	write_reg(priv, DELQA_RCLL, LOWORD(busaddr));
	write_reg(priv, DELQA_RCLH, HIWORD(busaddr));

	write_reg(priv, DELQA_CSR, DELQA_CSR_INT_ENABLE | DELQA_CSR_XMIT_INT | DELQA_CSR_RCV_INT);

	format_setup_frame(dev);
	queue_setup_frame(dev);

	return 0;
}

/* FIXME: implement delqa_close */

static int delqa_close(struct net_device *dev)
{
	printk("delqa_close not implemented\n");
HALT;
	return 0;
}

static void delqa_set_multicast(struct net_device *dev)
{
	format_setup_frame(dev);
	queue_setup_frame(dev);
}

static struct net_device_stats *delqa_get_stats(struct net_device *dev)
{
        struct delqa_private *priv = (struct delqa_private *) dev->priv;

        return &priv->stats;
}

/* This function allocates the receive buffers, allocates and maps
   QBUS mapping registers for these buffers, initializes the receive
   descriptors to point to these buffers, and sets up the chain
   descriptors at the end of the descriptor lists */

static int init_desc_rings(struct net_device *dev)
{
	struct delqa_private *priv = (struct delqa_private *)dev->priv;
	struct delqa_bufdesc *desc;
	unsigned int busaddr;
	int i;

	for (i=0; i<RXDESCS; i++) {
		unsigned char *buf;

		buf = kmalloc(RXBUFSIZE, GFP_KERNEL);
		if (buf == NULL) {
			printk("delqa: Cannot allocate RX buf");
			goto cleanup;
		}

		priv->rx_map[i] = qbus_alloc_mapregs(priv->parent, buf, RXBUFSIZE);
		if (priv->rx_map[i] == NULL) {
			printk("delqa init_desc_rings: dma mapping failed");
			kfree(buf);
			goto cleanup;
		}

		busaddr = priv->rx_map[i]->busaddr;

		desc = priv->descs->rxdesc + i;

		desc->addr_lo = LOWORD(busaddr);
		desc->addr_hi = HIWORD(busaddr);
		desc->flag = DELQA_NOTYET;
		desc->status1 = DELQA_NOTYET;
		desc->status2 = 1;
		desc->buflen = - (RXBUFSIZE / 2); /* words, not bytes */
		desc->addr_hi |= DELQA_ADDRHI_VALID;
	}

	/* Remember that we've allocated one more descriptor than we
	   need.  This one is used to chain the end of the descriptor
	   list back to the beginning.  */

	/* Last receive descriptor contains bus address of first desc */

	desc = priv->descs->rxdesc + RXDESCS;

	busaddr = priv->desc_map->busaddr + offsetof(struct delqa_descs, rxdesc[0]);

	desc->addr_lo = LOWORD(busaddr);
	desc->addr_hi = HIWORD(busaddr) | DELQA_ADDRHI_VALID | DELQA_ADDRHI_CHAIN;
	desc->flag = DELQA_NOTYET;
	desc->status1 = DELQA_NOTYET;

	/* Last transmit descriptor contains bus address of first desc */

	desc = priv->descs->txdesc + TXDESCS;

	busaddr = priv->desc_map->busaddr + offsetof(struct delqa_descs, txdesc[0]);

	desc->addr_lo = LOWORD(busaddr);
	desc->addr_hi = HIWORD(busaddr) | DELQA_ADDRHI_VALID | DELQA_ADDRHI_CHAIN;
	desc->flag = DELQA_NOTYET;
	desc->status1 = DELQA_NOTYET;

	priv->next_tx_free = 0;
	priv->next_tx_pending = 0;
	priv->next_rx = 0;

	return 0;

cleanup:
	for (i=0; i<RXDESCS; i++) {
		if (priv->rx_map[i] != NULL) {
			kfree(priv->rx_map[i]->virtaddr);
			qbus_unmap(priv->parent, priv->rx_map[i]);
			priv->rx_map[i] = NULL;
		}
	}
	return -ENOMEM;
}

static int delqa_probe(struct qbus_device *qbus_dev)
{
	struct net_device *dev;
	struct delqa_private *priv;
	int i;
	int status = 0;

	switch (QBUS_OCTAL_CSR(qbus_dev->csr)) {
		case DELQA_CSR_BUS_ADDR:
			/* This could be a DELQA */
			break;
		default:
			/* Not one of our expected CSR addresses */
			return 1;
	}

	dev = alloc_etherdev(sizeof(struct delqa_private));
	if (!dev) {
		return -ENOMEM;
	}

	priv = (struct delqa_private *) dev->priv;

	spin_lock_init(&priv->lock);

	priv->parent = get_device(qbus_dev->dev.parent);

	priv->base = qbus_ioremap(priv->parent, qbus_dev->csr, 16);
	if (priv->base == NULL) {
		status = -ENOMEM;
		goto cleanup;
	}

	priv->descs = kmalloc(sizeof(struct delqa_descs), GFP_KERNEL);
	if (priv->descs == NULL) {
		status = -ENOMEM;
		goto cleanup;
	}

	dev->mem_start = (unsigned int)priv->descs;
	dev->mem_end = (unsigned int)priv->descs + sizeof(*(priv->descs));

	priv->qbus_vector = qbus_alloc_vector(priv->parent);
	if (!priv->qbus_vector) {
		printk("delqa_probe: cannot allocate QBUS interrupt vector\n");
		status = -EAGAIN;
		goto cleanup;
	}

	write_reg(priv, DELQA_VECTOR, priv->qbus_vector);

	printk("delqa qbus vector: %d (0%03o, 0x%04x)\n", priv->qbus_vector, priv->qbus_vector, priv->qbus_vector);

	/* This is purely informational */
	dev->irq = qbus_vector_to_irq(priv->parent, priv->qbus_vector);

        printk("Ethernet address in ROM: ");
        for (i = 0; i < 6; i++) {
                dev->dev_addr[i] = priv->base[i*2] & 0xff;
                printk("%2.2x%c", dev->dev_addr[i], i == 5 ? '\n' : ':');
        }

	/* Here we need to setup qbus mapping registers so that the DELQA
	   can DMA to and from our buffers */

	priv->desc_map = qbus_alloc_mapregs(priv->parent, priv->descs, sizeof(struct delqa_descs));
	if (priv->desc_map == NULL) {
		status = -ENOMEM;
		goto cleanup;
	}

	status = init_desc_rings(dev);
	if (status < 0) {
		goto cleanup;
	}

        dev->open = delqa_open;
        dev->stop = delqa_close;
        dev->hard_start_xmit = delqa_start_xmit;
        dev->tx_timeout = delqa_tx_timeout;
        dev->watchdog_timeo = 5*HZ;
        dev->get_stats = &delqa_get_stats;
        dev->set_multicast_list = &delqa_set_multicast;
        dev->dma = 0;

	SET_NETDEV_DEV(dev, &qbus_dev->dev);

	status = register_netdev(dev);
	if (status) {
		goto cleanup;
	}

	return 0;

cleanup:
	if (priv->desc_map) {
		qbus_unmap(qbus_dev->dev.parent, priv->desc_map);
	}
	if (priv->qbus_vector) {
		qbus_free_vector(qbus_dev->dev.parent, priv->qbus_vector);
	}
	kfree(priv->descs);
	if (priv->base) {
		qbus_iounmap(priv->base);
	}
	if (priv->parent) {
		put_device(priv->parent);
	}

	free_netdev(dev);

	return status;
}

static struct qbus_driver delqa_driver = {
	.probe		= delqa_probe,
	.drv = {
		.name	= "delqa",
	},
};

int __init delqa_init(void)
{
	return qbus_register_driver(&delqa_driver);
}

device_initcall(delqa_init);


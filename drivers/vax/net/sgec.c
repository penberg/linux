/*
 *	SGEC ethernet driver.  Reported as EZA0 etc by VAX Console.
 *
 *      SGEC stands for Second Generation Ethernet Card, and is the
 *         replacement for the LANCE adapters in the MicroVaxen.
 *
 *      Loosely adapted from vaxlance.c by Dave Airlie
 *              by Richard Banks, Aug 2001
 *
 */

/* NOTE to self - look at code in arch/vax/if/ *ze*, arch/vax/vsa/ *ze*, and dev/ic/ *sgec* */


static char *version = "vaxsgec.c: v0.001 by Richard Banks\n";

static char *sgecstr = "SGEC";

#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ioport.h>        /* for autoirq_setup/_report */

#include <asm/pgalloc.h>         /* for __flush_tlb_one */
#include <asm/bus/vsbus.h>
#include <asm/mv.h>

/* use #undef to turn these off */
#define VAX_SGEC_DEBUG
#define VAX_SGEC_DEBUG_BUFFERS
#define VAX_SGEC_AUTOPROBE_IRQ

#define CRC_POLYNOMIAL_BE 0x04c11db7UL  /* Ethernet CRC, big endian */
#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */


/* SGEC CSRs */
struct sgec_regs {
  unsigned long sg_nicsr0;	/* vector address, IPL, sync mode */
  unsigned long sg_nicsr1;	/* TX poll demand */
  unsigned long sg_nicsr2;	/* RX poll demand */
  struct sgec_rx_desc *sg_nicsr3;	/* RX descriptor list address */
  struct sgec_tx_desc *sg_nicsr4;	/* TX descriptor list address */
  unsigned long sg_nicsr5;	/* SGEC status */
  unsigned long sg_nicsr6;	/* SGEC command/mode */
  unsigned long sg_nicsr7;	/* system page table base address */
  unsigned long sg_nivcsr8;	/* reserved virtual CSR */
  unsigned long sg_nivcsr9;	/* watchdog timers (virtual) */
  unsigned long sg_nivcsr10;	/* revision, missed frame count (v) */
  unsigned long sg_nivcsr11;	/* boot message verification (low) (v) */
  unsigned long sg_nivcsr12;	/* boot message verification (high) (v) */
  unsigned long sg_nivcsr13;	/* boot message processor (v) */
  unsigned long sg_nivcsr14;	/* diagnostic breakpoint (v) */
  unsigned long sg_nicsr15;	/* monitor command */
};

/* SGEC bit definitions */
/* NICSR0: */
#define SG_NICSR0_IPL 0xc0000000	/* interrupt priority level: */
#define SG_NICSR0_IPL14 0x00000000	/* 0x14 */
#define SG_NICSR0_IPL15 0x40000000	/* 0x15 */
#define SG_NICSR0_IPL16 0x80000000	/* 0x16 */
#define SG_NICSR0_IPL17 0xc0000000	/* 0x17 */
#define SG_NICSR0_SA 0x20000000		/* sync(1)/async mode */
#define SG_NICSR0_MBO 0x1fff0003	/* must be set to one on write */
#define SG_NICSR0_IV_MASK 0x0000fffc	/* bits for the interrupt vector */

/* NICSR1: */
#define SG_NICSR1_TXPD 0xffffffff	/* transmit polling demand */

/* NICSR2: */
#define SG_NICSR2_RXPD 0xffffffff	/* receive polling demand */

/* NICSR3 and NICSR4 are pure addresses */
/* NICSR5: */
#define SG_NICSR5_ID 0x80000000		/* init done */
#define SG_NICSR5_SF 0x40000000		/* self-test failed */
#define SG_NICSR5_SS 0x3c000000		/* self-test status field */
#define SG_NICSR5_TS 0x03000000		/* transmission state: */
#define SG_NICSR5_TS_STOP 0x00000000	/* stopped */
#define SG_NICSR5_TS_RUN 0x01000000	/* running */
#define SG_NICSR5_TS_SUSP 0x02000000	/* suspended */
#define SG_NICSR5_RS 0x00c00000		/* reception state: */
#define SG_NICSR5_RS_STOP 0x00000000	/* stopped */
#define SG_NICSR5_RS_RUN 0x00400000	/* running */
#define SG_NICSR5_RS_SUSP 0x00800000	/* suspended */
#define SG_NICSR5_OM 0x00060000		/* operating mode: */
#define SG_NICSR5_OM_NORM 0x00000000	/* normal */
#define SG_NICSR5_OM_ILBK 0x00020000	/* internal loopback */
#define SG_NICSR5_OM_ELBK 0x00040000	/* external loopback */
#define SG_NICSR5_OM_DIAG 0x00060000	/* reserved for diags */
#define SG_NICSR5_DN 0x00010000		/* virtual CSR access done */
#define SG_NICSR5_MBO 0x0038ff00	/* must be one */
#define SG_NICSR5_BO 0x00000080		/* boot message received */
#define SG_NICSR5_TW 0x00000040		/* transmit watchdog timeout */
#define SG_NICSR5_RW 0x00000020		/* receive watchdog timeout */
#define SG_NICSR5_ME 0x00000010		/* memory error */
#define SG_NICSR5_RU 0x00000008		/* receive buffer unavailable */
#define SG_NICSR5_RI 0x00000004		/* receiver interrupt */
#define SG_NICSR5_TI 0x00000002		/* transmitter interrupt */
#define SG_NICSR5_IS 0x00000001		/* interrupt summary */
/* whew! */

/* NICSR6: */
#define SG_NICSR6_RE 0x80000000		/* reset */
#define SG_NICSR6_IE 0x40000000		/* interrupt enable */
#define SG_NICSR6_MBO 0x01e7f000	/* must be one */
#define SG_NICSR6_BL 0x1e000000		/* burst limit mask */
#define SG_NICSR6_BL_8 0x10000000	/* 8 longwords */
#define SG_NICSR6_BL_4 0x08000000	/* 4 longwords */
#define SG_NICSR6_BL_2 0x04000000	/* 2 longwords */
#define SG_NICSR6_BL_1 0x02000000	/* 1 longword */
#define SG_NICSR6_BE 0x00100000		/* boot message enable */
#define SG_NICSR6_SE 0x00080000		/* single cycle enable */
#define SG_NICSR6_ST 0x00000800		/* start(1)/stop(0) transmission */
#define SG_NICSR6_SR 0x00000400		/* start(1)/stop(0) reception */
#define SG_NICSR6_OM 0x00000300		/* operating mode: */
#define SG_NICSR6_OM_NORM 0x00000000	/* normal */
#define SG_NICSR6_OM_ILBK 0x00000100	/* internal loopback */
#define SG_NICSR6_OM_ELBK 0x00000200	/* external loopback */
#define SG_NICSR6_OM_DIAG 0x00000300	/* reserved for diags */
#define SG_NICSR6_DC 0x00000080		/* disable data chaining */
#define SG_NICSR6_FC 0x00000040		/* force collision mode */
#define SG_NICSR6_PB 0x00000008		/* pass bad frames */
#define SG_NICSR6_AF 0x00000006		/* address filtering mode: */
#define SG_NICSR6_AF_NORM 0x00000000	/* normal filtering */
#define SG_NICSR6_AF_PROM 0x00000002	/* promiscuous mode */
#define SG_NICSR6_AF_ALLM 0x00000004	/* all multicasts */

/* NICSR7 is an address, NICSR8 is reserved */
/* NICSR9: */
#define SG_VNICSR9_RT 0xffff0000	/* receiver timeout, *1.6 us */
#define SG_VNICSR9_TT 0x0000ffff	/* transmitter timeout */

/* NICSR10: */
#define SG_VNICSR10_RN 0x001f0000	/* SGEC version */
#define SG_VNICSR10_MFC 0x0000ffff	/* missed frame counter */



/* SGEC Descriptor defines */
struct sgec_rx_desc{
  unsigned short  word0;       /* descriptor word 0 */
  unsigned short  framelen;    /* frame length */
  unsigned char   rsvd[3];     /* unused */
  unsigned char   word1;       /* descriptor word 1 - flags */
  short           page_offset; /* buffer page offset */
  short           buffsize;    /* buffer size */
  unsigned char  *buffaddr;    /* buffer address */
};

#define SG_FR_OWN       0x8000    /* We own the descriptor */
#define SG_R0_ERR       0x8000    /* an error occurred */
#define SG_R0_LEN       0x4000    /* length error */
#define SG_R0_DAT       0x3000    /* data type (next 3 are subtypes) */
#define SG_R0_DAT_NORM  0x0000        /* normal frame */
#define SG_R0_DAT_INLB  0x1000        /* internal loop back */
#define SG_R0_DAT_EXLB  0x2000        /* external loop back */
#define SG_R0_FRA       0x0800    /* runt frame */
#define SG_R0_OFL       0x0400    /* buffer overflow */
#define SG_R0_FSG       0x0200    /* first segment */
#define SG_R0_LSG       0x0100    /* last segment */
#define SG_R0_LNG       0x0080    /* frame too long */
#define SG_R0_COL       0x0040    /* collision seen */
#define SG_R0_EFT       0x0020    /* etherenet frame type */
#define SG_R0_TNV       0x0008    /* address translation not valid */
#define SG_R0_DRB       0x0004    /* saw some dribbling bits */
#define SG_R0_CRC       0x0002    /* CRC error */
#define SG_R0_FFO       0x0001    /* fifo overflow */
#define SG_R1_CAD       0x80      /* chain address */
#define SG_R1_VAD       0x40      /* virtual address */
#define SG_R1_VPA       0x20      /* virtual/physical PTE address */

/* Transmit descriptor */
struct sgec_tx_desc {
  unsigned short word0;		/* descriptor word 0 flags */
  unsigned short tdr;		/* TDR count of cable fault */
  unsigned char  rsvd1[2];	/* unused bytes */
  unsigned short word1;		/* descriptor word 1 flags */
  short          pageoffset;	/* offset of buffer in page */
  short          bufsize;	/* length of data buffer */
  unsigned char *bufaddr;	/* address of data buffer */
};

/* Receive descriptor bits */
#define SG_TDR_OWN 0x8000		/* SGEC owns this descriptor */
#define SG_TD0_ES 0x8000		/* an error has occurred */
#define SG_TD0_TO 0x4000		/* transmit watchdog timeout */
#define SG_TD0_LE 0x1000		/* length error */
#define SG_TD0_LO 0x0800		/* loss of carrier */
#define SG_TD0_NC 0x0400		/* no carrier */
#define SG_TD0_LC 0x0200		/* late collision */
#define SG_TD0_EC 0x0100		/* excessive collisions */
#define SG_TD0_HF 0x0080		/* heartbeat fail */
#define SG_TD0_CC 0x0078		/* collision count mask */
#define SG_TD0_TN 0x0004		/* address translation invalid */
#define SG_TD0_UF 0x0002		/* underflow */
#define SG_TD0_DE 0x0001		/* transmission deferred */
#define SG_TD1_CA 0x8000		/* chain address */
#define SG_TD1_VA 0x4000		/* virtual address */
#define SG_TD1_DT 0x3000		/* data type: */
#define SG_TD1_DT_NORM 0x0000		/* normal transmit frame */
#define SG_TD1_DT_SETUP 0x2000		/* setup frame */
#define SG_TD1_DT_DIAG 0x3000		/* diagnostic frame */
#define SG_TD1_AC 0x0800		/* CRC disable */
#define SG_TD1_FS 0x0400		/* first segment */
#define SG_TD1_LS 0x0200		/* last segment */
#define SG_TD1_POK 0x0600               /* packet OK to send - first and last segment set */
#define SG_TD1_IC 0x0100		/* interrupt on completion */
#define SG_TD1_VT 0x0080		/* virtual(1)/phys PTE address */

/*
 * Adresses.
 */
#define NISA_ROM	(				\
		{					\
			unsigned long __addr;		\
			if (is_ka49 ())			\
				/* VS 4000m90 */	\
				__addr = 0x27800000;	\
			else				\
				/* QBUS 3100/85 */	\
				__addr = 0x20084000;	\
							\
			__addr;				\
		})

/*
 * Register offsets
 */
#define SG_CSR0         0
#define SG_CSR1         4
#define SG_CSR2         8
#define SG_CSR3         12
#define SG_CSR4         16
#define SG_CSR5         20
#define SG_CSR6         24
#define SG_CSR7         28
#define SG_CSR8         32
#define SG_CSR9         36
#define SG_CSR10        40
#define SG_CSR11        44
#define SG_CSR12        48
#define SG_CSR13        52
#define SG_CSR14        56
#define SG_CSR15        60

/* must be an even number of receive/transmit descriptors */
#define RXDESCS 30            /* no of receive descriptors */
#define TXDESCS 60            /* no of transmit descriptors */

#define TX_RING_SIZE	        60
#define TX_RING_MOD_MASK	59

#define RX_RING_SIZE		30
#define RX_RING_MOD_MASK	29

#define PKT_BUF_SZ		1536
#define RX_BUFF_SIZE            PKT_BUF_SZ
#define TX_BUFF_SIZE            PKT_BUF_SZ


/* First part of the SGEC initialization block, described in databook. */
struct sgec_init_block {
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
	struct   sgec_rx_desc  brx_ring[RX_RING_SIZE];
	struct   sgec_tx_desc  btx_ring[TX_RING_SIZE];
};


#define BUF_OFFSET_CPU (offsetof(struct sgec_shared_mem, rx_buf))
#define BUF_OFFSET_LNC BUF_OFFSET_CPU


/* This is how our shared memory block is layed out */

struct sgec_shared_mem {
	struct sgec_init_block init_block;  /* Includes RX/TX descriptors */
	char rx_buf[RX_RING_SIZE][RX_BUFF_SIZE];
	char tx_buf[RX_RING_SIZE][RX_BUFF_SIZE];
};

struct sgec_private {
  char *name;
  /* location of registers */
  volatile struct sgec_regs *ll;
  /* virtual addr of shared memory block */
  volatile struct sgec_shared_mem *sgec_mem;
  /* virtual addr of block inside shared mem block */
  volatile struct sgec_init_block *init_block;
  unsigned char   vsbus_int;
  spinlock_t	lock;
  int rx_new, tx_new;
  int rx_old, tx_old;
  struct net_device_stats	stats;
  unsigned short busmaster_regval;
  struct net_device *dev;	/* Backpointer        */
  struct sgec_private *next_module;
  struct timer_list       multicast_timer;
};

#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			lp->tx_old+TX_RING_MOD_MASK-lp->tx_new:\
			lp->tx_old - lp->tx_new-1)


static inline void writereg(volatile unsigned short *regptr, unsigned long value)
{
  *regptr = value;
}

static inline void writecsr6(volatile struct sgec_regs *ll, unsigned long value)
{
  /* was &ll->sg_nicsr6 */
  writereg((volatile unsigned short *)&ll->sg_nicsr6, value);
}

static inline void sgec_stop(volatile struct sgec_regs *ll)
{
  writecsr6(ll, ll->sg_nicsr6 & ~SG_NICSR6_ST);  /* stop transmission */
  writecsr6(ll, ll->sg_nicsr6 & ~SG_NICSR6_SR);  /* stop receiving */
  udelay(100);
}

/* Load the CSR registers */
static void load_csrs(struct sgec_private *lp)
{
	volatile struct sgec_regs *ll = lp->ll;
	//	unsigned long leptr;

	writecsr6(ll, SG_NICSR6_IE|SG_NICSR6_BL_8|SG_NICSR6_ST|SG_NICSR6_SR|SG_NICSR6_DC);
}

static inline void cp_to_buf(void *to, const void *from, __kernel_size_t len)
{
	memcpy(to, from, len);
}

static inline void cp_from_buf(void *to, unsigned char *from, int len)
{
	memcpy(to, from, len);
}

static void sgec_init_ring(struct net_device *dev)
{
	struct sgec_private *lp = (struct sgec_private *) dev->priv;
	volatile struct sgec_init_block *ib = lp->init_block;
	unsigned long leptr;
	int i;

	/* Lock out other processes while setting up hardware */

	netif_stop_queue(dev);
	lp->rx_new = lp->tx_new = 0;
	lp->rx_old = lp->tx_old = 0;

	/* Copy the ethernet address to the sgec init block.
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
	/*	ib->rx_len = (SGEC_LOG_RX_BUFFERS << 13) | (leptr >> 16);
		ib->rx_ptr = leptr; */

#ifdef VAX_SGEC_DEBUG
	//	printk("RX ptr: %8.8lx(%8.8x)\n", leptr, ib->brx_ring);
#endif
	/* Setup tx descriptor pointer */

	/* Calculate the physical address of the first transmit descriptor */
	leptr = virt_to_phys(ib->btx_ring);
	/* ib->tx_len = (SGEC_LOG_TX_BUFFERS << 13) | (leptr >> 16);
	   ib->tx_ptr = leptr; */

#ifdef VAX_SGEC_DEBUG
	//printk("TX ptr: %8.8lx(%8.8x)\n", leptr, ib->btx_ring);

	printk("TX rings:\n");
#endif
	/* Setup the Tx ring entries */
	for (i = 0; i < TX_RING_SIZE; i++) {
		leptr = virt_to_phys(lp->sgec_mem->tx_buf[i]) & 0xffffff;
		//		ib->btx_ring[i].framelen = SG_FR_OWN;
#ifdef VAX_SGEC_DEBUG
		if (i < 3)
			printk("%d: 0x%8.8lx(0x%8.8x)\n", i, leptr, (int) lp->sgec_mem->tx_buf[i]);
#endif
	}

	/* Setup the Rx ring entries */
#ifdef VAX_SGEC_DEBUG
	printk("RX rings:\n");
#endif
	for (i = 0; i < RX_RING_SIZE; i++) {
		leptr = virt_to_phys(lp->sgec_mem->rx_buf[i]) & 0xffffff;
#ifdef VAX_SGEC_DEBUG
		if (i < 3)
			printk("%d: 0x%8.8lx(0x%8.8x)\n", i, leptr, (int) lp->sgec_mem->rx_buf[i]);
#endif
	}
}

static int init_restart_sgec(struct sgec_private *lp)
{
	volatile struct sgec_regs *ll = lp->ll;
	int i;
	int reg;

	udelay(100);

	reg = ll->sg_nicsr6;
	writecsr6(ll, SG_NICSR6_RE);  /* reset card */

	/* Wait for the sgec to complete initialization */
	for (i = 0; (i < 100) && !(ll->sg_nicsr5 & SG_NICSR5_ID); i++) {
#ifdef VAX_SGEC_DEBUG
		printk("SGEC opened maybe %d\n", i);
#endif
		udelay(10);
	}
	if ((i == 100) || (ll->sg_nicsr5 & SG_NICSR5_SF)) {
#ifdef VAX_SGEC_DEBUG
	  //		printk("SGEC unopened after %d ticks, csr0=%4.4x.\n", i, ll->sg_nicsr5);
#endif
		return -1;
	}
	if ((ll->sg_nicsr5 & SG_NICSR5_SF)) {
#ifdef VAX_SGEC_DEBUG
	  //	printk("SGEC unopened after %d ticks, csr0=%4.4x.\n", i, ll->sg_nicsr5);
#endif
		return -1;
	}
#ifdef VAX_SGEC_DEBUG
	printk("SGEC opened maybe\n");
#endif

	writecsr6(ll, SG_NICSR6_IE | SG_NICSR6_BL_8|SG_NICSR6_ST|SG_NICSR6_SR|SG_NICSR6_DC);
	return 0;
}


static int sgec_rx(struct net_device *dev)
{
  struct sgec_private *lp = (struct sgec_private *)dev->priv;
  volatile struct sgec_init_block *ib = lp->init_block;
  volatile struct sgec_rx_desc *rd = 0;
  unsigned char bits;
  int len = 0;
  struct sk_buff *skb = 0;

#ifdef SGEC_DEBUG_BUFFERS
  int i;

  printk("[");
  for (i=0; i < RX_RING_SIZE; i==){
    if (i == lp->rx_new)
      printk("%s",ib->brx_ring[i].framelen & SG_FR_OWN ? "_" : "X");
    else
      printk("%s",ib->brx_ring[i].framelen & SG_FR_OWN ? "." : "1");
  }
  printk("]");
#endif

  for (rd=&ib->brx_ring[lp->rx_new];
       !((bits = rd->framelen) & SG_FR_OWN);
       rd=&ib->brx_ring[lp->rx_new]){

    /*
     * check for incomplete frame
    if ((bits & SG_R0_POK) != SG_R0_POK) {
      lp->stats.rx_over_errors ++;
      lp->stats_rx_errors++;
    }
    else if (bits & SG_R0_ERR) {
      * only count last frame as the error
      if (bits & SG_R0_BUF) lp->stats.rx_fifo_errors++;
      if (bits & SG_R0_CRC) lp->stats.rx_crc_errors++;
      if (bits & SG_R0_OFL) lp->stats.rx_over_errors++;
      if (bits & SG_R0_FRA) lp->stats.rx_frame_errors++;
      if (bits & SG_R0_EOP) lp->stats.rx_errors++;
    } else { */
      len = rd->framelen;
      skb = dev_alloc_skb((unsigned int)len + 2);
      if (skb == 0) {
	printk("%s: SGEC Deferring Packet\n", dev->name);
	lp->stats.rx_dropped++;
	//rd->mblength = 0;
	rd->framelen = SG_FR_OWN;
	lp->rx_new =(lp->rx_new+1) & RX_RING_MOD_MASK;
	return 0;
	/*      } */
      lp->stats.rx_bytes += len;
      skb->dev = dev;
      skb_reserve(skb,2);  /*16 byte align */

      skb_put(skb, len);	/* make room */
      cp_from_buf(skb->data,
		  (char *) lp->sgec_mem->rx_buf[lp->rx_new],
		  len);
      skb->protocol = eth_type_trans(skb,dev);
      netif_rx(skb);
      dev->last_rx=jiffies;
      lp->stats.rx_packets++;
    }
      //   rd->mblength=0;
    rd->framelen = len;
    rd->framelen &= SG_FR_OWN;
    lp->rx_new = (lp->rx_new + 1) & RX_RING_MOD_MASK;
  }
  return 0;
}

static void sgec_tx(struct net_device *dev)
{
  struct sgec_private *lp = (struct sgec_private *) dev->priv;
  volatile struct sgec_init_block *ib = lp->init_block;
  volatile struct sgec_regs *ll = lp->ll;
  volatile struct sgec_tx_desc *td;
  int i, j;
  j = lp->tx_old;

  spin_lock(&lp->lock);

  for (i = j; i != lp->tx_new; i = j) {
    td = &ib->btx_ring[i];
    /* If we hit a packet not owned by us, stop */
    if (td->word1 & SG_FR_OWN) break;
    /*    if (td->tmd1_bits & SG_T0_ERR) {
      status = td->misc;
      lp->stats.tx_errors++;
      if (status & LE_T3_RTY) lp->stats.tx_aborted_errors++;
      if (status & LE_T3_LCOL) lp->stats.tx_window_errors++;
      if (status & LE_T3_CLOS) {
	lp->stats.tx_carrier_errors++;
	printk("%s: Carrier Lost", dev->name);
	sgec_stop(ll);
	sgec_init_ring(dev);
	load_csrs(lp);
	init_restart_sgec(lp);
	goto out;
      }
    */
      /* Buffer errors and underflows turn off the
       * transmitter, restart the adapter.
       */
    /*      if (status & (LE_T3_BUF | LE_T3_UFL)) {
	lp->stats.tx_fifo_errors++;
	printk("%s: Tx: ERR_BUF|ERR_UFL, restarting\n",	dev->name);
	sgec_stop(ll);
	sgec_init_ring(dev);
	load_csrs(lp);
	init_restart_sgec(lp);
	goto out;
      }
    } else
    */
 if ((td->word1 & SG_TD1_POK) == SG_TD1_POK) {
      /*
       * So we don't count the packet more than once.
       */
      td->word1 &= ~(SG_TD1_POK);

      /*      * One collision before packet was sent.
      if (td->word1 & SG_T1_EONE)
	lp->stats.collisions++;

      * More than one collision, be optimistic.
      if (td->tmd1_bits & LE_T1_EMORE)
	lp->stats.collisions += 2;
      */
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

static irqreturn_t sgec_interrupt(const int irq, void *dev_id, struct pt_regs *regs)
{
  struct net_device *dev = (struct net_device *) dev_id;
  struct sgec_private *lp = (struct sgec_private *) dev->priv;
  volatile struct sgec_regs *ll = lp->ll;
  unsigned long csr5;

  csr5 = ll->sg_nicsr5;

  if ((csr5 & SG_NICSR5_IS) == 0) {
    /* Hmmm, not for us... */
    return IRQ_NONE;
  }
  writereg(&ll->sg_nicsr5, csr5);  /* reset interrupt */

  /*  if ((csr0 & LE_C0_ERR)) {
    * Clear the error condition
    writecsr0(ll, LE_C0_BABL | LE_C0_ERR | LE_C0_MISS |
	      LE_C0_CERR | LE_C0_MERR);
  }*/
  if (csr5 & SG_NICSR5_RI) sgec_rx(dev);
  if (csr5 & SG_NICSR5_TI) sgec_tx(dev);
/*
	if (csr0 & LE_C0_BABL)
		lp->stats.tx_errors++;

	if (csr0 & LE_C0_MISS)
		lp->stats.rx_errors++;

	if (csr0 & LE_C0_MERR) {
		printk("%s: Memory error, status %04x", dev->name, csr0);

		sgec_stop(ll);

		sgec_init_ring(dev);
		load_csrs(lp);
		init_restart_sgec(lp);
		netif_wake_queue(dev);
	}
*/

  return IRQ_HANDLED;
}

extern struct net_device *last_dev;

static int sgec_open(struct net_device *dev)
{
        struct sgec_private *lp = (struct sgec_private *) dev->priv;
        volatile struct sgec_init_block *ib = lp->init_block;
	volatile struct sgec_regs *ll = lp->ll;

	last_dev = dev;

	/* Associate IRQ with sgec_interrupt */
	if (0){
	if (vsbus_request_irq (lp->vsbus_int, &sgec_interrupt, 0, lp->name, dev)) {
		printk("SGEC: Can't get irq %d\n", dev->irq);
		return -EAGAIN;
	}
	} else {
		printk (KERN_ERR "Ignoring interrupt for now...\n");
	}

	sgec_stop(ll);

	/* Clear the multicast filter */
	ib->mode=0;
	ib->filter[0] = 0;
	ib->filter[1] = 0;
	ib->filter[2] = 0;
	ib->filter[3] = 0;

	sgec_init_ring(dev);
	load_csrs(lp);

	netif_start_queue(dev);

	return init_restart_sgec(lp);
}

static int sgec_close(struct net_device *dev)
{
	struct sgec_private *lp = (struct sgec_private *) dev->priv;
	volatile struct sgec_regs *ll = lp->ll;

	netif_stop_queue(dev);
	del_timer_sync(&lp->multicast_timer);

	sgec_stop(ll);

	free_irq(dev->irq, (void *) dev);
	/*
	MOD_DEC_USE_COUNT;
	*/
	return 0;
}

static inline int sgec_reset(struct net_device *dev)
{
	struct sgec_private *lp = (struct sgec_private *) dev->priv;
	volatile struct sgec_regs *ll = lp->ll;
	int status;

	sgec_stop(ll);

	sgec_init_ring(dev);
	load_csrs(lp);
	dev->trans_start = jiffies;
	status = init_restart_sgec(lp);
#ifdef VAX_SGEC_DEBUG
	printk("SGEC restart=%d\n", status);
#endif
	return status;
}

static void sgec_tx_timeout(struct net_device *dev)
{
	struct sgec_private *lp = (struct sgec_private *) dev->priv;
	volatile struct sgec_regs *ll = lp->ll;

	printk(KERN_ERR "%s: transmit timed out, status %04x, reset\n",
			       dev->name, ll->sg_nicsr6);
	sgec_reset(dev);
	netif_wake_queue(dev);
}

static int sgec_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sgec_private *lp = (struct sgec_private *) dev->priv;
	volatile struct sgec_regs *ll = lp->ll;
	volatile struct sgec_init_block *ib = lp->init_block;
	int entry, skblen, len;

	skblen = skb->len;

	len = (skblen <= ETH_ZLEN) ? ETH_ZLEN : skblen;

	spin_lock_irq(&lp->lock);

	lp->stats.tx_bytes += len;

	entry = lp->tx_new & TX_RING_MOD_MASK;
	ib->btx_ring[entry].word1 = len;
	//	ib->btx_ring[entry].misc = 0;

	cp_to_buf((char *) lp->sgec_mem->tx_buf[entry], skb->data, skblen);

	/* Clear the slack of the packet, do I need this? */
	/* For a firewall its a good idea - AC */
/*
	if (len != skblen)
		memset ((char *) &ib->tx_buf [entry][skblen], 0, (len - skblen) << 1);
 */
	/* Now, give the packet to the card */
	ib->btx_ring[entry].word1 = SG_FR_OWN; /* (LE_T1_POK | LE_T1_OWN);*/
	lp->tx_new = (lp->tx_new + 1) & TX_RING_MOD_MASK;

	if (TX_BUFFS_AVAIL <= 0)
		netif_stop_queue(dev);

	/* Kick the SGEC: transmit now */
	writereg(ll->sg_nicsr5, SG_NICSR5_TI);

	spin_unlock_irq(&lp->lock);

	dev->trans_start = jiffies;
	dev_kfree_skb(skb);

	return 0;
}

static struct net_device_stats *sgec_get_stats(struct net_device *dev)
{
	struct sgec_private *lp = (struct sgec_private *) dev->priv;

	return &lp->stats;
}

static void sgec_load_multicast(struct net_device *dev)
{
	struct sgec_private *lp = (struct sgec_private *) dev->priv;
	volatile struct sgec_init_block *ib = lp->init_block;
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

static void sgec_set_multicast(struct net_device *dev)
{
	struct sgec_private *lp = (struct sgec_private *) dev->priv;
	volatile struct sgec_init_block *ib = lp->init_block;
	volatile struct sgec_regs *ll = lp->ll;

	if (!netif_running(dev))
		return;

	if (lp->tx_old != lp->tx_new) {
		mod_timer(&lp->multicast_timer, jiffies + 4);
		netif_wake_queue(dev);
		return;
	}

	netif_stop_queue(dev);

	sgec_stop(ll);

	sgec_init_ring(dev);

	if (dev->flags & IFF_PROMISC) {
		ib->mode |= SG_NICSR6_AF_PROM;
	} else {
		ib->mode &= ~SG_NICSR6_AF_PROM;
		sgec_load_multicast(dev);
	}
	load_csrs(lp);
	init_restart_sgec(lp);
	netif_wake_queue(dev);
}

static void sgec_set_multicast_retry(unsigned long _opaque)
{
	struct net_device *dev = (struct net_device *) _opaque;

	sgec_set_multicast(dev);
}

static int __init vax_sgec_init(struct net_device *dev,
		struct vsbus_device *vsbus_dev)
{
	static unsigned version_printed = 0;
	struct sgec_private *lp;
	volatile struct sgec_regs *ll;
	int i, ret;
	volatile unsigned long __iomem *esar;

	/* Could these base addresses be different on other CPUs? */
	unsigned long sgec_phys_addr = vsbus_dev->phys_base;
	unsigned long esar_phys_addr = NISA_ROM;
	printk (KERN_INFO "esar_phys_addr = 0x%08x\n", esar_phys_addr);

	if (version_printed++ == 0)
		printk(version);

	lp = (struct sgec_private *) dev->priv;

	spin_lock_init(&lp->lock);

	/* Need a block of 64KB */
        /* At present, until we figure out the address extension
	 * parity control bit, ask for memory in the DMA zone */
	dev->mem_start = __get_free_pages(GFP_DMA, 4);
	if (!dev->mem_start) {
		/* Shouldn't we free dev->priv here if dev was non-NULL on entry? */
		return -ENOMEM;
	}

	dev->mem_end = dev->mem_start + 65536;

	dev->base_addr = (unsigned long) ioremap (sgec_phys_addr, 0x8);
	dev->irq = vsbus_irqindex_to_irq (vsbus_dev->vsbus_irq);

	lp->sgec_mem = (volatile struct sgec_shared_mem *)(dev->mem_start);
	lp->init_block = &(lp->sgec_mem->init_block);

	lp->vsbus_int = vsbus_dev->vsbus_irq;

	ll = (struct sgec_regs *) dev->base_addr;

	/* FIXME: deal with failure here */
	esar = ioremap (esar_phys_addr, 0x80);

	/* 3rd byte contains address part in 3100/85 -RB */
	/* Note that 660 board types use a different position */
	/* Copy the ethernet address to the device structure, later to the
	 * sgec initialization block so the card gets it every time it's
	 * (re)initialized.
	 */
	printk("Ethernet address in ROM: ");
	for (i = 0; i < 6; i++) {
#if 0 /* Not yet */
		if (is_ka670 ())
			dev->dev_addr[i] = (esar[i] & 0xff00) >> 8;
		else
#endif
			dev->dev_addr[i] = esar[i] & 0xff;
		printk("%2.2x%c", dev->dev_addr[i], i == 5 ? '\n' : ':');
	}

	/* Don't need this any more */
	iounmap (esar);

	printk (KERN_INFO "Using SGEC interrupt vector %d, vsbus irq %d\n",
			dev->irq, lp->vsbus_int);

        dev->open = &sgec_open;
	dev->stop = &sgec_close;
	dev->hard_start_xmit = &sgec_start_xmit;
	dev->tx_timeout = &sgec_tx_timeout;
	dev->watchdog_timeo = 5*HZ;
	dev->get_stats = &sgec_get_stats;
	dev->set_multicast_list = &sgec_set_multicast;
	dev->dma = 0;

	/* lp->ll is the location of the registers for card */
	lp->ll = ll;

	lp->name = sgecstr;

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
	lp->multicast_timer.function = &sgec_set_multicast_retry;

	SET_NETDEV_DEV(dev, &vsbus_dev->dev);

	return 0;

err_out:
	unregister_netdev(dev);
	kfree(dev);
	return ret;
}


/* Find all the SGEC cards on the system and initialize them */
static int __init vax_sgec_probe (struct vsbus_device *vsbus_dev)
{
	struct net_device *netdev;
	int retval;

	printk("vax_sgec_probe: name = %s, base = 0x%08x, irqindex = %d\n",
			vsbus_dev->dev.bus_id, vsbus_dev->phys_base, vsbus_dev->vsbus_irq);

	netdev = alloc_etherdev (sizeof (struct sgec_private));
	if (!netdev)
		return -ENOMEM;

	retval = vax_sgec_init (netdev, vsbus_dev);
	if (retval == 0) {
		retval = register_netdev (netdev);
		if (retval)
			free_netdev (netdev);
	}

	return 0;
}

static struct vsbus_driver vax_sgec_driver = {
	.probe	= vax_sgec_probe,
	.drv	= {
		.name = "sgec",
	},
};

int __init sgec_init_module (void)
{
	return vsbus_register_driver (&vax_sgec_driver);
}

void __exit sgec_exit_module (void)
{
	printk (KERN_ERR "vax_sgec_exit: What to do???\n");
}

module_init (sgec_init_module);
module_exit (sgec_exit_module);


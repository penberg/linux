
/* Register offsets */

#define DELQA_ADDR1     0
#define DELQA_ADDR2     2
#define DELQA_RCLL      4  /* loword of first RX descriptor addr */
#define DELQA_RCLH      6  /* hiword of first RX descriptor addr */
#define DELQA_XMTL      8  /* loword of first TX descriptor addr */
#define DELQA_XMTH     10  /* hiword of first TX descriptor addr */
#define DELQA_VECTOR   12  /* Q-bus interrupt vector */
#define DELQA_CSR      14  /* control & status */


/* Bits in CSR */

#define DELQA_CSR_RCV_ENABLE   0x0001   /* Receiver enable               */
#define DELQA_CSR_RESET        0x0002   /* Software reset                */
#define DELQA_CSR_NEX_MEM_INT  0x0004   /* Non-existent memory interrupt */
#define DELQA_CSR_LOAD_ROM     0x0008   /* Load boot/diag from rom       */
#define DELQA_CSR_XL_INVALID   0x0010   /* Transmit list invalid         */
#define DELQA_CSR_RL_INVALID   0x0020   /* Receive list invalid          */
#define DELQA_CSR_INT_ENABLE   0x0040   /* Interrupt enable              */
#define DELQA_CSR_XMIT_INT     0x0080   /* Transmit interrupt            */
#define DELQA_CSR_ILOOP        0x0100   /* Internal loopback             */
#define DELQA_CSR_ELOOP        0x0200   /* External loopback             */
#define DELQA_CSR_STIM_ENABLE  0x0400   /* Sanity timer enable           */
#define DELQA_CSR_POWERUP      0x1000   /* Transceiver power on          */
#define DELQA_CSR_CARRIER      0x2000   /* Carrier detect                */
#define DELQA_CSR_RCV_INT      0x8000   /* Receiver interrupt            */


/* Bits in ADDR_HI field in descriptors */

#define DELQA_ADDRHI_VALID     0x8000   /* ADDRHI/LO are valid */
#define DELQA_ADDRHI_CHAIN     0x4000   /* ADDRHI/LO points to next descriptor */
#define DELQA_ADDRHI_EOMSG     0x2000   /* Buffer contains last byte of frame */
#define DELQA_ADDRHI_SETUP     0x1000   /* Buffer contains a setup frame */
#define DELQA_ADDRHI_ODDEND    0x0080   /* last byte not on word boundary */
#define DELQA_ADDRHI_ODDBEGIN  0x0040   /* first byte not on word boundary */




/* Bits in buffer descriptor field STATUS1 for transmit */

#define DELQA_TXSTS1_LASTNOT     0x8000
#define DELQA_TXSTS1_ERRORUSED   0x4000
#define DELQA_TXSTS1_LOSS        0x1000
#define DELQA_TXSTS1_NOCARRIER   0x0800
#define DELQA_TXSTS1_STE16       0x0400
#define DELQA_TXSTS1_ABORT       0x0200
#define DELQA_TXSTS1_FAIL        0x0100
#define DELQA_TXSTS1_COUNT_MASK  0x00f0
#define DELQA_TXSTS1_COUNT_SHIFT 4

/* Special value that signifies that descriptor is not yet used
   by DELQA.  The descriptor FLAG and STATUS1 fields both get
   initialized to this value. */
#define DELQA_NOTYET 0x8000

/* Bits in buffer descriptor field STATUS1 for transmit */

#define DELQA_TXSTS2_TDR_MASK  0x3fff
#define DELQA_TXSTS2_TDR_SHIFT 0


/* Bits in buffer descriptor field STATUS1 for receive */

#define DELQA_RXSTS1_LASTNOT      0x8000
#define DELQA_RXSTS1_ERRORUSED    0x4000
#define DELQA_RXSTS1_ESETUP       0x2000
#define DELQA_RXSTS1_DISCARD      0x1000
#define DELQA_RXSTS1_RUNT         0x0800
#define DELQA_RXSTS1_LEN_HI_MASK  0x0700
#define DELQA_RXSTS1_LEN_HI_SHIFT 8
#define DELQA_RXSTS1_FRAME        0x0004
#define DELQA_RXSTS1_CRCERR       0x0002
#define DELQA_RXSTS1_OVF          0x0001


/* Bits in buffer descriptor field STATUS2 for receive */

#define DELQA_RXSTS2_LEN_LO1_MASK  0x00ff
#define DELQA_RXSTS2_LEN_LO1_SHIFT 0
#define DELQA_RXSTS2_LEN_LO2_MASK  0xff00
#define DELQA_RXSTS2_LEN_LO2_SHIFT 8



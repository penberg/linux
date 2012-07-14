#ifndef _VAX_KERNEL_INTERRUPT_H
#define _VAX_KERNEL_INTERRUPT_H

/*
 * This file contains private definitions for the interrupt and
 * exception handling code in interrupt.c. I don't expect that
 * anyone else should need these definitions. If so, then we'll
 * have to move them to include/asm-vax instead.
 */

#include <linux/interrupt.h>

/* This is the max number of exception and interrupt handlers we can
   handle.  You can increase this as far as NR_IRQS if you need to. */
#define NR_IRQVECTORS 64

/* Initially, we use one of these to catch each vector in the SCB.
   When an exception or interrupt handler is registered, a struct
   irqvector is used instead */

struct stray_handler {
	unsigned char inst_jsb;		/* JSB instruction, 0x16 */
	unsigned char inst_addr_mode;	/* Absolute addr mode, 0x9F */
	void *dest_addr;		/* Address of irqvec_stray */
	unsigned short flags;		/* Used for probe_irq() */
} __attribute__ ((__packed__));

/* Bits in stray_handler.flags */
#define STRAY_EXCEPTION_FIRED 1

/* The VAX architecture defines interrupt vectors 64 and higher to be
   adapter and device vectors and are implementation dependent.  Vectors
   in this region can be autoprobed.  */
#define FIRST_ADAPTER_VECTOR 64


/* The irqvector structure is the VAX-specific equivalent of the
   Linux irqaction structure.  In fact, it has so much in common
   that it contains an irqaction...

   It is also used to vector exceptions, for which the excep_handler
   field is used.

   See Documentation/vax/interrupts.txt for more info on how this
   all works. */

struct irqvector {
	unsigned char inst_jsb;          /* JSB instruction, 0x16 - MUST be long-aligned*/
	unsigned char inst_addr_mode;    /* Absolute addressing mode, 0x9F */
	void *dest_addr;                 /* Address of irqvec_handler in entry.S */
	unsigned long excep_info_size;   /* This MUST follow dest_addr, irqvec_handler
                                            depends on it. */
	unsigned short vec_num;          /* Offset into SCB (in longwords, not bytes) */
	struct irqaction action;         /* Linux's normal interrupt vector structure */
	void (*excep_handler)(struct pt_regs *, void *);
	unsigned char *orig_scb_vector;  /* Original stray handler from SCB, restored when
                                            vector is un-hooked */
} __attribute__ ((__packed__));


/* And declarations of some standard interrupt handlers */

extern void accvio_handler(struct pt_regs *regs, void *excep_info);
extern void page_fault_handler(struct pt_regs *regs, void *excep_info);
extern void reserved_operand_handler(struct pt_regs *regs, void *excep_info);
extern void reserved_instr_handler(struct pt_regs *regs, void *excep_info);
extern void corrected_read_handler(struct pt_regs *regs, void *excep_info);
extern void syscall_handler(struct pt_regs *regs, void *excep_info);
extern void resam_handler(struct pt_regs *regs, void *unused);
extern void arith_handler(struct pt_regs *regs, void *excep_info);
extern void bpt_handler(struct pt_regs *regs, void *excep_info);
extern void tpend_handler(struct pt_regs *regs, void *excep_info);

#endif /* _VAX_KERNEL_INTERRUPT_H */

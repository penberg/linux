/*
 * entry.S for the VAX architecture
 * Copyright Dec 1998 atp.
 * Copyright 2000, Kenn Humborg <kenn@linux.ie>
 * 2001 atp. Additions for Machine check handling.
 * Copyright 2004, Jan-Benedict Glaw <jbglaw@lug-owl.de>
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/mtpr.h>

/*
 * irqvec_handler is the generic handler for all interrupts and
 * exceptions for which a driver (or other code) has registered
 * a handler. Except machine checks. We are responsible for
 *
 *	o  saving all registers
 *
 *	o  determining the address of the irqvector that called
 *	   us (actually we get the address of a field in the middle
 *	   of the irqvector structure)
 *
 *	o  passing this and other useful into to do_irq_excep()
 *	   in vax/kernel/interrupt.c
 *
 *	o  cleaning up the stack and dismissing the interrupt or
 *	   exception
 *
 * See Documentation/vax/interrupts.txt for the gory details
 */
//.globl irqvec_handler
//irqvec_handler:
ENTRY(irqvec_handler)
	/*
	 * At this point stack looks like:
	 *
	 *	SP:	handler_PC (inside the irqvector)
	 *		(maybe) exception info
	 *		saved PC
	 *		saved PSL
	 *
	 * Here is where it starts to get a bit twisted (I _love_
	 * the VAX instruction set!).
	 *
	 * We need to duplicate the saved PC and PSL to form then
	 * end of the pt_regs struct.  However, there may be some
	 * exception info between SP and the saved PC/PSL.  The number
	 * of longwords of this exception info is available in the
	 * irqvector structure (at our "return address")
	 */

	pushl %r0			/* Push R0 to have a free working register */

	/*
	 * Get number of exception info longwords into R0.
	 * Remember that this value is stored immediately after the
	 * JSB instruction in the irqvector, so the "return address"
	 * points to it
	 */
	movl *4(%sp), %r0
	addl2 $3, %r0			/* R0 now contains number of longwords between
					 * top of stack and saved PSL */

	pushl (%sp)[%r0]		/* Push saved PSL again */
	pushl (%sp)[%r0]		/* Push saved PC again */
	extzv $22, $2, 4(%sp), -(%sp)	/* Extract PREVMODE field from saved
					 * PSL and save on stack (because we
					 * don't have any spare registers yet) */
	cmpzv $24, $2, 8(%sp), (%sp)	/* Is PREVMODE == CURMODE? */
	beql same_mode
	mfpr (%sp),(%sp)		/* HACK ALERT!  The processor modes are 0..3
					 * for kernel..user mode.  The stack pointer
					 * internal processor registers are also 0..3
					 * for kernel..user mode.  So, using the
					 * PREVMODE value we just saved on the stack,
					 * we overwrite it with the correct stack
					 * pointer register.  Net result:  the saved_sp
					 * here is the correct stack pointer for the
					 * processor mode before the exception occurred */
	brb sp_saved
same_mode:
	moval 16(%sp)[%r0],(%sp)	/* Exception is not changing modes.  Therefore
					 * we calculate how far up the stack the SP
					 * was pointing when the exception occurred. */
sp_saved:
	pushr $0x3ffe			/* Push FP to R1 */
	pushl 64(%sp)			/* Duplicate saved R0 */

	/*
	 * The stack now looks like:
	 *
	 *	SP:	saved R0
	 *	SP+4	saved R1
	 *		...
	 *	SP+56	saved SP for previous mode
	 *	SP+60	saved PC
	 *	SP+64	saved PSL
	 *	SP+68	saved R0
	 *	SP+72	handler_PC (inside the irqvector)
	 *	SP+76	(maybe) exception info
	 *		saved PC
	 *		saved PSL
	 *
	 * Now build the argument list for do_irq_excep().  We need
	 * to pass the saved PC within the irqvector, the address of the
	 * pt_regs and the address of the exception info.  Be careful
	 * when you modify this code, we're counting stack locations
	 * to get the right offsets here...
	 */
	pushal 76(%sp)			/* address of exception info */
	pushl 76(%sp)			/* handler_PC */
	pushal 8(%sp)			/* start of pt_regs */

	calls $3, do_irq_excep

.globl ret_from_syscall
ret_from_syscall:
	/*
	 * Now we need to restore all registers, clear the stack down to
	 * the original saved PC/PSL and dismiss the interrupt.  The
	 * stack currently looks like the same as above.
	 *
	 * Copy the duplicate saved R0 (part of the struct pt_regs) down to
	 * the other saved R0 that we'll be restoring from soon.  This allows
	 * exception handlers to modify R0 in the thread of execution that
	 * triggered the exception.  Note that the offset from SP is 64, not
	 * 68 as you'd expect from the stack layout shown above.  This is
	 * because the SP is incremented by 4 while evaluating the first
	 * operand.
	 */
	movl (%sp)+, 64(%sp)
	popr $0x3ffe			/* Restore registers R1 up to FP */

        /*
	 * The stack now looks like:
	 *
	 *	SP:	saved SP for previous mode
	 *	+4	saved PC (maybe modified by exception handler)
	 *	+8	saved PSL (end of struct pt_regs)
	 *	+12	saved R0 (maybe modified by exception handler)
	 *	+16	handler_PC (inside the irqvector)
	 *	+20	(maybe) exception info
	 *	...
	 *		saved PC (original saved by CPU)
	 *		saved PSL
	 *
	 * Copy duplicate saved PC (part of the struct pt_regs) down over
	 * the original saved PC saved by the CPU.  This allows the exception
	 * handlers to change the PC of the code that triggered the exception.
	 * This is tricky because of the exception info that may be present
	 * on the stack.  First get the size of the exception info into R0
	 */
	movl *16(%sp), %r0

	/*
	 * Now move the saved PC down over the original (the 20-byte offset
	 * takes care of skipping over the top 5 items on the stack, and then
	 * the R0 index skips over the exception info.
	 */
	movl 4(%sp), 20(%sp)[%r0]

	/*
	 * D.A. May 2001 - we need to copy the PSL down, to
	 * get to usermode originally as we make up a new PSL
	 * in start_thread and we need the CPU to believe it
	 */
	movl 8(%sp), 24(%sp)[%r0]

        /*
	 * The stack now looks like:
	 *
	 *	SP:	saved SP for previous mode
	 *	+4	saved PC (maybe modified by exception handler)
	 *	+8	saved PSL (end of struct pt_regs)
	 *	+12	saved R0 (maybe modified by exception handler)
	 *	+16	handler_PC (inside the irqvector)
	 *	+20	(maybe) exception info
	 *	...
	 *		saved PC (maybe modified by exception handler)
	 *		saved PSL
	 *
	 * Now move the saved R0 (r0+1) longwords down the stack,
	 * leaving it just before the saved PC, overwriting either the
	 * saved handler_PC or the end of the exception info ...
	 */
	movl 12(%sp), 16(%sp)[%r0]
	/* ... and clear stack down to this point */
	moval 16(%sp)[%r0], %sp

	/*
	 * Stack now looks like
	 *
	 *	SP:	saved R0
	 *	+4	saved PC
	 *	+8	saved PSL
	 */

	/* Restore R0 and dismiss exception */
	movl (%sp)+, %r0
	mtpr $31, $PR_IPL
	rei


ENTRY(ret_from_fork)
	/*
	 * A newly-created thread starts here when it is first
	 * scheduled.  R0 will contain the previous task (the one
	 * that we just scheduled away from on this CPU).
	 */
	pushl %r0
	calls $1, schedule_tail
	brb ret_from_syscall


/*
 * irqvec_stray is the generic handler for all exceptions and interrupts
 * for which there is no registered handler.  We just save all registers,
 * and call unhandled_exception(), passing it the return address saved
 * by the JSB instruction that got us here.  This JSB instruction should
 * be in the struct stray_handler[] array in interrupt.c
 */
ENTRY(irqvec_stray)
	/*
	 * At this point stack looks like:
	 *
	 *	SP:	handler_PC (inside the stray_handler struct)
	 *		(maybe) exception info
	 *		saved PC
	 *		saved PSL
	 */
	pushr $0x3fff			/* Save FP to R0 */
	pushl 56(%sp)			/* copy return address (handler_PC) */
	calls $1, unhandled_exception

	/*
	 * If unhandled_exception() returns, then we must be
	 * autoprobing interrupt vectors, and this exception
	 * must have been an interrupt.  Therefore, there is
	 * no exception info on the stack, so it's safe to
	 * continue
	 */
	popr $0x3fff			/* restore R0 to FP */
	moval 4(%sp), %sp		/* Remove handler_PC */
	rei


/*
 * mcheck_handler is the handler for machine check exceptions.
 * This is here, because its easier to special case it, and deal with
 * the machine dependent number of longwords than warp the generic
 * registration methods to deal with it.
 */
.align 2				/* ensure longword alignment */
.globl machine_check_handler
machine_check_handler:
	/*
	 * Note this doesn't use the usual exception registration, as we don't
	 * know in advance how many longwords of exception info have been pushed.
	 *
	 * The interrupt stack at this point looks like this:
	 *
	 *	SP:	handler_PC (inside the irqvector)
	 *		longword count of exception info
	 *		exception longwords
	 *		:
	 *		PC
	 *		PSL
	 */
	addl2 $4, %sp			/* Discard the pointer into the irqvector */
	tstl probe_resume_addr		/* Are we executing an iospace_probeX() call? */
	bneq probing			/* Branch if yes */

	pushr $0x3f			/* push all registers in case we can restart */
	pushab 24(%sp)			/* address of stack slot which holds byte count */
	calls $1, machine_check		/* in reboot.c */
	popr $0x3f			/* spring them off */
	addl2 (%sp)+,%sp		/* get rid of the machine check frame */
	rei				/* dismiss */
probing:
	addl2 (%sp)+,%sp		/* get rid of the machine check frame */
	mtpr $0x0f, $PR_MCESR		/* clear the machine check error summary register,
					 * (taken from NetBSD) */
	movl probe_resume_addr, (%sp)
	rei				/* Return to the handler address */


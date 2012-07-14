/*
 * This file handles the interrupts and exceptions.
 *
 * It also contains the interrupt stack. Eventually, there will
 * need to be a separate interrupt stack per-cpu, within the
 * per-cpu data structures.
 *
 * FIXME: We should use the new interrupt architecture. It looks like
 *        a closer match to the VAX SCB.
*/

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>

#include <asm/pgalloc.h>
#include <asm/scb.h>
#include <asm/hardirq.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#include "interrupt.h"  /* Local, private definitions */

int do_signal(sigset_t *oldset, struct pt_regs *regs); /* signal.c */

unsigned char __attribute__((__aligned__(PAGE_SIZE))) interrupt_stack[NR_CPUS][INT_STACK_SIZE];

union scb_and_device_vectors __attribute__((__aligned__(PAGE_SIZE))) scb;

/*
 * Statically-defined pool of irqvector structures.  This will go once
 * we have a working kmalloc()/kfree().
 *
 * Actually, it's not that simple...  trap_init() is called before the
 * slab caches are initialized so we can't call kmalloc() this early
 * in initialization.  What we could do is statically allocate a small
 * number of irqvectors here (enough for trap_init() and friends) and
 * then kmalloc() vectors on demand later.
 *
 * An entry in the list is free if the dest_addr field is zero, and
 * is in use if non-zero.
 */
struct irqvector irqvectors[NR_IRQVECTORS];

/* Default handlers for each SCB vector */
static struct stray_handler stray_handlers[NR_IRQS];

/* Non-zero when autoprobing interrupt vectors */
static int autoprobing;


void guard_int_stack(void)
{
	void *stack_base;
	unsigned long base_pfn;
	pte_t *base_pte_addr;

        /*
	 * Do we need more than a page for the int stack?
	 * Yes, if we want a guard page.
	 */
	if (INT_STACK_SIZE <= PAGE_SIZE) {
		printk("Interrupt stack too small, must be > PAGE_SIZE\n");
		machine_halt();
	}

	stack_base = interrupt_stack + smp_processor_id();
	base_pfn = MAP_NR(stack_base);

	base_pte_addr = GET_SPTE_VIRT(stack_base);

	/*
	 * Set first page of interrupt stack area to kernel read, thus
	 * trapping any writes to this page.  This will catch attempts
	 * to overflow the interrupt stack before they can do any damage.
	 */
	set_pte(base_pte_addr, pfn_pte(base_pfn, __pgprot(_PAGE_KR|_PAGE_VALID)));

	__flush_tlb_one(stack_base);
}

static void setup_scb(void)
{
	int i;
	extern void irqvec_stray(void);

	for (i = 0; i < NR_IRQS; i++) {
		stray_handlers[i].inst_jsb = 0x16; /* JSB opcode */
		stray_handlers[i].inst_addr_mode = 0x9F; /* absolute */
		stray_handlers[i].dest_addr = irqvec_stray;
		stray_handlers[i].flags = 0;

		SCB_VECTOR(i) = &stray_handlers[i].inst_jsb;
	}

	flush_icache();
}

/* Register the machine check handler. */
void register_mcheck_handler(void)
{
        extern void machine_check_handler(struct pt_regs *regs, void *unused);
        struct irqvector *vector;
        unsigned char *inside_vec;

	/* First register things properly so that the irq functions don't get upset */
	if (register_excep_handler(SCB_MCHECK, "Machine Check (SCB_MCHECK: machine_check_handler)", machine_check_handler, 2, 1)) {
		printk("Panic: unable to register machine check handler\n");
		machine_halt();
	}

        /*
	 * Install the specific machine check handler in entry.S
	 * We override the value set up above, in register_excep_handler, as
	 * its easier than special casing all the exception info sizing.
	 */
        inside_vec = (unsigned char *) ((unsigned long)(SCB_VECTOR(SCB_MCHECK)) & ~0x3);
	vector = (struct irqvector *)(inside_vec -
					offsetof(struct irqvector, inst_jsb));
	vector->dest_addr = machine_check_handler;
}

void trap_init(void)
{
	struct exception_entry {
		unsigned int exception_number;
		unsigned char *exception_name;
		void (*exception_handler)(struct pt_regs *, void *);
		unsigned int exception_info_size;
		unsigned int use_interrupt_stack;
	} exception[] = {
		{ SCB_BPT, "Breakpoint fault (SCB_BPT: bpt_handler)", bpt_handler, 0, 0, },
		{ SCB_XFC, "Reserved instruction (SCB_XFC: reserved_instr_handler)", reserved_instr_handler, 0, 0, },
		{ SCB_CHMK, "CHMK trap (SCB_CHMK: syscall_handler)", syscall_handler, 1, 0, },
		{ SCB_ARITH, "Arithmetic fault (SCB_ARITH: arith_handler)", arith_handler, 1, 0, },
		{ SCB_RESAM, "Reserved addressing mode (SCB_RESAM: resam_handler)", resam_handler, 0, 0, },
		{ SCB_RESOP, "Reserved operand (SCB_RESOP: reserved_operand_handler)", reserved_operand_handler, 0, 0, },
		{ SCB_TPEND, "Trace Pending (SCB_TPEND: tpend_handler)", tpend_handler, 0, 0, },
		{ SCB_ACCVIO, "Access violation (SCB_ACCVIO: page_fault_handler)", page_fault_handler, 2, 0, },
		/* Perhaps this should be done in CPU-specific code? */
		{ SCB_MEMCORR, "Memory corrected read (SCB_MEMCORR: corrected_read_handler)", corrected_read_handler, 0, 0, },
		{ SCB_RESINSTR, "Reserved instruction (SCB_RESINSTR: reserved_instr_handler)", reserved_instr_handler, 0, 0, },
		{ SCB_TRANS_INVAL, "Translation not valid (SCB_TRANS_INVAL: page_fault_handler)", page_fault_handler, 2, 0, },
	};
	int i;

        /*
	 * Initialize the SCB with the stray interrupt/exception
	 * handlers. Some of these will be overridden later
	 * as device drivers hook up to their interrupts.
	 */
	setup_scb();

	/*
	 * And tell the hardware to use this SCB
	 */
	__mtpr(__pa(&scb), PR_SCBB);

	/*
	 * Register the machine check handler. This is a special case due to
	 * the machine specific exception info which is not fixed sized.
	 */
	register_mcheck_handler();

	/*
	 * Now register all exception handlers
	 */
	for (i = 0; i < ARRAY_SIZE (exception); i++) {
		if (register_excep_handler(exception[i].exception_number,
					exception[i].exception_name,
					exception[i].exception_handler,
					exception[i].exception_info_size,
					exception[i].use_interrupt_stack)) {
			printk("Panic: unable to register \"%s\" handler\n",
					exception[i].exception_name);
			machine_halt();
		}
	}
}

void init_IRQ(void)
{
	/* Nothing to do...  Already done by trap_init */
}

/*
 * This is the handler for reserved operand faults and aborts.
 * Eventually this will have to check if the fault was from user
 * mode or kernel mode and either throw a SIGILL or panic.
 */
void reserved_operand_handler(struct pt_regs *regs, void *unused)
{
	printk("\nReserved operand fault at PC=%08lx\n", regs->pc);


	printk("\nStack dump\n");
	hex_dump((void *)(regs->sp), 256);

	show_regs(regs);
	show_cpu_regs();

	if (user_mode(regs)) {
		force_sig(SIGILL,current);
		return;
	}
	machine_halt();
}

/*
 * This is the handler for reserved instruction exceptions.
 * Eventually this will have to check if the fault was from user
 * mode or kernel mode and either throw a SIGILL or panic.
 */
void reserved_instr_handler(struct pt_regs *regs, void *unused)
{
	unsigned short instr = *(unsigned short *)(regs->pc);

	if ((instr == 0xfeff) || (instr == 0xfdff)) {
		printk("\nKernel bugcheck at PC=%08lx\n", regs->pc);
	} else {
		printk("\nReserved instruction at PC=%08lx\n", regs->pc);
	}

	printk("\nStack dump\n");
	hex_dump((void *)(regs->sp), 256);
	dump_stack();
	show_regs(regs);
	show_cpu_regs();

	if (user_mode(regs)) {
		force_sig(SIGILL,current);
		return;
	}

	machine_halt();
}

/* This is the handler for break points */
void bpt_handler(struct pt_regs *regs, void *unused)
{
	siginfo_t info;
#if 0
	printk("\nbp sending SIGTRAP\n");

	printk("\nBreakpoint at PC=%08lx at %08lX\n", regs->pc, &regs->pc);

	printk("\nStack dump\n");
	hex_dump((void *)(regs->sp), 256);
	show_regs(regs);
	show_cpu_regs();
#endif
	if (user_mode(regs)) {
		info.si_signo = SIGTRAP;
		info.si_errno = 0;
		info.si_code = TRAP_BRKPT;
		info.si_addr = (void *) (regs->pc);
		force_sig_info(SIGTRAP, &info,current);
		return;
	}

	machine_halt();
	force_sig(SIGTRAP, current);
}

/* This is the handler for break points */
void tpend_handler(struct pt_regs *regs, void *unused)
{
	siginfo_t info;

	regs->psl.t = 0;

#if 0
	printk("\ntpend sending SIGTRAP\n");
	printk("\nTrace Pending at PC=%08lx at %08lX\n", regs->pc, &regs->pc);
	printk("\nStack dump\n");
	hex_dump((void *)(regs->sp), 256);
	show_regs(regs);
	show_cpu_regs();
#endif

	if (user_mode(regs)) {
		info.si_signo = SIGTRAP;
		info.si_errno = 0;
		info.si_code = TRAP_BRKPT;
		info.si_addr = (void *) (regs->pc);
		force_sig_info(SIGTRAP, &info, current);
		return;
	}

	machine_halt();
	force_sig(SIGTRAP, current);
}

/*
 * This is the handler for reserved addressing mode exceptions.
 * Eventually this will have to check if the fault was from user
 * mode or kernel mode and either throw a SIGILL or panic.
 */
void resam_handler(struct pt_regs *regs, void *unused)
{
	unsigned short instr = * (unsigned short *) (regs->pc);

	if ((instr == 0xfeff) || (instr == 0xfdff)) {
		printk("\nKernel bugcheck at PC=%08lx\n", regs->pc);
	} else {
		printk("\nReserved addressing mode fault at PC=%08lx\n", regs->pc);
	}

	printk("\nStack dump\n");
	hex_dump((void *)(regs->sp), 256);
	dump_stack();
	show_regs(regs);
	show_cpu_regs();

        if (user_mode(regs)) {
		force_sig(SIGILL,current);
		return;
	}

	machine_halt();
}

/* This is the handler for corrected memory read errors */
void corrected_read_handler(struct pt_regs *regs, void *unused)
{
	printk("Corrected memory read error.  "
		"RAM failing or cache incorrectly initialized?\n");
}

/* This is the handler for arithmetic faults */
static char *arith_faults[] = {
	"none",
	"Integer Overflow",
	"Integer Division by Zero",
	"Floating Overflow Trap",
	"Floating or Decimal Division by Zero Trap",
	"Floating Underflow Trap",
	"Decimal Overflow",
	"Subscript Range",
	"Floating Overflow Fault",
	"Floating or Decimal Division by Zero Fault",
	"Floating Underflow Fault",
};

void arith_handler(struct pt_regs *regs, void *excep_info)
{
	int code = *(unsigned int *)(excep_info);

	printk("Arithmetic Fault at PC=%8lx, %s, (code=%x)\n", regs->pc,
			arith_faults[code], code);
	/* FIXME: need to code up the info for user handler */
	if (user_mode(regs)) {
		force_sig(SIGFPE, current);
		return;
	}
}

/*
 * This function gets called from irqvec_stray when we get an exception or
 * interrupt that doesn't have a handler in the SCB.  The argument is the
 * saved PC value from the JSB instruction in the stray_handler structure.
 * From this value, we can find the address of the struct stray_handler,
 * and thus the vector number.
 *
 * This will also be used to auto-probe interrupt vectors.  probe_irq_on()
 * will clear the STRAY_EXCEPTION_FIRED flag on each stray handler above 64
 * (adapter and device vectors).  Then probe_irq_off() will look for a
 * vector with this bit set.
 */
int unhandled_exception(unsigned char *retaddr)
{
	struct stray_handler *handler;
	unsigned int vec_num;

	handler = (struct stray_handler *) (retaddr
			- offsetof(struct stray_handler, flags));

	vec_num = handler - stray_handlers;

	if (autoprobing && vec_num >= FIRST_ADAPTER_VECTOR) {
		stray_handlers[vec_num].flags |= STRAY_EXCEPTION_FIRED;
		return 0;
	}

	printk("\nUnhandled interrupt or exception number 0x%04x (SCB offset 0x%04x)\n",
			vec_num, vec_num * 4);

	printk("\nStack dump:\n");
	vax_dump_stack(DUMP_STACK_CALLER);

	dump_cur_regs(DUMP_REGS_CALLER);
	show_cpu_regs();

	machine_halt();
}


/*
 * This is the equivalent of handle_IRQ_event() on x86.  There is no
 * need to walk the list of irqactions as in x86 because we don't have
 * shared interrupts on the VAX.
 */
static inline void dispatch_irq(struct pt_regs *regs, struct irqvector *vec)
{
	struct irqaction *action;
	int vec_num;

	action = &vec->action;
	vec_num = vec->vec_num;

	kstat_cpu(smp_processor_id()).irqs[vec_num]++;
	action->handler(vec_num, action->dev_id, regs);

	if (action->flags & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(vec_num);
}

/*
 * This is called once we know that an interrupt or exception is actually
 * an interrupt.
 */
static inline void do_irq(struct pt_regs *regs, struct irqvector *vec)
{
        int flags;

	/* Fake a single-priority-level interrupt system by raising IPL
	   to 31 for _any_ interrupt.  This is such a waste of the VAX's
	   hardware capabilities... */

	irq_enter();

	local_irq_save(flags);

	dispatch_irq(regs, vec);
	irq_exit();

	local_irq_restore(flags);

	if (local_softirq_pending())
		do_softirq();
}

static inline void do_exception(struct pt_regs *regs, struct irqvector *vec, void *excep_info)
{
	kstat_cpu(smp_processor_id()).irqs[vec->vec_num]++;
	vec->excep_handler(regs, excep_info);
}

/*
 * This is called from irqvec_handler in entry.S. At this point, inside_vec
 * points to the excep_info_size field of the relevant struct irqvector.
 * Locate the actual struct irqvector and dispatch the interrupt or
 * exception.
 *
 * "Understanding the Linux Kernel" by Bovet & Cesati from O'Reilly
 * contains the best explanation I've found for the various exit paths
 * from this function.
 */
void do_irq_excep(struct pt_regs *regs, void *inside_vec, void *excep_info)
{
	struct irqvector *vec;

	vec = (struct irqvector *) (inside_vec
			- offsetof(struct irqvector, excep_info_size));

	/*
	 * If the excep_handler field of the irqvector is NULL,
	 * then this is an interrupt vector.  Dispatch it via the
	 * irqaction struct.
	 */
	if (vec->excep_handler != NULL) {
//                printk("exception: vec=%p handler %p excep_info=%p(%d)\n",vec,vec->excep_handler,excep_info,*(int *)excep_info);
		do_exception(regs, vec, excep_info);
		if (vec == scb.scb.chmk) {
			goto ret_from_sys_call;
		} else {
			goto ret_from_exception;
		}
	} else {
		do_irq(regs, vec);
		goto ret_from_intr;
	}

ret_from_sys_call:
	if (local_softirq_pending()) {
		do_softirq();
		goto ret_from_intr;
	}
	goto ret_with_reschedule;

ret_from_exception:
	if (local_softirq_pending())
		do_softirq();

ret_from_intr:
	if (__psl.prevmode == 0) {
		/* returning to kernel mode */
		goto done;
	}

ret_with_reschedule:
//	printk("syscall: pid %d need_resched %d sigpending %d state %d\n",current->pid,current->need_resched,current->sigpending,current->state);
	if (need_resched()) {
		schedule();
		goto ret_from_sys_call;
	}

	/* Check for pending signals */
	if (test_tsk_thread_flag(current, TIF_SIGPENDING)) {
		/* FIXME: do we need to check the IPL here (i386 does a sti here) */
		/* FIXME: oldset? */
		do_signal(0, regs);
	}

//	printk("syscall: out of c code\n");
done:
	return;
}

/*
 * These two functions, alloc_irqvector() and free_irqvector(), are temporary
 * until we have a working kmalloc. We have a statically-allocated array of
 * irqvector structures. An entry is free if the dest_addr field is NULL,
 * it is in use otherwise.
 */
static struct irqvector *alloc_irqvector(void)
{
	int i;
	int flags;
	struct irqvector *vec;

	local_irq_save(flags);

	for (i=0, vec=irqvectors; i<NR_IRQVECTORS; i++, vec++) {
		if (vec->dest_addr == NULL) {
			vec->dest_addr = (void *) 0xffffffff;
			local_irq_restore(flags);
			return vec;
		}
	}

	local_irq_restore(flags);
	return NULL;
}

static void free_irqvector(struct irqvector *vec)
{
	memset(vec, 0, sizeof(*vec));
}

static int scb_vec_free(unsigned int vec_num)
{
	unsigned char *stray_start;
	unsigned char *stray_end;

	stray_start = &stray_handlers[0].inst_jsb;
	stray_end = &stray_handlers[NR_IRQS].inst_jsb;

	if ((SCB_VECTOR(vec_num) >= stray_start) &&
		(SCB_VECTOR(vec_num) < stray_end)) {
		return 1;
	} else {
		return 0;
	}
}

static int hook_scb_vector(unsigned int vec_num, struct irqvector *vec,
	unsigned int use_interrupt_stack)
{
	unsigned char *new_vector;
	int flags;
	extern void irqvec_handler(void);

	local_irq_save(flags);

	if (!scb_vec_free(vec_num)) {
		local_irq_restore(flags);
		printk("hook_scb_vector: SCB vector %04x (%p) already in use\n",
			vec_num, SCB_VECTOR(vec_num));
		return -EBUSY;
	}

	vec->vec_num = vec_num;

	vec->inst_jsb = 0x16;		/* JSB */
	vec->inst_addr_mode = 0x9f;	/* absolute addressing mode */
	vec->dest_addr = irqvec_handler;

	vec->orig_scb_vector = SCB_VECTOR(vec_num);

	new_vector = &vec->inst_jsb;

	if (use_interrupt_stack) {
		/*
		 * LSB set in SCB vector tells CPU to service event
		 * on interrupt stack regardless of current stack.
		 */
		new_vector++;
	}

	SCB_VECTOR(vec_num) = new_vector;

	flush_icache();

	local_irq_restore(flags);
	return 0;
}

int request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
        int retval;
        struct irqvector *vector;

        if (irq >= NR_IRQS)
                return -EINVAL;
        if (!handler)
                return -EINVAL;

	vector = alloc_irqvector();
        if (!vector)
                return -ENOMEM;

        vector->action.handler = handler;
        vector->action.flags = irqflags;
        vector->action.mask = CPU_MASK_NONE;
        vector->action.name = devname;
        vector->action.next = NULL;
        vector->action.dev_id = dev_id;

	vector->excep_info_size = 0;
	vector->excep_handler = NULL;

        retval = hook_scb_vector(irq, vector, 1);

        if (retval)
		free_irqvector(vector);

        return retval;
}

static void unhook_scb_vector(unsigned int vec_num, void *dev_id)
{
	int flags;
	struct irqvector *vector;
	unsigned char *inside_vec;

	local_irq_save(flags);

	if (scb_vec_free(vec_num)) {
		local_irq_restore(flags);
		printk("unhook_scb_vector: SCB vector %04x already free\n", vec_num);
		return;
	}

	inside_vec = SCB_VECTOR(vec_num);

	/* We must mask off the bottom two bits.  They have meaning to
	   to the hardware, and are not part of the actual target address */

	inside_vec = (unsigned char *) ((unsigned long) (inside_vec) & ~0x3);

	vector = (struct irqvector *) (inside_vec
			- offsetof(struct irqvector, inst_jsb));

	if (dev_id != vector->action.dev_id) {
		local_irq_restore(flags);
		printk("unhook_scb_vector: dev_id mismatch (expected %p, currently %p)\n",
			dev_id, vector->action.dev_id);
		return;
	}

	SCB_VECTOR(vec_num) = vector->orig_scb_vector;

	local_irq_restore(flags);

	free_irqvector(vector);
}

void free_irq(unsigned int irq, void *dev_id)
{
        if (irq >= NR_IRQS)
                return;

	unhook_scb_vector(irq, dev_id);
}

unsigned long probe_irq_on(void)
{
	int i;
	int flags;

	local_irq_save(flags);

	for (i = FIRST_ADAPTER_VECTOR; i < NR_IRQS; i++)
		stray_handlers[i].flags &= ~STRAY_EXCEPTION_FIRED;

	autoprobing = 1;
	local_irq_restore(flags);

	return 1;
}

int probe_irq_off(unsigned long mask)
{
	int i;
	int vec_found;
	int nr_vecs;
	int flags;

	nr_vecs = 0;
	vec_found = 0;

	local_irq_save(flags);

	for (i = FIRST_ADAPTER_VECTOR; i < NR_IRQS; i++) {
		if (stray_handlers[i].flags & STRAY_EXCEPTION_FIRED) {
			vec_found = i;
			nr_vecs++;
		}
	}
	autoprobing=0;
	local_irq_restore(flags);

	if (nr_vecs > 1) {
		vec_found = -vec_found;
	}

	return vec_found;
}

int register_excep_handler(unsigned int vec_num, char *exception_name,
		void (*handler)(struct pt_regs *, void *),
		unsigned int exception_info_size, unsigned int use_interrupt_stack)
{
        int retval;
        struct irqvector *vector;

        if (vec_num >= NR_IRQS)
                return -EINVAL;
        if (!handler)
                return -EINVAL;

	vector = alloc_irqvector();

	if (!vector)
		return -ENOMEM;

	vector->excep_info_size = exception_info_size;
	vector->excep_handler = handler;
        vector->action.name = exception_name; /* Needed to stop get_irq_list dying */
	/* FIXME: This doesn't set dev_id or other members of the irqaction structure... */

        retval = hook_scb_vector(vec_num, vector, use_interrupt_stack);

        if (retval)
		free_irqvector(vector);

        return retval;
}

int show_interrupts(struct seq_file *p, void *v)
{
	int i = * (loff_t *) v;
	struct irqvector *vector;
	unsigned char *inside_vec;

	if (i < NR_IRQS && !scb_vec_free (i)) {

		inside_vec = SCB_VECTOR (i);
		/*
		 * We must mask off the bottom two bits. They have
		 * meaning to the hardware, and are not part of
		 * the actual target address
		 */
		inside_vec = (unsigned char *) ((unsigned long) (inside_vec) & ~0x3);
		vector = (struct irqvector *) (inside_vec
				- offsetof (struct irqvector, inst_jsb));
		if (vector->action.name == NULL)
			seq_printf (p, "%4d: %8d no interrupt vector name\n", vector->vec_num, 0);
		else
			seq_printf (p, "%4d: %8d %s\n", vector->vec_num, kstat_irqs(i), vector->action.name);
	}

	return 0;
}

/* Empty for now. See arch/i386/kernel/irq.c for what this should do. */
void init_irq_proc(void)
{
}


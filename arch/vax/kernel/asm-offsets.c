/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed
 * to extract and format the required data.
 */

#include <stddef.h>
#include <linux/sched.h>
#include <asm/mv.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/rpb.h>

#define DEFINE(sym, val) \
		asm volatile ("\n->" #sym " %0 " #val : : "i" (val))
#define BLANK()	asm volatile ("\n->" : : )

void foo (void)
{
	DEFINE (MV_PRE_VM_INIT,		offsetof (struct vax_mv, pre_vm_init));
	DEFINE (MV_POST_VM_INIT,	offsetof (struct vax_mv, post_vm_init));
	DEFINE (MV_PRE_VM_PUTCHAR,	offsetof (struct vax_mv, pre_vm_putchar));
	DEFINE (MV_PRE_VM_GETCHAR,	offsetof (struct vax_mv, pre_vm_getchar));
	DEFINE (MV_POST_VM_PUTCHAR,	offsetof (struct vax_mv, post_vm_putchar));
	DEFINE (MV_POST_VM_GETCHAR,	offsetof (struct vax_mv, post_vm_getchar));
	DEFINE (MV_CONSOLE_INIT,	offsetof (struct vax_mv, console_init));
	DEFINE (MV_REBOOT,		offsetof (struct vax_mv, reboot));
	DEFINE (MV_HALT,		offsetof (struct vax_mv, halt));
	DEFINE (MV_MCHECK,		offsetof (struct vax_mv, mcheck));
	DEFINE (MV_INIT_DEVICES,	offsetof (struct vax_mv, init_devices));
	DEFINE (MV_CPU_TYPE_STR,	offsetof (struct vax_mv, cpu_type_str));
	DEFINE (MV_CLOCK_INIT,		offsetof (struct vax_mv, clock_init));
	DEFINE (MV_CLOCK_BASE,		offsetof (struct vax_mv, clock_base));
	DEFINE (MV_SIDEX,		offsetof (struct vax_mv, sidex));
	BLANK ();
	DEFINE (ASM_SBR_OFFSET,		sizeof (struct pgd_descriptor) * 2 + offsetof (struct pgd_descriptor, br));
	DEFINE (ASM_SLR_OFFSET,		sizeof (struct pgd_descriptor) * 2 + offsetof (struct pgd_descriptor, lr));
	BLANK ();
	DEFINE (PAGE_OFFSET,		PAGE_OFFSET);
	BLANK ();
	DEFINE (RPB_SIZE,		sizeof (struct rpb_struct));
	DEFINE (RPB_PFNCNT_OFFSET,	offsetof (struct rpb_struct, l_pfncnt));
}


#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/ipr-index.h>
#include <asm/page.h>

	.extern start_kernel

	/*
	 * The main entry point to kernel images. We only do the bare minimum
	 * to be able to call into C code.
	 */
ENTRY(_stext)
	jmp	start

	/*
	 * The kernel command line:
	 */
.globl command_line
command_line:
        .fill   256, 1, 0

start:
	/* Save important registers: */
	movl	%r11, boot_r11

	/* Disable memory mapping: */
	mtpr	$0, $IPR_MAPEN

	/* Disable interrupts: */
	mtpr	$31, $IPR_IPL

	/* Setup system address space but don't enable memory mapping: */
	pushl	boot_r11
	calls	$1, vax_setup_system_space

	/* Relocate the kernel: */
	pushal	_end
	pushal	_stext
	calls	$2, vax_relocate_kernel

	/* Jump to the relocated kernel: */
	addl2	$relocated_kernel, %r0
	subl2	$_stext, %r0
	jmp	(%r0)

relocated_kernel:
	/*
	 * Enable memory mapping:
	 */
	addl2	$PAGE_OFFSET, %sp		/* Fix %sp and %fp to be virtual addresses */
	addl2	$PAGE_OFFSET, %fp
	movab	memory_mapping_enabled, %r0	/* Fix jump target to be a virtual address */
	addl2	$PAGE_OFFSET, %r0
	mtpr	$IPR_TBIA, $IPR_TBIA		/* Flush TLB */
	mtpr	$1, $IPR_MAPEN			/* Enable mapping */
	jmp	(%r0)

memory_mapping_enabled:
	/* Start the kernel: */
	calls	$0, vax_start_kernel

	/* Not reached. */
	halt

.globl boot_r11
boot_r11:
	.int 0x00000000

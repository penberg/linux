#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/ipr-index.h>

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
	/* Disable memory mapping: */
	mtpr	$0, $IPR_MAPEN

	/* Disable interrupts: */
	mtpr	$31, $IPR_IPL

	/* Relocate the kernel: */
	pushal	_end
	pushal	_stext
	calls	$2, vax_relocate_kernel

	/* Jump to the relocated kernel: */
	addl2	$relocated_kernel, %r0
	subl2	$_stext, %r0
	jmp	(%r0)

relocated_kernel:
	/* Start the kernel: */
	calls	$0, vax_start_kernel

	/* Not reached. */
	halt

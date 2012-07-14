# Copyright atp Nov 1998.
# Changes for aligning IOMAP/VMALLOC - Copyright airlied@linux.ie - June 2001
# start of boot. entry point
# this assumes vmb has does most of the hard work (ie uvax rom vmb)
# save useful registers. jump to c in boot.c
#
# TBD: Some of this stuff could do with being rewritten in C
#      Some of this stuff could be in .init sections and thrown away.
#

#include <linux/init.h>		/* For __INITDATA */
#include <asm/mtpr.h>		/* Processor register definitions */
#include <asm/asm_offsets.h>	/* machine vector definitions */
#include <asm/pgtable.h>	/*  PTE definitions  */
#include <asm/irq.h>		/* interrupt stack definitions */

#include "boot_sections.h"

__BOOT

.globl _stext
_stext:
.globl start
start:
	jmp codestart			# Word displacement.


.globl bootparam
bootparam:
	.fill 256,1,0			# The boot parameter block. Presently just the
					# command line.


codestart:
	# disable memory mapping
	mtpr $0, $PR_MAPEN
	mtpr $31, $PR_IPL

	# save r11, ap and scbb and location of command line
	movl %ap,  boot_ap
	movl %r11, boot_r11
	mfpr $PR_SCBB, boot_scb
	movab bootparam, %r5
	addl2 $PAGE_OFFSET,%r5		# we will only access this when MAPEN=1
	movl %r5,kernel_cmd_line

	# Put the sp somewhere safe, over our bootblock in fact
	moval	start, %r5
	subl2	$0x200, %r5
	movl	%r5,%sp

#	Debug code:
#	movzbl	$0x42, %r2
#	jsb	0x20040058

	pushal	start
	calls	$1, idcpu		# Identify this CPU and...
	movl	%r0, mv			# ...put the returned mv ptr into mv.

	# Now fix up the machine vector entries. (They currently contain
	# pointers to virtual addresses in S0 space. We need to change
	# the pointers to the functions we use before VM init to point
	# into the newly-loaded kernel image.)
	movl	mv, %r10
	moval	start, %r8

	subl2	$PAGE_OFFSET+KERNEL_START_PHYS, %r8
	addl2	%r8, MV_PRE_VM_PUTCHAR(%r10)
	addl2	%r8, MV_PRE_VM_GETCHAR(%r10)
	addl2	%r8, MV_CPU_TYPE_STR(%r10)

	calls	$0, boot_crlf
	calls	$0, boot_crlf

	# print the cpu type
	calls	$0, boot_print_cpu_id

	# print first line of debug diagnostics
	pushab  msg_loaded			# ascii string
	calls	$1, boot_printstr
	pushal	start				# where we were loaded
	calls	$1, boot_printint
	calls	$0, boot_crlf
	pushab	msg_registers			# ascii string
	calls	$1, boot_printstr
	calls	$0, boot_printspace
	movl	boot_r11, %r11
	pushl	%r11				# r11 (holds the rpb base address, usually 0x0)
	calls	$1, boot_printint
	calls	$0, boot_printspace
						# FIXME: magic offset -> asmoffsets.h
	pushl	48(%r11)			# saved r5 in RPB (argument to BOOT command)
	calls	$1, boot_printint
	calls	$0, boot_printspace
	pushl	%ap				# argument pointer (struct arglist)
	calls	$1, boot_printint
	calls	$0, boot_printspace
	pushl	%sp				# stack pointer
	calls	$1, boot_printint
	calls	$0, boot_crlf

	# Save off the current machine vector address in boot_mv, because it
	# lies in the .bss section and it will get clobbered real soon...
	#  - atp. in fact it gets clobbered real quick, if your kernel is
	#    larger than about 950k, as the relocation code clobbers it, along
	#    with every thing else poking its head above $KERNEL_START_PHYS,
	#    like the entire .bss section.
	movl	mv, boot_mv

	# copy the loaded image higher up in physical RAM

	movl	$__bss_start, %r6
	subl2	$start, %r6			# byte count to r6
	pushl	%r6
	pushab	start				# source address
	pushl	$KERNEL_START_PHYS		# dest address
	calls	$3, boot_memmove

	# Next we have to fill the .bss section with zeros. We do it now
	# instead of when we are preparing the loadable image because it
	# makes the image smaller.
	subl3	$__bss_start, $_end, %r6	# length of .bss to r6

	subl3	$start, $__bss_start, %r7	# offset of .bss to r7
	addl2	$KERNEL_START_PHYS, %r7		# phys address of .bss start now in r7

	pushl	%r6
	pushl	%r7
	calls	$2, boot_memzero
	addl2	%r6, %r7

	decl	%r7
	movl	%r7, %r9			# save phys addr of last byte of kernel
						# in R9. We will need this later.

	# Need to know the distance we have moved the kernel, so that we can
	# fix up the machine vector pointer after we jump
	moval	start, %r2
	subl3	%r2, $KERNEL_START_PHYS, %r2	# r2 = START_PHYS - load_address

	# Calculate the position after jump to reloc
	movl	$KERNEL_START_PHYS, %r8
	addl2	$reloc, %r8
	subl2	$start, %r8
	jmp	(%r8)
	halt

reloc:
	# Fix up the machine vector pointer (by restoring it from boot_mv
	# and adding in the distance that the kernel was re-located).
	addl3	%r2, boot_mv, mv
	movl	mv, %r10

	# ...as well as all pre-VM mv function pointers
	addl2	%r2, MV_PRE_VM_PUTCHAR(%r10)
	addl2	%r2, MV_PRE_VM_GETCHAR(%r10)
	addl2	%r2, MV_CPU_TYPE_STR(%r10)

	# Print 'relocated at phys addr xxxxx'
	pushab	msg_relocated
	calls	$1, boot_printstr
	pushal	reloc
	calls	$1, boot_printint
	calls	$0, boot_crlf
	calls	$0, boot_crlf

	# Save RPB before it gets obliterated
	movl	boot_r11, %r11
	movc3	$RPB_SIZE, (%r11), boot_rpb

	# Execute mv->pre_vm_init() if it is needed
	movl	mv, %r10
	tstl	MV_PRE_VM_INIT(%r10)
	beql	no_pre_vm_init
	subl2	$PAGE_OFFSET, MV_PRE_VM_INIT(%r10)
	calls	$0, *MV_PRE_VM_INIT(%r10)
no_pre_vm_init:

	# set up the system page table for all of physical memory.
	# for the i386, the first page only is setup. For us, as the
	# system memory map is contiguous anyway, we might as well setup
	# all of physical ram at once, which will be useful for later bits
	# of system start up. We will expand the page table later when we
	# set up the virtual memory map properly, but for now, we can use
	# this to access all of RAM via the kernel _page_offset mapping.
	#
	# PAGE_OFFSET here is 0x80000000 - the start of system space.
	#
	# swapper_pg_dir is actually a pgd_t. The spt is the third entry.
	# see include/asm-vax/mm/pagelet.h for details of the pgd_t structure.
	# If you change the SPT, change the offsets in asm/pgtable.h.
	#
	# First find a suitable start position for the SPT. This must be
	# longword aligned.
	#
	# Correction, for 3100/85 it needs to be page aligned.
	addl3	$0x200, %r9, %r5		# R9 holds kernel end
	bicl2	$0x1ff, %r5			# R5 is R9 rounded up to page aligned
	moval	swapper_pg_dir, %r0
	# This is (pgd_t)swapper_pg_dir[2].br
	movl	%r5, ASM_SBR_OFFSET(%r0)	# save address of base of system page table

	# Fill in the main part of the SPT (the entries that map physical
	# memory)
	movl	$0, %r6				# pfn number
	movl	RPB_PFNCNT_OFFSET(%r11), %r7	# pfncnt from rpb.
sysfill:
	bisl3	$_PAGE_VALID + _PAGE_UW, %r6, (%r5)+
						# set PFN, VALID bit and protection UW in PTE
	incl	%r6				# next PFN
	cmpl	%r6, %r7			# one page of PTE Table -> 128 Pages of PTES
	blssu	sysfill

	# We need to align the IOMAP/VMALLOC tables (well at least the VMALLOC
	# tables, but no harm.. we will waste the space here or later) on a Linux
	# PAGE boundary (4K)
	# we need to check is r7 4k-aligned if not we need to fill zero until it
	# is, so round up r7 to the next page, add 7 then and with 7,
	# check with r6 if the same we are aligned if not put zeros into the
	# PTE until we are aligned. - D.A. June 2001 (this stuff is bitchin..)
	addl2	$0x7, %r7
	bicl2	$7, %r7
	cmpl	%r6, %r7
	beql	nozerofl
zerofl:
	movl	$0x00000000, (%r5)+
	incl	%r6				# next PFN
	cmpl	%r6, %r7			# one page of PTE Table -> 128 Pages of PTES
	blssu	zerofl
nozerofl:
	# Zero out the spare part of the SPT (the entries that will be used
	# to map I/O space and provide virtual addrs for vmalloc() later)
	movl	%r5, iomap_base
	addl2	$SPT_HWPTES_IOMAP+0, %r7
sparef1:
	movl	$0x00000000, (%r5)+
	incl	%r6				# next PFN
	cmpl	%r6, %r7			# one page of PTE Table -> 128 Pages of PTES
	blssu	sparef1

	movl	%r5, vmallocmap_base
	addl2	$SPT_HWPTES_VMALLOC, %r7
sparefill2:
	movl	$0x00000000, (%r5)+
	incl	%r6				# next PFN
	cmpl	%r6, %r7			# one page of PTE Table -> 128 Pages of PTES
	blssu	sparefill2

	# System page table is setup. Save SPT length and zap processor registers
	moval	swapper_pg_dir, %r0
	movl	%r7, ASM_SLR_OFFSET(%r0)
	mtpr	ASM_SBR_OFFSET(%r0), $PR_SBR	# set SBR
	mtpr	%r7, $PR_SLR			# set SLR

	# PCBB
	# Set up the process control block. Some machines need a valid PCB for
	# MM to work properly.
	# We should use the PCB for the init task for this, but since this bit
	# should be done in C, rather than hardwiring offsets, I have put a fake
	# PCB in a throwaway .init section below.
	moval	fake_pcb, %r9
	movl	$PAGE_OFFSET, 88(%r9)		# p1br	FIXME: magic offset
	mtpr	%r9, $PR_PCBB

	# No need to TBIA - memory mapping not enabled
	# ready to turn on VM
	pushab	msg_starting_vm
	calls	$1, boot_printstr
	calls	$0, boot_crlf
	calls	$0, VAX_start_mm			# do that ole black magic
	# made it

	moval	swapper_pg_dir, %r0
	addl2	$PAGE_OFFSET, ASM_SBR_OFFSET(%r0)	# fix up our reference to the system page tbl.
	addl2	$PAGE_OFFSET, iomap_base		# ... and the IOMAP PTEs
	addl2	$PAGE_OFFSET, vmallocmap_base		# ... and the IOMAP PTEs
	addl2	$PAGE_OFFSET, mv			# fix up machine vector pointer
	movl	mv, %r10
	addl2	$PAGE_OFFSET, MV_CPU_TYPE_STR(%r10)

	# relocate the interrupt stack. The C code will turn the
	# last page of the interrupt stack into a read-only guard
	# page.

	# FIXME SMP: This needs to select the right stack for this CPU
	# rather than hard-coding the first one. Looks like we need to
	# determine our CPU_ID before this point...
	moval	interrupt_stack, %r0
	addl2	$INT_STACK_SIZE, %r0
	movl	%r0, %sp

	# Now that we have ISP (the interrupt stack pointer) sorted,
	# we need to move over to working on the kernel stack. We do this
	# by loading KSP with the top of the kernel stack for the 'init task'
	# and faking a saved PC/PSL on the interrupt stack which we then
	# 'return' to
	moval	init_thread_union, %r0
	addl2	$8192, %r0			# FIXME: taken from <linux/sched.h>
	mtpr	%r0,$PR_KSP

	pushl	$0x001f0000			# IS=0, accmode=prevmode=K, IPL=31
	pushab	now_on_kstack
	rei
	halt

now_on_kstack:
	calls	$0, vax_start_kernel		# should never return
	halt

msg_loaded:
	.ascii "Boot Head.S loaded at address \0"
	.align 1
msg_registers:
	.ascii "rpb/bootr5/ap/sp \0"
	.align 1
msg_relocated:
	.ascii "relocated at phys address \0"
	.align 1
msg_starting_vm:
	.ascii "Starting VM\0"
	.align 1


#
# Memory locations.
#

# iomap_base holds the physical address of the first PTE in the
# IOMAP portion of the system page table.
# Once VM is enabled, this is replaced with the VIRTUAL address
.globl iomap_base
iomap_base:
	.int 0x00000000

.globl vmallocmap_base
vmallocmap_base:
	.int 0x00000000

# These global symbols allow us to pass stuff to C in a relatively painless
# manner.
.globl boot_ap
boot_ap:
	.int 0x00000000
.globl boot_r11
boot_r11:
	.int 0x00000000
.globl boot_scb
boot_scb:
	.int 0x00000000
# This is here because we need a safe place to store it as we
# relocate around in memory.
boot_mv:
	.int 0x00000000

# Our dummy PCB
__INITDATA
.globl fake_pcb
fake_pcb:
	.fill 24,4,0x00000000

# ...and a pointer to our initial command line
.globl kernel_cmd_line
kernel_cmd_line:
	.int 0x00000000


# vax/mm/mmstart.S 
# Copyright Nov 1998 atp. 
# How to go from mapen=0 to mapen=1 (farmers method)
# This could be simpler if we didnt need to worry about the stack.
# FIXME: all this needs to be initdata..
#
# An 8 MB SPTE must already be set up + loaded by the time we get here...
# page align this function because we want to map it in P0 space

#include <asm/mtpr.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include "boot_sections.h"

__BOOT

.globl VAX_start_mm
.balign 0x200
VAX_start_mm:
	.word 0xffc	# protect registers r2->r11
	movl %sp,%r8	# save the stack pointer
	movl %fp,%r9	# save the frame pointer
	clrl %r4	# clear the decks
	clrl %r5 
	clrl %r6
	clrl %r10
	movab VAX_start_mm, %r10 # This is where we start mapping
	ashl $-9, %r10, %r4      # calculate the pfn of the start of this fn
	movab P0_table,%r3       # Phys address of our P0 table
	addl2 $PAGE_OFFSET,%r3	 # virtual address of our P0 table
	ashl $0x02, %r4, %r5	 # P0 PTE offset in bytes
	subl2 %r5, %r3		 # fake P0BR in bytes
	ashl $-2, %r5, %r6	 # fake P0LR in PTEs	 
	addl2 $0x08, %r6	 # add in length of table
	mtpr %r3, $PR_P0BR       # load P0BR
	mtpr %r6, $PR_P0LR       # load P0LR
	mtpr $PAGE_OFFSET,$PR_P1BR	 # make sure P1BR is sensible
	mtpr $0,  $PR_P1LR	 # and zero length
	movab P0_table,%r3	 # Phys address of P0table.
	movl 0x08,%r2
P0fill:	movl (%r3), %r1		 # fill the P0table with the right pfns
	addl2 %r4, %r1
	movl %r1, (%r3)+
	sobgtr %r2, P0fill
#
# fixup stack 
#
	addl2 $PAGE_OFFSET, 8(%sp) # saved ap.
	addl2 $PAGE_OFFSET, 12(%sp)# fixup saved fp
	addl2 $PAGE_OFFSET, 16(%sp)# fixup saved pc
# active stack
#        movl %sp, %r10
#	subl2 $0x80, %r10
#	movl $0x40, %r9
#	jsb hexdump
#	halt
	addl2 $PAGE_OFFSET, %sp	# fixup sp to virtual address
	addl2 $PAGE_OFFSET, %fp	# fixup fp to virtual address
	movab vreloc, %r6	# where we want to end up (phys)
	addl2 $PAGE_OFFSET, %r6	# & virt

# According to ARM, we should invalidate the whole TB here

	mtpr $0, $PR_TBIA       # clear translation buffer
        mtpr $1, $PR_MAPEN     	# switch on mm.
	jmp (%r6)
	halt
vreloc:				# made it. now fixup sp fp and stack frame
				# cant use prom routines now. 	

	mtpr $PAGE_OFFSET, $PR_P0BR	# Clear out P0BR, P0LR - do not
	mtpr $0, $PR_P0LR		# need them anymore
	mtpr $0, $PR_TBIA		# clear tlb after touching BRs.
	ret

.globl PK_str1
PK_str1:
.align 1
.ascii "value = %8.8x\n\0"

# This the initial P0 page table we use to springboard to S0 space 
# it is used only for mapping this function. so the enabling
# of virtual memory and jmp to S0 space needs to happen here.
#
# initial flags are user_write, valid
# we align this on a page boundary
.globl P0_table
.balign 0x200
P0_table: 	
	.int _PAGE_VALID + _PAGE_UW + 0
	.int _PAGE_VALID + _PAGE_UW + 1
	.int _PAGE_VALID + _PAGE_UW + 2
	.int _PAGE_VALID + _PAGE_UW + 3
	.int _PAGE_VALID + _PAGE_UW + 4
	.int _PAGE_VALID + _PAGE_UW + 5
	.int _PAGE_VALID + _PAGE_UW + 6
	.int _PAGE_VALID + _PAGE_UW + 7


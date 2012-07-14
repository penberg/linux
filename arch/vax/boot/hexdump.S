# Copyright atp Sept 1998
# simple register based hexdumper. aide debug for head.S and friends
# inputs: r10 offset to dump 
#	  r9  number of longwords to dump
#   (0x80 is one page, 0x200 is 4 pages)
# side effects: clobbered registers: r3, r4, r5, r6, r9, r10 
#
.globl hexdump
hexdump:
# This is broken since the C code can clobber r0 to r5 and this
# code has not been updated for this
	halt

# move offset to start dumping at into r4
#	movl $0xffa800,r4
#	movl ap,r4
#	movl $0x0, r4
	movl %r10,%r4
	movl %r9,%r11
	pushl %r10
	calls $1, boot_printint
	calls $1, boot_printspace
        pushl (%r4)
	calls $1, boot_printint
	calls $1, boot_printspace
        pushl %r11
	calls $1, boot_printint
	calls $0, boot_crlf
	clrl %r3
	clrl %r9
LOOP:	
	pushl %r11
	calls $1, boot_printint 
	calls $1, boot_printspace
	pushl %r3
	calls $1, boot_printint  
	calls $1, boot_printspace
	pushl %r4
	calls $1, boot_printint 
	calls $1, boot_printspace
	pushl (%r4)
	calls $1, boot_printint 
	calls $1, boot_printspace
	jmp PASC
	
LRET:	calls $1, boot_crlf
	incl %r3
	incl %r9
# page length on my uvax is about 0x1f lines	
	cmpl %r9, $0x1F
	beql pause
#c 0x80 is one page 0x200 is 4 pages 	
LRET2:	addl2 $4, %r4
	cmpl %r3, %r11
 	bleq LOOP
	rsb
	
PASC:	movl %r4, %r6
	pushl (%r6)
	calls $1, boot_printchar
	incl %r6
	pushl (%r6)
	calls $1, boot_printchar
	incl %r6
	pushl (%r6)
	calls $1, boot_printchar
	incl %r6
	pushl (%r6)
	calls $1, boot_printchar
	jmp LRET

pause:
	pushab more
	calls $1, boot_printstr
GCLOOP: jsb     0x20040044  
        tstl    %r0          
	beql    GCLOOP 
	clrl %r9
	calls $1, boot_crlf
	jmp LRET2

more:
.ascii "----------more-----------\0"
.align 1


/* ld script to make bootable VAX image
 * Written for i386 by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 * Modified by Kenn Humborg <kenn@linux.ie>
 */

#include <asm-generic/vmlinux.lds.h>

/* As of 2.5.16, the jiffy counter is 64-bit and is called jiffies_64.
   For backward-compatibility, a symbol 'jiffies' needs to be aliased
   to the least-significant 32-bits of jiffies_64 */

jiffies = jiffies_64;

SECTIONS
{

  /* kernel is based in S0 space at 0x80100000 */
  . = 0x80000000 + 0x100000;

  /* read-only */
  _text = .;			/* Text and read-only data */
  __kernel_start = .;
  .text : {
        /* boot code comes first.  The fact that it is linked
           at 0x80100000 doesn't matter because it's all 
           position-independent code */
	*(.boot.text)
	*(.boot.data)

        /* Kernel C code starts here */
	*(.text)
	SCHED_TEXT;
	*(.fixup)
	*(.gnu.warning)
	} = 0x9090
  .text.lock : { *(.text.lock) }	/* out-of-line lock text */

  _etext = .;			/* End of text section */

  .rodata : { *(.rodata) }
  .kstrtab : { *(.kstrtab) }

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  __start___ksymtab = .;	/* Kernel symbol table */
  __ksymtab : { *(__ksymtab) }
  __stop___ksymtab = .;

  __start___gpl_ksymtab = .;    /* Kernel symbol table: GPL-only symbols */
  __gpl_ksymtab : { *(__gpl_ksymtab) }
  __stop___gpl_ksymtab = .;

  __start___kallsyms = .;       /* All kernel symbols */
  __kallsyms : { *(__kallsyms) }
  __stop___kallsyms = .;


  /* writeable */
  .data : {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  _edata = .;			/* End of data section */

  . = ALIGN(8192);		/* init_task */
  .data.init_task : { *(.data.init_task) }

  /* will be freed after init */
  . = ALIGN(4096);		/* Init code and data */
  __init_begin = .;
  .init.text : { 
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  .init.data : { *(.init.data) }
  . = ALIGN(16);
  __setup_start = .;
  .init.setup : { *(.init.setup) }
  __setup_end = .;
  __start___param = .;
  __param : { *(__param) }
  __stop___param = .;
  __initcall_start = .;
  .initcall.init : {
	*(.initcall1.init)
	*(.initcall2.init)
	*(.initcall3.init)
	*(.initcall4.init)
	*(.initcall5.init)
	*(.initcall6.init)
	*(.initcall7.init)
  }
  __initcall_end = .;
  __init_cpumatch_start = .;
  .init.cpumatch : { *(.init.cpumatch) }
  __init_cpumatch_end = .;
  . = ALIGN(4096);
  .init.ramfs : {
	__initramfs_start = .;
	*(.init.ramfs)
	__initramfs_end = .;
  }

  . = ALIGN(8);
  .con_initcall.init : {
	__con_initcall_start = .;
	*(.con_initcall.init)
	__con_initcall_end = .;
  }

  . = ALIGN(4096);
  __init_end = .;               /* This is end of loadable image */

  __bss_start = .;		/* BSS */
  .bss : {
	*(.bss)
	}
  _end = .;  /* Boot code uses this to determine how much
                code and data to relocate */

  /* Sections to be discarded */
  /DISCARD/ : {
        *(.exit.text)
        *(.exit.data)
        *(.exitcall.exit)
        }

  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
}

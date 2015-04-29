#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>

OUTPUT_ARCH(vax)
ENTRY(_stext)

jiffies = jiffies_64;

SECTIONS
{
	. = CONFIG_KERNEL_START;
	/* read-only */
	.text : {
		_text = .;	/* Text and read-only data */
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.text.*)
		*(.fixup)
		. = ALIGN (4) ;
		_etext = .;	/* End of text section */
	}

	. = ALIGN(16);
	_sdata =  .;			/* Start of data section */
	RODATA

	EXCEPTION_TABLE(16)

	RW_DATA_SECTION(32, PAGE_SIZE, THREAD_SIZE)

	/* We want the small data sections together, so single-instruction offsets
	   can access them all, and initialized data all before uninitialized, so
	   we can shorten the on-disk segment size.  */
	. = ALIGN(8);
	.sdata : {
		*(.sdata)
	}
	_edata =  .;			/* End of data section */

	/* will be freed after init */
	. = ALIGN(PAGE_SIZE);		/* Init code and data */
	__init_begin = .;

	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)

	/* .exit.text is discarded at runtime, not link time, to deal with
	 * references from .rodata
	 */
	.exit.text : {
		EXIT_TEXT
	}
	.exit.data : {
		EXIT_DATA
	}
	. = ALIGN(PAGE_SIZE);
	__init_end = .;
	/* freed after init ends here */

	BSS_SECTION(0, 0, 0)
	_end = .;
}

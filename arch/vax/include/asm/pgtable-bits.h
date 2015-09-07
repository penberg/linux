#ifndef _ASM_VAX_PGTABLE_BITS_H
#define _ASM_VAX_PGTABLE_BITS_H

/*
 * PTE valid bit <31>:
 */
#define _PAGE_VALID	(1UL << 31)

/*
 * PTE protection field <30:27>:
 */
#define _PAGE_KW	(2UL << 27)
#define _PAGE_PROT_MASK	(15UL << 27)

/*
 * PTE modify bit <26>:
 */
#define _PAGE_MODIFY	(1UL << 26)
#define _PAGE_DIRTY	_PAGE_MODIFY

/*
 * PTE software bits <22:21>:
 */
#define _PAGE_ACCESSED	(1UL << 23)	/* implemented in software */

/*
 * PTE PFN <20:0>
 */
#define _PFN_MASK	(0x80000)

#define _PAGE_CHG_MASK	(_PAGE_VALID|_PAGE_PROT_MASK|_PAGE_MODIFY|_PAGE_ACCESSED|_PFN_MASK)

#endif /* _ASM_VAX_PGTABLE_BITS_H */

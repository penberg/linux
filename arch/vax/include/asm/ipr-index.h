#ifndef _ASM_VAX_IPR_INDEX_H
#define _ASM_VAX_IPR_INDEX_H

/*
 * VAX internal processor registers (IPRs) as specified in Table 8.1 of the
 * VAX Architecture Reference Manual.
 */

#define IPR_KSP		0	/* kernel stack pointer */
#define IPR_ESP		1	/* executive stack pointer */
#define IPR_SSP		2	/* supervisor stack pointer */
#define IPR_USP		3	/* user stack pointer */
#define IPR_ISP		4	/* interrupt stack pointer */
#define IPR_P0BR	8	/* P0 base register */
#define IPR_P0LR	9	/* P0 length register */
#define IPR_P1BR	10	/* P1 base register */
#define IPR_P1LR	11	/* P1 length register */
#define IPR_SBR		12	/* system base register */
#define IPR_SLR		13	/* system limit register */
#define IPR_PCBB	16	/* process control block base */
#define IPR_SCBB	17	/* system control block base */
#define IPR_IPL		18	/* interrupt priority level */
#define IPR_ASTLVL	19	/* AST level */
#define IPR_SIRR	20	/* software interrupt request */
#define IPR_SISR	21	/* software interrupt summary */
#define IPR_ICCS	24	/* interval clock control */
#define IPR_NICR	25	/* next interval count */
#define IPR_ICR		26	/* interval count */
#define IPR_TODR	27	/* time of year */
#define IPR_RXCS	32	/* console receiver status */
#define IPR_RXDB	33	/* console receiver data buffer */
#define IPR_TXCS	34	/* console transmit status */
#define IPR_TXDB	35	/* console transmit data buffer */
#define IPR_MAPEN 	56	/* memory management mnable */
#define IPR_TBIA	57	/* translation buffer invalidate all */
#define IPR_TBIS	58	/* translation buffer invalidate single */
#define IPR_PMR		61	/* performance monitor enable */
#define IPR_SID		62	/* system identification */
#define IPR_TBCHK	63	/* translation buffer check */

#endif /* _ASM_VAX_IPR_INDEX_H */

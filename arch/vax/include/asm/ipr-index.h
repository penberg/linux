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
#define IPR_CPUID	14	/* CPU identification */
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
#define IPR_VPSR	144	/* vector processor status register */
#define IPR_VAER	145	/* vector arithmetic exception register */
#define IPR_VMAC	146	/* vector memory activity check */
#define IPR_VTBIA	147	/* vector TB invalidate all */
#define IPR_VSAR	148	/* vector state address register */

/*
 * System identification register (SID) and system-type register (SYS_TYPE)
 * assignments as specified in Table 8.2 of the VAX architecture reference
 * manual.
 */
#define SID_VAX_11_78X		1		/* VAX-11/780; VAX-11/782; VAX-11/785 */
#define SID_VAX_11_750		2		/* VAX-11/750 */
#define SID_VAX_11_730		3		/* VAX-11/730 */
#define SID_VAX_86X0		4		/* VAX 8600; VAX 8650 */
#define SID_VAX_82X0_83X0	5		/* VAX 8200, 8300; VAX 8250, 8350; VS8000 */
#define SID_VAX_8700_88X0_8500	6		/* VAX 8700, 8800, 8810, 8820-N; VAX 8500 */
#define SID_MICROVAX_I		7		/* MicroVAX-I */
#define SID_MICROVAX_II_CHIP	8
#  define SYS_TYPE_MICROVAX_II		1	/* MicroVAX II */
#  define SYS_TYPE_MICROVAX_2000	2	/* MicroVAX 2000; VAXstation 2000; */
						/* PC/LAN SERVER 2000; MicroVAX Information */
						/* Processor Module */
#  define SYS_TYPE_VAXTERM		5	/* VAXterm */
#  define SYS_TYPE_VAX_9000_CONSOLE	6	/* VAX 9000 Console */
#define SID_CVAX_CHIP		10
#  define SYS_TYPE_MICROVAX_3X00	1	/* MicroVAX 3500 or 3600; VAXserver 3500 or 3600; */
						/* VAXstation 3200 or 3500; MicroVAX 3300, 3400; */
						/* VAXserver 3300, 3400; MicroVAX 3800, 3900; */
						/* VAXserver 3800, 3900 */
#  define SYS_TYPE_VAX_62N0		2	/* VAX 62n0; VAX Fileserver 62n0; VAX 63n0; */
						/* VAX Fileserver 63n0 */
#  define SYS_TYPE_VAXSTATION_35X0	3	/* VAXstation 3520, 3540 */
#  define SYS_TYPE_VAXSTATION_3100	4	/* VAXstation 3100 */
#  define SYS_TYPE_VAXSTATION_3000FT	7	/* VAX 3000FT */
#define SID_REX520_CHIP		11
#  define SYS_TYPE_VAX_4000_300		1	/* VAX 4000-300 */
#  define SYS_TYPE_VAX_6000_4N0		2	/* VAX 6000-4n0 */
#define SID_VAX_9000		14
#define SID_RT_UVAX		16
#  define SYS_TYPE_RTVAX_1000		1	/* rtVAX 1000 */
#define SID_VAX_88X0		17		/* VAX 8820, 8830, 8840 */

#endif /* _ASM_VAX_IPR_INDEX_H */

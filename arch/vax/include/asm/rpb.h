#ifndef _VAX_RPB_H
#define _VAX_RPB_H

/* atp Sep. 1998 VAX arch. Restart Parameter Block */
/* This work is copyright atp 1998 and is licenced under the
 * GNU GPL version 2.
 *
 * History:
 *     1.0 (atp) Cribbed out of VMS internals + Data structures Table 30.22
 *		 Stripped RPB$ off struct names.
 */

#include <linux/types.h>

typedef uint8_t		byte;
typedef uint16_t	word;
typedef uint32_t	longword;
typedef uint64_t	quadword;

struct rpb_struct {
	longword l_base;	/* physical base addr (VMB) */
	longword l_restart;	/* Phys addr of EXE$RESTART */
	longword l_chksum;	/* Checksum of first 31 longwords of exe$restart */
	longword l_rststflg;	/* restart in progress flag (console) */
	longword l_haltpc;	/* PC at halt (vmb) */
	longword l_haltpsl;	/* PSL at halt (vmb) */
	longword l_haltcode;	/* reason for the halt/restart (vmb) */
	longword l_bootr0;	/* saved bootstrap parameters */
	longword l_bootr1;	/* saved bootstrap parameters */
	longword l_bootr2;	/* saved bootstrap parameters */
	longword l_bootr3;	/* saved bootstrap parameters */
	longword l_bootr4;	/* saved bootstrap parameters */
	longword l_bootr5;	/* saved bootstrap parameters */
	longword l_iovec;	/* address of bootstrap driver */
	longword l_iovecsz;	/* size in bytes of bootstrap driver */
	longword l_fillbn;	/* Logical Block no of secondary bootstrap file (us?) */
	longword l_filsiz;	/* size in BLOCKS of secondary bootstrap file */
	quadword q_pfnmap;	/* descriptor of PFN bitmap */
	longword l_pfncnt;	/* count of physical pages */
	longword l_svaspt;	/* system virtual address of system pg table */
	longword l_csrphy;	/* physical addr of UBA device CSR */
	longword l_csrvir;	/* virtual addr of UBA CSR */
	longword l_adpphy;	/* phys addr of adapter config register */
	longword l_adpvir;	/* virtual addr of adapter config register */
	word w_unit;		/* bootstrap device unit number */
	byte b_devtyp;		/* bootstrap device type code */
	byte b_slave;		/* bootstrap device slave unit num */
	char t_file[40];	/* secondary bootstrap file name */
	byte b_confreg[16];	/* byte array of adapter types (11/78x,11/750 only)*/
#if 0
	byte b_hdrpgcnt;	/* count of header pages in secondary boot image */
	word w_bootndt;		/* type of boot adapter */
	byte b_flags;		/* misc flag bits */
#else
				/* Compiler doesn't align these fields correctly */
	byte b_hdrpgcnt;	/* count of header pages in secondary boot image */
	byte b_bootndt_lo;	/* type of boot adapter (low byte) */
	byte b_bootndt_hi;	/* type of boot adapter (high byte) */
	byte b_flags;		/* misc flag bits */
#endif
	longword l_max_pfn;	/* maximum PFN */
	longword l_sptep;	/* system space PTE prototype register */
	longword l_sbr;		/* system base register */
	longword l_cpudbvec;	/* phys addr of per CPU db vector or primary's percpu database */
	longword l_cca_addr;	/* physical address of cca */
	longword l_slr;		/* saved system length register */
	longword l_memdsc[16];	/* array of memory descriptors (bugcheck) */
	longword l_smp_pc;	/* smp boot page physical address */
	byte b_wait[4];		/* loop code for attached processor */
	longword l_badpgs;	/* number of bad pages found on memory scan */
	byte b_ctrlltr;		/* controller letter designation */
	byte b_scbpagct;	/* count of SCB pages */
	byte b_reserved[6];	/* reserved */
	longword l_vmb_version;	/* vmb version number (uvax VMB's) */
};

extern struct rpb_struct boot_rpb;

#endif /* _VAX_RPB_H */

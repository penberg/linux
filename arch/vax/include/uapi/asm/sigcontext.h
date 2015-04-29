#ifndef _ASM_VAX_SIGCONTEXT_H
#define _ASM_VAX_SIGCONTEXT_H

/*
 * Keep this struct definition in sync with the sigcontext fragment
 * in arch/vax/tools/offset.c
 */
struct sigcontext {
#if 0
	unsigned int		sc_regmask;
	unsigned int		sc_psr;
	unsigned int		sc_condition;
	unsigned long		sc_pc;
	unsigned long		sc_regs[32];
	unsigned int		sc_ssflags;
	unsigned int		sc_mdceh;
	unsigned int		sc_mdcel;
	unsigned int		sc_ecr;
	unsigned long		sc_ema;
	unsigned long		sc_sigset[4];
#endif
};

#endif /* _ASM_VAX_SIGCONTEXT_H */

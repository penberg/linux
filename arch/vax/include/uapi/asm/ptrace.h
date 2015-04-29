#ifndef _UAPI_ASM_VAX_PTRACE_H
#define _UAPI_ASM_VAX_PTRACE_H

struct pt_regs {
        unsigned long	r0;
        unsigned long	r1;
        unsigned long	r2;
        unsigned long	r3;
        unsigned long	r4;
        unsigned long	r5;
        unsigned long	r6;
        unsigned long	r7;
        unsigned long	r8;
        unsigned long	r9;
        unsigned long	r10;
        unsigned long	r11;
        unsigned long	ap;
        unsigned long	fp;
        unsigned long	sp;
        unsigned long	pc;
        struct psl	psl;
};

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETFPREGS	14
#define PTRACE_SETFPREGS	15

#endif /* _UAPI_ASM_VAX_PTRACE_H */

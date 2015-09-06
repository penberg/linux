#ifndef _ASM_VAX_SETUP_H
#define _ASM_VAX_SETUP_H

#include <uapi/asm/setup.h>

extern void pagetable_init(void);
extern void pgd_init(unsigned long page);

#endif /* _ASM_VAX_SETUP_H */

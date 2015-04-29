#ifndef _ASM_VAX_IRQFLAGS_H
#define _ASM_VAX_IRQFLAGS_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

#include "asm/ipr-index.h"
#include "asm/ipr.h"
#include "asm/psl.h"

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	flags = mfpr(IPR_IPL);

	return flags;
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	flags = mfpr(IPR_IPL);

	mtpr(0x1f, IPR_IPL);

	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	mtpr(flags, IPR_IPL);
}

static inline void arch_local_irq_enable(void)
{
	struct psl psl;

	movpsl(&psl);

	if (psl.is)
		mtpr(0x01, IPR_IPL);
	else
		mtpr(0x00, IPR_IPL);
}

static inline void arch_local_irq_disable(void)
{
	mtpr(0x1f, IPR_IPL);
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return flags == 0x1f;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* __ASSEMBLY__ */

#endif /* _ASM_VAX_IRQFLAGS_H */

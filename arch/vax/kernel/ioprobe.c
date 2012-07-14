/*
 * Functions for checking if addresses in IO space exist.  Used
 * to probe for devices.  Non-existent addresses trigger a machine
 * check, which is dismissed immediately if probe_resume_addr is
 * non-zero
 * 
 * Inspired by NetBSD/vax.
 */

#include <asm/ioprobe.h>
#include <asm/system.h>

void *probe_resume_addr;

int iospace_probeb(void *virt_addr)
{
	int valid = 1;
	unsigned int flags;

	local_irq_save(flags);

	__asm__ (
	"	movl %2, %%r1			\n"
	"	movab probeb_resume, %1		\n"
	"	tstb (%%r1)			\n"
	"	brb probeb_good			\n"
	"probeb_resume:				\n"
	"	clrl %0				\n"
	"probeb_good:				\n"
	: "=g"(valid), "=g"(probe_resume_addr)
	: "g" (virt_addr), "0"(valid)
	: "r0", "r1");

	probe_resume_addr = NULL;

	local_irq_restore(flags);

	return valid;
}

int iospace_probew(void *virt_addr)
{
	int valid = 1;
	unsigned int flags;

	local_irq_save(flags);

	__asm__ (
	"	movl %2, %%r1			\n"
	"	movab probew_resume, %1		\n"
	"	tstw (%%r1)			\n"
	"	brb probew_good			\n"
	"probew_resume:				\n"
	"	clrl %0				\n"
	"probew_good:				\n"
	: "=g"(valid), "=g" (probe_resume_addr)
	: "g"(virt_addr), "0"(valid)
	: "r0", "r1");

	probe_resume_addr = NULL;

	local_irq_restore(flags);

	return valid;
}

int iospace_probel(void *virt_addr)
{
	int valid = 1;
	unsigned int flags;

	local_irq_save(flags);

	__asm__ (
	"	movl %2, %%r1			\n"
	"	movab probel_resume, %1		\n"
	"	tstb (%%r1)			\n"
	"	brb probel_good			\n"
	"probel_resume:				\n"
	"	clrl %0				\n"
	"probel_good:				\n"
	: "=g"(valid), "=g"(probe_resume_addr)
	: "g"(virt_addr), "0"(valid)
	: "r0", "r1");

	probe_resume_addr = NULL;

	local_irq_restore(flags);

	return valid;
}


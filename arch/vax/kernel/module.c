/*
 * Kernel module help for VAX.
 * Copyright (C) 2001 Rusty Russell.
 * Copyright (C) 2003-2004 Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */

#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

#define DEBUG_VAX_MODULE_LOADER
#ifdef DEBUG_VAX_MODULE_LOADER
#define DEBUGP(fmt...) printk(fmt)
#else
#define DEBUGP(fmt...)
#endif


/*
 * Allocate RAM for a module
 */
void *
module_alloc (unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc (size);
}

/*
 * Free memory returned from module_alloc
 */
void
module_free (struct module *mod, void *module_region)
{
	vfree (module_region);
}

/*
 * Nothing special
 */
int
module_frob_arch_sections (Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
		char *secstrings, struct module *mod)
{
	return 0;
}

/*
 * Do the hard work - relocate
 */
int
apply_relocate(Elf32_Shdr *sechdrs, const char *strtab, unsigned int symindex,
		unsigned int relsec, struct module *me)
{
	unsigned int i;
	Elf32_Rel *rel = (void *) sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	uint32_t *location;

	DEBUGP ("Applying relocate section %u to %u\n", relsec,
			sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/*
		 * This is where to make the change
		 */
		location = (void *) sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/*
		 * This is the symbol it is referring to.  Note that all
		 * undefined symbols have been resolved.
		 */
		sym = (Elf32_Sym *) sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);

		switch (ELF32_R_TYPE(rel[i].r_info)) {
			case R_VAX_32:
				DEBUGP (KERN_ERR "R_VAX_32: loc=%p, v=0x%d\n",
						(void *) *location, sym->st_value);
				*location += sym->st_value;
				break;

			case R_VAX_PC32:
			case R_VAX_PLT32:
				DEBUGP (KERN_ERR "R_VAX_P%s32: loc=%p, v=0x%d\n",
						ELF32_R_TYPE(rel[i].r_info) == R_VAX_PC32?
							"C": "LT",
						(void *) *location, sym->st_value);
				*location += sym->st_value - (uint32_t)location;
				break;

			case R_VAX_GOT32:
				DEBUGP (KERN_ERR "R_VAX_GOT32: loc=%p, v=0x%d\n",
						(void *) *location, sym->st_value);
				/* FIXME */
				printk (KERN_ERR "R_VAX_GOT32 not yet implemented\n");
				return -ENOEXEC;
				break;

			default:
				DEBUGP (KERN_ERR "module %s: Unknown relocation: %u\n",
						me->name, ELF32_R_TYPE(rel[i].r_info));
				return -ENOEXEC;
				break;
		}
	}

	return 0;
}

int
apply_relocate_add (Elf32_Shdr *sechdrs, const char *strtab,
		unsigned int symindex, unsigned int relsec, struct module *me)
{
	printk (KERN_ERR "module %s: ADD RELOCATION unsupported\n", me->name);
	return -ENOEXEC;
}

extern void apply_alternatives(void *start, void *end);

int
module_finalize (const Elf_Ehdr *hdr, const Elf_Shdr *sechdrs,
		struct module *me)
{
	printk (KERN_ERR "Omitted apply_alternatives()...\n");
#if 0
	const Elf_Shdr *s;
	char *secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

	/* look for .altinstructions to patch */
	for (s = sechdrs; s < sechdrs + hdr->e_shnum; s++) {
		void *seg; 
		if (strcmp(".altinstructions", secstrings + s->sh_name))
			continue;
		seg = (void *)s->sh_addr;
		apply_alternatives(seg, seg + s->sh_size);
	}
#endif /* 0 */
	return 0;
}

void
module_arch_cleanup (struct module *mod)
{
}


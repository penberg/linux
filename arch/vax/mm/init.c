#include <linux/mm.h>

unsigned long empty_zero_page;

EXPORT_SYMBOL_GPL(empty_zero_page);

void __init_refok free_initmem(void)
{
}

void __init mem_init(void)
{
}

pgd_t swapper_pg_dir[PTRS_PER_PGD];

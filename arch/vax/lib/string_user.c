/*
 * Copyright (C) 2001, Kenn Humborg
 *
 * These functions are used to do string operations on user memory
 */

#include <linux/string.h>
#include <linux/kernel.h> /* for panic() */

unsigned long __clear_user(void *addr, unsigned long size)
{
	panic("__clear_user: not implemented");
}


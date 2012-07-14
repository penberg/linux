/*
 * Copyright (C) 2001, Kenn Humborg
 *
 * This is a temporary implementation of GCCs negdi2 primitive.
 * Once we get native support in the compiler, this will be
 * removed from here
 *
 */

long long __negdi2(long long x)
{
	__asm__ volatile (
		"	xorl2 $-1, 4(%0)	\n" /* complement high longword */
		"	mnegl (%0), (%0)	\n" /* negate low longword */
		"	bneq 1f			\n" /* no overflow */
		"	incl 4(%0)		\n" /* inc high longword */
		"1:			"
		: : "r"(&x) : "r0");

	return x;
}


#include <linux/kbuild.h>
#include <asm/rpb.h>

int main(void)
{
	DEFINE(RPB_SIZE,	sizeof(struct rpb_struct));

	return 0;
}

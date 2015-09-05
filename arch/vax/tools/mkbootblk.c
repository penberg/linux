#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

struct vax_boot_imgdesc {
	unsigned short arch_type; /* 0x18 for VAX */
	unsigned char check1;
	unsigned char check2;    /* = 0xff ^ (0x18+check1) */
	unsigned long ignored;
	unsigned long blk_count;
	unsigned long load_offset;
	unsigned long start_offset;
	unsigned long checksum; /* blk_count + load_offset + start_offset */
};

struct vax_bootblock_header {
	unsigned short ignored1;
	unsigned char imgdesc_offset; /* offset (in words) to imgdesc */
	unsigned char must_be_1;
	unsigned short lbn_hi;
	unsigned short lbn_lo;
	struct vax_boot_imgdesc imgdesc;
};


int main(int argc, char **argv)
{
	union bootblock {
		struct vax_bootblock_header hdr;
		unsigned char data[512];
	} block;

	struct stat kernelstat;
	int retval;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <kernel-image>\n", argv[0]);
		return 1;
	}

	retval = stat(argv[1], &kernelstat);
	if (retval != 0) {
		fprintf(stderr, "Cannot stat %s: %s\n", argv[1], strerror(errno));
		return 1;
	}

	memset(&block, 0, sizeof(block));

	block.hdr.imgdesc_offset = offsetof(struct vax_bootblock_header, imgdesc) / 2;
	block.hdr.must_be_1 = 1;
	block.hdr.lbn_hi = 0;
	block.hdr.lbn_lo = 1;

	block.hdr.imgdesc.arch_type = 0x18;
	block.hdr.imgdesc.check1 = 0x02;
	block.hdr.imgdesc.check2 = 0xff ^ (0x18 + 0x02);

	/* Round up kernel size to multiple of sector size */
	block.hdr.imgdesc.blk_count = (kernelstat.st_size + 511) / 512;
	block.hdr.imgdesc.load_offset = 0;
	block.hdr.imgdesc.start_offset = 0;
	block.hdr.imgdesc.checksum = block.hdr.imgdesc.blk_count
		+ block.hdr.imgdesc.load_offset
		+ block.hdr.imgdesc.start_offset;

	fwrite(&block, sizeof(block), 1, stdout);

	return 0;
}


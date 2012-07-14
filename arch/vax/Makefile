#
# vax/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and for cleaning up this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

CROSS_COMPILE := vax-linux-

LDFLAGS_BLOB := --format binary --oformat elf32-vax


# Note that this CFLAGS definition will override the definition
# in the top-level Makefile.  We want -O1, not -O2. This is probably
# only needed for our old gcc-2.95, but no longer for gcc-4.1.
CFLAGS += -pipe
CFLAGS += $(call cc-option,-fno-unit-at-a-time)
CFLAGS := $(subst -O2,-O1,$(CFLAGS))

# These flags are used by the top-level makefile when linking
# the kernel
LDFLAGS_vmlinux=-N -nostartfiles -nostdlib -warn-once \
		-Map vmlinux.map -cref

# Tell the top-level makefile about the addition arch-specific
# stuff that's needed
head-y := arch/vax/boot/head.o

init-y += arch/vax/boot/
core-y += arch/vax/kernel/ arch/vax/mm/
libs-y += arch/vax/lib/

TOOLSDIR := arch/vax/tools
MKBOOTBLK := $(obj)/$(TOOLSDIR)/mkbootblk
SETCMDLINE := $(obj)/$(TOOLSDIR)/setcmdline
SHOWCMDLINE := $(obj)/$(TOOLSDIR)/showcmdline
DEFAULT_CMDLINE := "$(CONFIG_CMDLINE)"

# Default target if none specified is to make both MOP and disk images
all: mopboot diskboot

# This will make a MOP-bootable image
mopboot: vmlinux.SYS

.PHONY: TOOLS
TOOLS:
	$(Q)$(MAKE) $(build)=$(TOOLSDIR)

#
# This will make a disk-bootable image.  dd this directly to a
# disk and then tell your VAX to boot from this disk.
#
# NOTE:
# If you want to keep a partition table on the disk, and use
# a boot loader to load  the kernel from a file system, then
# use the vmlinux.SYS and something like asbl. Don't forget to
# set the command line to something useful like "root=/dev/sda1"
#
diskboot: vmlinux.dsk

OBJCOPYFLAGS := -O binary -R .note -R .comment -S

quiet_cmd_vmlinux.bin = OBJCOPY $@
cmd_vmlinux.bin = $(OBJCOPY) $(OBJCOPYFLAGS) $< $@

vmlinux.bin: vmlinux
	$(call cmd,vmlinux.bin)

quiet_cmd_setcmd = SETCMD  $@ "$(DEFAULT_CMDLINE)"
cmd_setcmd = $(SETCMDLINE) $@ "$(DEFAULT_CMDLINE)"

quiet_cmd_mksys = MKSYS   $@
cmd_mksys = dd if=/dev/zero bs=512 count=1 of=$@ 2>/dev/null && cat $< >> $@

vmlinux.SYS: vmlinux.bin TOOLS
	$(call cmd,mksys)
	$(call cmd,setcmd)

quiet_cmd_mkdsk = MKDSK   $@
cmd_mkdsk = $(MKBOOTBLK) $< > $@ && cat $< >> $@

vmlinux.dsk: vmlinux.bin TOOLS
	$(call cmd,mkdsk)
	$(call cmd,setcmd)

# This is helpful for low level debuggery
listfile:
	rm -f vmlinux.lst
	$(OBJDUMP) -D vmlinux > vmlinux.lst

CLEAN_FILES += include/asm-$(ARCH)/asm_offsets.h

archclean:
	@echo 'Cleaning up (arch/vax)'
	@rm -f vmlinux.* vmlinux
	@cd $(TOPDIR)/arch/vax ; rm -f *.out TEST.BIN TEST.SYS
	@cd $(TOOLSDIR) ; rm -f setcmdline showcmdline mkbootblk

archmrproper:

define archhelp
  echo '* mopboot         - MOP-bootable kernel image (vmlinux.SYS)'
  echo '* diskboot        - HDD bootable kernel image (vmlinux.dsk)'
  echo ''
  echo 'Notice: module support is not complete right now'
endef

clean:  archclean


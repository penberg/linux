KBUILD_DEFCONFIG	:= vax_defconfig
KBUILD_IMAGE		:= vmlinux.bin

head-y := arch/vax/kernel/head.o
libs-y += arch/vax/lib/
core-y += arch/vax/kernel/ arch/vax/mm/

all: $(KBUILD_IMAGE)

boot := arch/vax/boot

archprepare:
	$(Q)$(MAKE) $(build)=arch/vax/tools

vmlinux.bin: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

vmlinux.SYS: vmlinux.bin
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	@$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '* vmlinux.bin	- Stripped kernel ELF image (arch/$(ARCH)/boot/vmlinux.bin)'
  echo  '  vmlinux.SYS	- Stripped kernel MOP image (arch/$(ARCH)/boot/vmlinux.SYS)'
endef

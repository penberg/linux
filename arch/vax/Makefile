KBUILD_DEFCONFIG := vax_defconfig

head-y := arch/vax/kernel/head.o
libs-y += arch/vax/lib/
core-y += arch/vax/kernel/ arch/vax/mm/

boot := arch/vax/boot

vmlinux.bin: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	@$(MAKE) $(clean)=$(boot)

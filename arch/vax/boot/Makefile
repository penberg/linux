targets	:= vmlinux.bin vmlinux.SYS vmlinux.dsk

OBJCOPYFLAGS := -O binary -R .note.gnu.build-id -R .note -R .comment -S

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

quiet_cmd_mkmop = MKMOP   $@
      cmd_mkmop = dd if=/dev/zero bs=512 count=1 of=$@ 2>/dev/null && \
		  cat $< >> $@

$(obj)/vmlinux.SYS: $(obj)/vmlinux.bin
	$(call if_changed,mkmop)

quiet_cmd_mkdsk = MKDSK   $@
      cmd_mkdsk = $(objtree)/arch/vax/tools/mkbootblk $< > $@ && \
		  cat $< >> $@

$(obj)/vmlinux.dsk: $(obj)/vmlinux.bin
	$(call if_changed,mkdsk)

clean-files += vmlinux.bin vmlinux.SYS vmlinux.dsk

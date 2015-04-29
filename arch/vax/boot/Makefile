targets	:= vmlinux.bin

OBJCOPYFLAGS := -O binary -R .note.gnu.build-id -R .note -R .comment -S

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo 'Kernel: $@ is ready'

clean-files += vmlinux.bin

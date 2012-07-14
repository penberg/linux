

hostprogs-y := mkbootblk setcmdline
extra-y := mkbootblk setcmdline showcmdline

quiet_cmd_showcmdline = LN      $@ -> $<
cmd_showcmdline = ln -f $< $@

$(obj)/showcmdline: $(obj)/setcmdline
	$(call cmd,showcmdline)


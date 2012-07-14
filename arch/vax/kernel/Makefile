#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically
# DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

extra-y := vmlinux.lds

obj-y   := ptrace.o process.o setup.o regdump.o interrupt.o entry.o time.o \
		ioprobe.o syscall.o signal.o semaphore.o vax_dev_init.o \
		init_task.o reboot.o cpu_generic.o clock.o \

obj-y	+= bootcons/

obj-$(CONFIG_CPU_KA42)	+= cpu_ka42.o
obj-$(CONFIG_CPU_KA43)	+= cpu_ka43.o
obj-$(CONFIG_CPU_KA46)	+= cpu_ka46.o
obj-$(CONFIG_CPU_KA48)	+= cpu_ka48.o
obj-$(CONFIG_CPU_KA49)	+= cpu_ka49.o
obj-$(CONFIG_CPU_KA52)	+= cpu_ka52.o
obj-$(CONFIG_CPU_KA55)	+= cpu_ka55.o
obj-$(CONFIG_CPU_KA62)	+= cpu_ka62.o
obj-$(CONFIG_CPU_KA410)	+= cpu_ka410.o
obj-$(CONFIG_CPU_KA630)	+= cpu_ka630.o
obj-$(CONFIG_CPU_KA640)	+= cpu_ka640.o
obj-$(CONFIG_CPU_KA650)	+= cpu_ka650.o
obj-$(CONFIG_CPU_KA660)	+= cpu_ka660.o
obj-$(CONFIG_CPU_VXT)	+= cpu_vxt.o

obj-$(CONFIG_MODULES)	+= module.o
obj-$(CONFIG_VAX_DIAG_LED)	+= diag_led.o

obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o


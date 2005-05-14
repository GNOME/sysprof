ifneq ($(KERNELRELEASE),)

obj-m	:= sysprof-module.o
CFLAGS += $(MODCFLAGS) -DKERNEL26

else

ifeq ($(PREFIX),)
PREFIX	:= /usr/local
endif

MODULE    := sysprof-module
KDIR      := /lib/modules/$(shell uname -r)/build
INCLUDE   := -isystem $(KDIR)/include
MODCFLAGS := -DMODULE -D__KERNEL__ -Wall ${INCLUDE}

KMAKE	  := $(MAKE) -C $(KDIR) SUBDIRS=$(PWD)

modules: $(MODULE).o

insert_module: install
	modprobe -r sysprof-module
	modprobe sysprof-module

ifneq ($(shell (uname -r | grep 2.6) > /dev/null ; echo -n $$?),0)
	echo A 2.6 kernel is required; exit 1
endif

# build module

$(MODULE).o: $(MODULE).c
	$(KMAKE) modules

endif

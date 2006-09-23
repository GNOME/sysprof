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


# Automake rules, as per "Third-Party Makefiles" in the automake manual

all: $(MODULE).o

distdir:
	cp sysprof-module.c $(distdir)
	cp sysprof-module.h $(distdir)
	cp Makefile $(distdir)

install:
	$(KMAKE) modules_install
	[ -e /sbin/depmod ] && /sbin/depmod

install-data:
install-exec:
uninstall:

install-info:
installdirs:
check:
installcheck:
mostlyclean:
clean:
	rm -f sysprof-module.ko
	rm -f sysprof-module.o
	rm -f sysprof-module.mod.o
	rm -f sysprof-module.mod.c
distclean: clean
maintainer-clean:
dvi:
pdf:
info:
html:
tags:
ctags:

endif


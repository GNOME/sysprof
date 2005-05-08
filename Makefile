ifneq ($(KERNELRELEASE),)

obj-m	:= sysprof-module.o
CFLAGS += $(MODCFLAGS) -DKERNEL26

else

ifeq ($(PREFIX),)
PREFIX	:= /usr/local
endif

BINDIR		:= $(PREFIX)/bin
GLADE_DIR	:= $(PREFIX)/share/sysprof
PIXMAP_DIR	:= $(PREFIX)/share/pixmaps/sysprof

CFLAGS    := $(shell pkg-config --cflags gtk+-2.0 libglade-2.0) -Wall -g -DGLADE_DIR=\"$(GLADE_DIR)\" -DPIXMAP_DIR=\"$(PIXMAP_DIR)\"
LIBS      := $(shell pkg-config --libs gtk+-2.0 libglade-2.0) -lbfd -liberty 
C_FILES	:= sysprof.c binfile.c stackstash.c watch.c process.c	\
profile.c treeviewutils.c sfile.c
OBJS	  := $(addsuffix .o, $(basename $(C_FILES)))
BINARY    := sysprof
MODULE    := sysprof-module
KDIR      := /lib/modules/$(shell uname -r)/build
INCLUDE   := -isystem $(KDIR)/include
MODCFLAGS := -DMODULE -D__KERNEL__ -Wall ${INCLUDE}
MODULE    := sysprof-module

KMAKE	  := $(MAKE) -C $(KDIR) SUBDIRS=$(PWD)

all:	binaries
	@echo ==========================================
	@echo
	@echo To install sysprof type
	@echo 
	@echo \ \ \ make PREFIX=\<prefix\> install
	@echo
	@echo as root. For example:
	@echo
	@echo \ \ \ make PREFIX=/usr install
	@echo 

binaries: check $(BINARY) $(MODULE).o

check:
	pkg-config gtk+-2.0 libglade-2.0
	@[ -r $(KDIR)/include/linux/kernel.h ] || (echo No kernel headers found; exit 1)
#	@[ -r /usr/include/bzlib.h ] || (echo bzip2 header file not found; exit 1)
	touch check

$(BINARY): $(OBJS) depend
	$(CC) $(OBJS) $(LIBS) -o$(BINARY)
clean:
	rm -f $(OBJS) $(BINARY) $(MODULE).o *~ core* depend.mk

depend:	
	$(CC) -MM $(CFLAGS) $(C_FILES) > depend.mk

install: binaries
	@echo
	@echo Installing in $(PREFIX)
	@echo

# binary
	@mkdir -p		$(BINDIR)
	cp sysprof		$(BINDIR)

# glade file
	mkdir -p		$(GLADE_DIR)
	cp sysprof.glade	$(GLADE_DIR)

# icon
	mkdir -p		$(PIXMAP_DIR)
	cp sysprof-icon.png	$(PIXMAP_DIR)

# kernel module
	$(KMAKE) modules_install

	depmod

	@echo ======================================
	@echo
	@echo To run sysprof first insert the module by typing
	@echo
	@echo \ \ \ \ modprobe sysprof-module
	@echo
	@echo as root. Then run \"$(PREFIX)/bin/sysprof\".
	@echo 

insert_module: install
	modprobe -r sysprof-module
	modprobe sysprof-module

depend.mk:
	touch depend.mk
	$(MAKE) depend

include depend.mk

.PHONY: depend all

ifneq ($(shell (uname -r | grep 2.6) > /dev/null ; echo -n $$?),0)
	echo A 2.6 kernel is required; exit 1
endif

# build module

$(MODULE).o: $(MODULE).c
	$(KMAKE) modules

endif

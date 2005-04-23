ifneq ($(KERNELRELEASE),)

obj-m	:= sysprof-module.o
CFLAGS += $(MODCFLAGS) -DKERNEL26

else

CFLAGS    := $(shell pkg-config --cflags gtk+-2.0 libglade-2.0) -Wall -g
LIBS      := $(shell pkg-config --libs gtk+-2.0 libglade-2.0) -lbfd -liberty -lbz2
C_FILES	:= sysprof.c binfile.c stackstash.c watch.c process.c	\
profile.c treeviewutils.c sfile.c
OBJS	  := $(addsuffix .o, $(basename $(C_FILES)))
BINARY    := sysprof
MODULE    := sysprof-module
KDIR      := /lib/modules/$(shell uname -r)/build
INCLUDE   := -isystem $(KDIR)/include
MODCFLAGS := -DMODULE -D__KERNEL__ -Wall ${INCLUDE}
MODULE    := sysprof-module

all: check $(BINARY) $(MODULE).o

check:
	pkg-config gtk+-2.0 libglade-2.0
	@[ -r $(KDIR)/include/linux/kernel.h ] || (echo No kernel headers found; exit 1)
	@[ -r /usr/include/bzlib.h ] || (echo bzip2 header file not found; exit 1)
	touch check

$(BINARY): $(OBJS) depend
	$(CC) $(OBJS) $(LIBS) -o$(BINARY)
clean:
	rm -f $(OBJS) $(BINARY) $(MODULE).o *~ core* depend.mk

depend:	
	$(CC) -MM $(CFLAGS) $(C_FILES) > depend.mk

depend.mk:
	touch depend.mk
	$(MAKE) depend

include depend.mk

.PHONY: depend all

ifeq ($(shell (uname -r | grep 2.6) > /dev/null ; echo -n $$?),0)

# if kernel 2.6
$(MODULE).o: $(MODULE).c
#	echo modcflags $(MODCFLAGS)

	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
else

MODCFLAGS += -DKERNEL24

$(MODULE).o: $(MODULE).c
	$(CC) $(MODCFLAGS) $(MODULE).c -c -o$(MODULE).o


endif
endif

ifneq ($(KERNELRELEASE),)

obj-m	:= sysprof-module.o
CFLAGS += $(MODCFLAGS) -DKERNEL26

else

CFLAGS    := $(shell pkg-config --cflags gtk+-2.0 libglade-2.0) -Wall -g
LIBS      := $(shell pkg-config --libs gtk+-2.0 libglade-2.0) -lbfd -liberty
C_FILES	  := sysprof.c binfile.c stackstash.c watch.c process.c profile.c treeviewutils.c
OBJS	  := $(addsuffix .o, $(basename $(C_FILES)))
BINARY    := sysprof
MODULE    := sysprof-module
INCLUDE   := -isystem /lib/modules/`uname -r`/build/include
MODCFLAGS := -O2 -DMODULE -D__KERNEL__ -Wall ${INCLUDE}
KDIR      := /lib/modules/$(shell uname -r)/build
MODULE    := sysprof-module

all: $(BINARY) $(MODULE).o

$(BINARY): $(OBJS) depend
	gcc $(OBJS) $(LIBS) -o$(BINARY)
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
	echo birnan $(MODCFLAGS)

	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
else

MODCFLAGS += -DKERNEL24

$(MODULE).o: $(MODULE).c
	gcc $(MODCFLAGS) $(MODULE).c -c -o$(MODULE).o


endif
endif

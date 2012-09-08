ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

#SUBDIRS = $(MODULE_SUBDIR)
#DIST_SUBDIRS = module

bin_PROGRAMS = sysprof-cli

if BUILD_GUI
bin_PROGRAMS += sysprof 
endif

SYSPROF_CORE =					\
	binfile.h				\
	binfile.c				\
	collector.c				\
	collector.h				\
	demangle.c				\
	elfparser.c				\
	elfparser.h				\
	profile.h				\
	profile.c				\
	sfile.h					\
	sfile.c					\
	sformat.h				\
	sformat.c				\
	stackstash.h				\
	stackstash.c				\
	tracker.h				\
	tracker.c				\
	unwind.h				\
	unwind.c				\
	watch.h					\
	watch.c					\
						\
	util.h

#	module/sysprof-module.h			

#
# GUI version
#
if BUILD_GUI

sysprof_SOURCES =				\
	$(SYSPROF_CORE)				\
	footreestore.c				\
	footreestore.h				\
	footreedatalist.h			\
	footreedatalist.c			\
	treeviewutils.h				\
	treeviewutils.c				\
	sysprof.c

sysprof_CPPFLAGS =				\
	$(GUI_DEP_CFLAGS)			\
	-DDATADIR=\"$(pkgdatadir)\"		\
	-DPIXMAPDIR=\"$(pixmapsdir)\"

sysprof_LDADD = $(GUI_DEP_LIBS)

endif

udevdir = $(sysconfdir)/udev/rules.d
dist_udev_DATA = 60-sysprof.rules

pixmapsdir = $(datadir)/pixmaps

dist_pkgdata_DATA = sysprof.glade
dist_pixmaps_DATA = sysprof-icon-16.png sysprof-icon-24.png sysprof-icon-32.png sysprof-icon-48.png

#
# Command line version
#

sysprof_cli_SOURCES =				\
	$(SYSPROF_CORE)				\
	signal-handler.h			\
	signal-handler.c			\
	sysprof-cli.c

sysprof_cli_CPPFLAGS =				\
	$(CORE_DEP_CFLAGS)

sysprof_cli_LDADD = $(CORE_DEP_LIBS)

#
# Module stuff
#  

#EXTRA_DIST =					\
#	module/sysprof-module.c			\
#	module/sysprof-module.h			\
#	module/Makefile

#insert-module:
#	/sbin/modprobe -r sysprof-module
#	/sbin/modprobe sysprof-module

# 
# Test programs
#
noinst_PROGRAMS = testelf testunwind testdemangle

# testunwind
testunwind_SOURCES =	\
	testunwind.c	\
	demangle.c	\
	elfparser.c	\
	elfparser.h	\
	unwind.c	\
	unwind.h
testunwind_CPPFLAGS = $(CORE_DEP_CFLAGS)
testunwind_LDADD = $(CORE_DEP_LIBS)

# testelf
testelf_SOURCES =	\
	testelf.c	\
	demangle.c	\
	elfparser.c	\
	elfparser.h

testelf_CPPFLAGS = $(CORE_DEP_CFLAGS)
testelf_LDADD = $(CORE_DEP_LIBS)

# testdemangle
testdemangle_SOURCES =	\
	testdemangle.c	\
	elfparser.c	\
	elfparser.h	\
	demangle.c
testdemangle_CPPFLAGS = $(CORE_DEP_CFLAGS)
testdemangle_LDADD = $(CORE_DEP_LIBS)

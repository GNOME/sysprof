SUBDIRS = $(MODULE_SUBDIR)
DIST_SUBDIRS = module

bin_PROGRAMS = sysprof-text

noinst_PROGRAMS = testelf
testelf_SOURCES = testelf.c elfparser.c elfparser.h binparser.c binparser.h
testelf_CPPFLAGS =				\
	$(CORE_DEP_CFLAGS)
testelf_LDADD = $(CORE_DEP_LIBS)

if BUILD_GUI
bin_PROGRAMS += sysprof 
endif

SYSPROF_CORE =					\
	binfile.h				\
	binfile.c				\
	collector.c				\
	collector.h				\
	process.h				\
	process.c				\
	profile.h				\
	profile.c				\
	sfile.h					\
	sfile.c					\
	sformat.h				\
	sformat.c				\
	stackstash.h				\
	stackstash.c				\
	module/sysprof-module.h			\
	watch.h					\
	watch.c

#
# GUI version
#
if BUILD_GUI

sysprof_SOURCES =				\
	$(SYSPROF_CORE)				\
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
dist_pixmaps_DATA = sysprof-icon.png

#
# Command line version
#

sysprof_text_SOURCES =				\
	$(SYSPROF_CORE)				\
	signal-handler.h			\
	signal-handler.c			\
	sysprof-text.c

sysprof_text_CPPFLAGS =				\
	$(CORE_DEP_CFLAGS)

sysprof_text_LDADD = $(CORE_DEP_LIBS)

#
# Module stuff
#  

EXTRA_DIST =					\
	module/sysprof-module.c			\
	module/sysprof-module.h			\
	module/Makefile

insert-module:
	modprobe -r sysprof-module
	modprobe sysprof-module

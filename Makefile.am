SUBDIRS = $(MODULE_SUBDIR)
DIST_SUBDIRS = module

bin_PROGRAMS = sysprof sysprof-text

SYSPROF_CORE =					\
	binfile.h				\
	binfile.c				\
	process.h				\
	process.c				\
	profile.h				\
	profile.c				\
	profiler.c				\
	profiler.h				\
	sfile.h					\
	sfile.c					\
	stackstash.h				\
	stackstash.c				\
	module/sysprof-module.h			\
	watch.h					\
	watch.c

sysprof_SOURCES =				\
	$(SYSPROF_CORE)				\
	treeviewutils.h				\
	treeviewutils.c				\
	sysprof.c

sysprof_text_SOURCES =				\
	$(SYSPROF_CORE)				\
	signal-handler.h			\
	signal-handler.c			\
	sysprof-text.c

sysprof_LDADD = $(DEP_LIBS)

sysprof_text_LDADD = $(DEP_LIBS)

pixmapsdir = $(datadir)/pixmaps

INCLUDES =					\
	$(DEP_CFLAGS)				\
	-DDATADIR=\"$(pkgdatadir)\"		\
	-DPIXMAPDIR=\"$(pixmapsdir)\"

EXTRA_DIST =					\
	module/sysprof-module.c			\
	module/sysprof-module.h			\
	module/Makefile

dist_pkgdata_DATA = sysprof.glade
dist_pixmaps_DATA = sysprof-icon.png

insert-module:
	modprobe -r sysprof-module
	modprobe sysprof-module

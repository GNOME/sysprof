bin_PROGRAMS = sysprof
pkgdata_DATA = sysprof.glade sysprof-icon.png

sysprof_SOURCES =	        \
	binfile.h		\
	binfile.c		\
	process.h		\
	process.c		\
	profile.h		\
	profile.c		\
	sfile.h			\
	sfile.c			\
	stackstash.h		\
	stackstash.c		\
	sysprof-module.h	\
	sysprof.c		\
	treeviewutils.h		\
	treeviewutils.c		\
	watch.h			\
	watch.c

sysprof_LDADD = $(DEP_LIBS)

INCLUDES = 					   \
	$(DEP_CFLAGS)				   \
	-DDATADIR=\"$(pkgdatadir)\"		   \
	-DPIXMAPDIR=\"$(datadir)/pixmaps\"

#	memprof.desktop		
#	memprof.spec.in

EXTRA_DIST =			\
	sysprof.glade	  	\
	sysprof-icon.png

pixmapsdir = $(datadir)/pixmaps
pixmaps_DATA = sysprof-icon.png

modulesdir = /lib/modules/`uname -r`/kernel/drivers
modules_DATA = module/sysprof-module.ko

CLEANFILES =				\
	module/sysprof-module.mod.c	\
	module/sysprof-module.mod.h	\
	module/sysprof-module.mod.o	\
	module/sysprof-module.o		\
	module/sysprof-module.ko

module/sysprof-module.ko:
	(cd module && $(MAKE))

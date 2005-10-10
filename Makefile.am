SUBDIRS = $(MODULE_SUBDIR)
DIST_SUBDIRS = module

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
	module/sysprof-module.h	\
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
	sysprof-icon.png	\
	module/sysprof-module.c \
	module/sysprof-module.h	\
	module/Makefile

pixmapsdir = $(datadir)/pixmaps
pixmaps_DATA = sysprof-icon.png

insert-module:
	modprobe -r sysprof-module
	modprobe sysprof-module

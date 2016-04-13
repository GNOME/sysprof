bin_PROGRAMS = sysprof

sysprof_CFLAGS = \
	$(SYSPROF_CFLAGS) \
	-I$(top_srcdir)/lib \
	-I$(top_builddir)/lib

sysprof_SOURCES = \
	sysprof.c \
	sp-application.c \
	sp-application.h \
	sp-credits.h \
	sp-window.c \
	sp-window.h \
	sp-window-settings.c \
	sp-window-settings.h \
	$(NULL)

nodist_sysprof_SOURCES = \
	sp-resources.c \
	sp-resources.h

sysprof_LDADD = \
	$(SYSPROF_LIBS) \
	$(top_builddir)/lib/libsysprof-@API_VERSION@.la

EXTRA_DIST =
CLEANFILES =
DISTCLEANFILES =
BUILT_SOURCES =

glib_resources_xml = resources/sysprof.gresource.xml
glib_resources_c = sp-resources.c
glib_resources_h = sp-resources.h
glib_resources_namespace = sysprof
include $(top_srcdir)/gresources.mk

-include $(top_srcdir)/git.mk

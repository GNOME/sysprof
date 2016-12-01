if ENABLE_GTK

bin_PROGRAMS = sysprof

sysprof_CFLAGS = \
	$(GIO_CFLAGS) \
	$(GTK_CFLAGS) \
	$(POLKIT_CFLAGS) \
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
	$(GIO_LIBS) \
	$(GTK_LIBS) \
	$(LIBM) \
	$(POLKIT_LIBS) \
	$(top_builddir)/lib/libsysprof-@API_VERSION@.la \
	$(top_builddir)/lib/libsysprof-ui-@API_VERSION@.la \
	$(NULL)

EXTRA_DIST = meson.build
CLEANFILES =
DISTCLEANFILES =
BUILT_SOURCES =

glib_resources_xml = resources/sysprof.gresource.xml
glib_resources_c = sp-resources.c
glib_resources_h = sp-resources.h
glib_resources_namespace = sysprof
include $(top_srcdir)/gresources.mk

endif

-include $(top_srcdir)/git.mk

bin_PROGRAMS = sysprof-cli
noinst_PROGRAMS = sysprof-dump
EXTRA_DIST = meson.build

AM_CPPFLAGS = \
	$(GIO_CFLAGS) \
	-I$(top_srcdir)/lib \
	-I$(top_builddir)/lib

sysprof_cli_SOURCES = sysprof-cli.c
sysprof_cli_LDADD = \
	$(GIO_LIBS) \
	$(top_builddir)/lib/libsysprof-@API_VERSION@.la

sysprof_dump_SOURCES = sysprof-dump.c
sysprof_dump_LDADD = \
	$(GIO_LIBS) \
	$(top_builddir)/lib/libsysprof-@API_VERSION@.la

-include $(top_srcdir)/git.mk

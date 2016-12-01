TESTS =
TEST_PROGS =
EXTRA_DIST = meson.build

test_cflags = \
	$(GIO_CFLAGS) \
	$(POLKIT_CFLAGS) \
	-I$(top_srcdir)/lib \
	-I$(top_builddir)/lib

test_libs = \
	$(GIO_LIBS) \
	$(POLKIT_LIBS) \
	$(top_builddir)/lib/libsysprof-@API_VERSION@.la

TESTS += test-capture
TEST_PROGS += test-capture
test_capture_SOURCES = test-capture.c
test_capture_CFLAGS = $(test_cflags)
test_capture_LDADD = $(test_libs)

TESTS += test-capture-cursor
TEST_PROGS += test-capture-cursor
test_capture_cursor_SOURCES = test-capture-cursor.c
test_capture_cursor_CFLAGS = $(test_cflags)
test_capture_cursor_LDADD = $(test_libs)


if ENABLE_GTK

test_ui_cflags = \
	$(GIO_CFLAGS) \
	$(GTK_CFLAGS) \
	$(POLKIT_CFLAGS) \
	-I$(top_srcdir)/lib \
	-I$(top_builddir)/lib
test_ui_libs = \
	$(GIO_LIBS) \
	$(GTK_LIBS) \
	$(POLKIT_LIBS) \
	$(top_builddir)/lib/libsysprof-@API_VERSION@.la \
	$(top_builddir)/lib/libsysprof-ui-@API_VERSION@.la


TESTS += test-model-filter
TEST_PROGS += test-model-filter
test_model_filter_SOURCES = test-model-filter.c
test_model_filter_CFLAGS = $(test_ui_cflags)
test_model_filter_LDADD = $(test_ui_libs)

TESTS += test-zoom
TEST_PROGS += test-zoom
test_zoom_SOURCES = test-zoom.c
test_zoom_CFLAGS = $(test_ui_cflags)
test_zoom_LDADD = $(test_ui_libs)

endif

TEST_ENVIRONMENT = \
	G_TEST_SRCDIR="$(abs_srcdir)" \
	G_TEST_BUILDDIR="$(abs_builddir)" \
	G_DEBUG=gc-friendly \
	GSETTINGS_BACKEND=memory \
	MALLOC_CHECK_=2 \
	MALLOC_PERTURB_=$$(($${RANDOM:-256} % 256))

LOG_COMPILER = $(top_srcdir)/tap-test

noinst_PROGRAMS = $(TESTS)
check_PROGRAMS = $(TESTS)

-include $(top_srcdir)/git.mk

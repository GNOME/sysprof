TESTS =

test_cflags = \
	$(SYSPROF_CFLAGS) \
	-I$(top_srcdir)/lib \
	-I$(top_builddir)/lib

test_libs = \
	$(SYSPROF_LIBS) \
	$(top_builddir)/lib/libsysprof-@API_VERSION@.la


TESTS += test-model-filter
test_model_filter_SOURCES = test-model-filter.c
test_model_filter_CFLAGS = $(test_cflags)
test_model_filter_LDADD = $(test_libs)

TESTS += test-capture
test_capture_SOURCES = test-capture.c
test_capture_CFLAGS = $(test_cflags)
test_capture_LDADD = $(test_libs)

TEST_ENVIRONMENT = \
	G_TEST_SRCDIR="$(abs_srcdir)" \
	G_TEST_BUILDDIR="$(abs_builddir)" \
	G_DEBUG=gc-friendly \
	GSETTINGS_BACKEND=memory \
	MALLOC_CHECK_=2 \
	MALLOC_PERTURB_=$$(($${RANDOM:-256} % 256))

TEST_PROGS = test-model-filter
LOG_COMPILER = $(top_srcdir)/build-aux/tap-test

noinst_PROGRAMS = $(TESTS)
check_PROGRAMS = $(TESTS)

-include $(top_srcdir)/git.mk

noinst_LTLIBRARIES =
lib_LTLIBRARIES =
EXTRA_DIST =
CLEANFILES =
DISTCLEANFILES =
BUILT_SOURCES =

# Both the profiler library and the UI library need to share some
# data-structures. Notably, the StackStash. So we build a private
# static library that can be used from both. It adds a little bit
# of overhead in terms of duplicated procedures, but they should
# always be installed in sync, and therefore no big deal.
noinst_LTLIBRARIES += libutil.la
libutil_la_SOURCES = \
	util/binfile.c \
	util/binfile.h \
	util/demangle.cpp \
	util/demangle.h \
	util/elfparser.c \
	util/elfparser.h \
	util/pointcache.c \
	util/pointcache.h \
	util/stackstash.c \
	util/stackstash.h \
	$(NULL)
libutil_la_CFLAGS = \
	-I$(srcdir)/util \
	$(GIO_CFLAGS) \
	$(WARN_CFLAGS) \
	$(NULL)
libutil_la_CXXFLAGS = \
	-I$(srcdir)/util \
	$(GIO_CFLAGS) \
	$(WARN_CXXFLAGS) \
	$(NULL)


# We split up the library code into two libraries.  One containing the
# minimum necessary to do profiling on a particular host (that might not
# have GTK+ installed), and the library containing reusable GTK
# components (for IDE integration).
#
lib_LTLIBRARIES += libsysprof-@API_VERSION@.la
headersdir = $(includedir)/sysprof-@API_VERSION@
headers_DATA = \
	sysprof.h \
	sysprof-version.h \
	sp-address.h \
	sp-callgraph-profile.h \
	sp-capture-condition.h \
	sp-capture-cursor.h \
	sp-capture-reader.h \
	sp-capture-writer.h \
	sp-capture-types.h \
	sp-clock.h \
	sp-elf-symbol-resolver.h \
	sp-error.h \
	sp-gjs-source.h \
	sp-hostinfo-source.h \
	sp-jitmap-symbol-resolver.h \
	sp-kernel-symbol.h \
	sp-kernel-symbol-resolver.h \
	sp-local-profiler.h \
	sp-map-lookaside.h \
	sp-perf-source.h \
	sp-proc-source.h \
	sp-profile.h \
	sp-profiler.h \
	sp-source.h \
	sp-symbol-resolver.h \
	$(NULL)

libsysprof_@API_VERSION@_la_SOURCES = \
	$(headers_DATA) \
	sp-address.c \
	sp-callgraph-profile.c \
	sp-callgraph-profile-private.h \
	sp-capture-condition.c \
	sp-capture-cursor.c \
	sp-capture-reader.c \
	sp-capture-writer.c \
	sp-clock.c \
	sp-elf-symbol-resolver.c \
	sp-error.c \
	sp-gjs-source.c \
	sp-hostinfo-source.c \
	sp-jitmap-symbol-resolver.c \
	sp-kernel-symbol.c \
	sp-kernel-symbol-resolver.c \
	sp-line-reader.c \
	sp-line-reader.h \
	sp-local-profiler.c \
	sp-map-lookaside.c \
	sp-perf-counter.c \
	sp-perf-counter.h \
	sp-perf-source.c \
	sp-platform.c \
	sp-platform.h \
	sp-proc-source.c \
	sp-profile.c \
	sp-profiler.c \
	sp-source.c \
	sp-symbol-resolver.c \
	$(NULL)

libsysprof_@API_VERSION@_la_CFLAGS = \
	-I$(srcdir)/util \
	$(GIO_CFLAGS) \
	$(WARN_CFLAGS) \
	$(NULL)

libsysprof_@API_VERSION@_la_LIBADD = \
	libutil.la \
	-lstdc++ \
	$(GIO_LIBS) \
	$(NULL)

if ENABLE_POLKIT
libsysprof_@API_VERSION@_la_LIBADD += $(POLKIT_LIBS)
libsysprof_@API_VERSION@_la_CFLAGS += $(POLKIT_CFLAGS) -DENABLE_POLKIT
endif

libsysprof_@API_VERSION@_la_LDFLAGS = \
	-avoid-version \
	-no-undefined \
	-export-symbols-regex '^(sp_).*' \
	$(WARN_LDFLAGS) \
	$(NULL)


if ENABLE_GTK

# This is our GTK library containing the widgets suitable for viewing
# and manipulating the various profiler API in libsysprof.  This is
# meant to be used by IDEs and the sysprof gui.
lib_LTLIBRARIES += libsysprof-ui-@API_VERSION@.la

uiheadersdir = $(includedir)/sysprof-@API_VERSION@
uiheaders_DATA = \
	sp-callgraph-view.h \
	sp-cell-renderer-percent.h \
	sp-cpu-visualizer-row.h \
	sp-empty-state-view.h \
	sp-failed-state-view.h \
	sp-line-visualizer-row.h \
	sp-model-filter.h \
	sp-multi-paned.h \
	sp-process-model.h \
	sp-process-model-item.h \
	sp-process-model-row.h \
	sp-profiler-menu-button.h \
	sp-recording-state-view.h \
	sp-visualizer-row.h \
	sp-visualizer-view.h \
	sp-zoom-manager.h \
	sysprof-ui.h \
	$(NULL)

libsysprof_ui_@API_VERSION@_la_SOURCES = \
	$(uiheaders_DATA) \
	sp-callgraph-view.c \
	sp-cell-renderer-percent.c \
	sp-color-cycle.c \
	sp-color-cycle.h \
	sp-cpu-visualizer-row.c \
	sp-empty-state-view.c \
	sp-failed-state-view.c \
	sp-line-visualizer-row.c \
	sp-model-filter.c \
	sp-multi-paned.c \
	sp-process-model.c \
	sp-process-model-item.c \
	sp-process-model-row.c \
	sp-profiler-menu-button.c \
	sp-recording-state-view.c \
	sp-visualizer-list.c \
	sp-visualizer-list.h \
	sp-visualizer-row.c \
	sp-visualizer-ticks.c \
	sp-visualizer-ticks.h \
	sp-visualizer-view.c \
	sp-zoom-manager.c \
	$(NULL)

libsysprof_ui_@API_VERSION@_la_CFLAGS = \
	-I$(srcdir)/util \
	$(GIO_CFLAGS) \
	$(GTK_CFLAGS) \
	$(POLKIT_CFLAGS) \
	$(WARN_CFLAGS) \
	$(NULL)

libsysprof_ui_@API_VERSION@_la_LIBADD = \
	libutil.la \
	libsysprof-@API_VERSION@.la \
	$(GIO_LIBS) \
	$(GTK_LIBS) \
	$(POLKIT_LIBS) \
	$(NULL)

libsysprof_ui_@API_VERSION@_la_LDFLAGS = \
	-avoid-version \
	-no-undefined \
	-export-symbols-regex '^(sp_).*' \
	$(WARN_LDFLAGS) \
	$(NULL)

nodist_libsysprof_ui_@API_VERSION@_la_SOURCES = \
	sp-resources.c \
	sp-resources.h

glib_resources_xml = resources/libsysprof.gresource.xml
glib_resources_c = sp-resources.c
glib_resources_h = sp-resources.h
glib_resources_namespace = sp
include $(top_srcdir)/gresources.mk

endif


-include $(top_srcdir)/git.mk

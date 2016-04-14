EXTRA_DIST =
CLEANFILES =
DISTCLEANFILES =
BUILT_SOURCES =

lib_LTLIBRARIES = libsysprof-@API_VERSION@.la

nodist_libsysprof_@API_VERSION@_la_SOURCES = \
	sp-resources.c \
	sp-resources.h

headersdir = $(includedir)/sysprof-@API_VERSION@
headers_DATA = \
	sysprof.h \
	sysprof-version.h \
	sp-address.h \
	sp-callgraph-profile.h \
	sp-callgraph-view.h \
	sp-capture-reader.h \
	sp-capture-writer.h \
	sp-capture-types.h \
	sp-cell-renderer-percent.h \
	sp-clock.h \
	sp-elf-symbol-resolver.h \
	sp-empty-state-view.h \
	sp-failed-state-view.h \
	sp-error.h \
	sp-gjs-source.h \
	sp-jitmap-symbol-resolver.h \
	sp-kernel-symbol.h \
	sp-kernel-symbol-resolver.h \
	sp-map-lookaside.h \
	sp-model-filter.h \
	sp-perf-source.h \
	sp-proc-source.h \
	sp-process-model.h \
	sp-process-model-item.h \
	sp-process-model-row.h \
	sp-profile.h \
	sp-profiler.h \
	sp-profiler-menu-button.h \
	sp-recording-state-view.h \
	sp-source.h \
	sp-symbol-resolver.h \
	$(NULL)

libsysprof_@API_VERSION@_la_SOURCES = \
	$(headers_DATA) \
	sp-address.c \
	sp-callgraph-profile.c \
	sp-callgraph-profile-private.h \
	sp-callgraph-view.c \
	sp-capture-reader.c \
	sp-capture-writer.c \
	sp-cell-renderer-percent.c \
	sp-clock.c \
	sp-elf-symbol-resolver.c \
	sp-empty-state-view.c \
	sp-failed-state-view.c \
	sp-error.c \
	sp-gjs-source.c \
	sp-jitmap-symbol-resolver.c \
	sp-kernel-symbol.c \
	sp-kernel-symbol-resolver.c \
	sp-line-reader.c \
	sp-line-reader.h \
	sp-map-lookaside.c \
	sp-model-filter.c \
	sp-perf-counter.c \
	sp-perf-counter.h \
	sp-perf-source.c \
	sp-proc-source.c \
	sp-process-model.c \
	sp-process-model-item.c \
	sp-process-model-row.c \
	sp-profile.c \
	sp-profiler.c \
	sp-profiler-menu-button.c \
	sp-recording-state-view.c \
	sp-scrolled-window.c \
	sp-scrolled-window.h \
	sp-source.c \
	sp-symbol-resolver.c \
	util/binfile.c \
	util/binfile.h \
	util/demangle.cpp \
	util/demangle.h \
	util/elfparser.c \
	util/elfparser.h \
	util/stackstash.c \
	util/stackstash.h \
	util/util.h \
	$(NULL)


libsysprof_@API_VERSION@_la_CFLAGS = \
	-I$(srcdir)/util \
	$(SYSPROF_CFLAGS) \
	$(WARN_CFLAGS) \
	$(NULL)

libsysprof_@API_VERSION@_la_CXXFLAGS = \
	-I$(srcdir)/util \
	$(SYSPROF_CFLAGS) \
	$(WARN_CXXFLAGS) \
	$(NULL)

if ENABLE_SYSPROFD
libsysprof_@API_VERSION@_la_CFLAGS += -DENABLE_SYSPROFD
endif

libsysprof_@API_VERSION@_la_LIBADD = \
	$(SYSPROF_LIBS) \
	$(NULL)

libsysprof_@API_VERSION@_la_LDFLAGS = \
	-avoid-version \
	-no-undefined \
	-export-symbols-regex '^(sp_).*' \
	$(WARN_LDFLAGS) \
	$(NULL)


glib_resources_xml = resources/libsysprof.gresource.xml
glib_resources_c = sp-resources.c
glib_resources_h = sp-resources.h
glib_resources_namespace = sp
include $(top_srcdir)/gresources.mk


-include $(top_srcdir)/git.mk

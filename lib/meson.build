
# Both the profiler library and the UI library need to share some
# data-structures. Notably, the StackStash. So we build a private
# static library that can be used from both. It adds a little bit
# of overhead in terms of duplicated procedures, but they should
# always be installed in sync, and therefore no big deal.

libutil_sources = [
  'util/binfile.c',
  'util/binfile.h',
  'util/demangle.cpp',
  'util/demangle.h',
  'util/elfparser.c',
  'util/elfparser.h',
  'util/pointcache.c',
  'util/pointcache.h',
  'util/stackstash.c',
  'util/stackstash.h',
]

libutil_deps = [
  dependency('gio-unix-2.0', version: '>=2.44.0')
]

libutil_includes = [
  'util',
]

libutil = static_library('util',
  libutil_sources,
  include_directories: include_directories(libutil_includes),
  dependencies: libutil_deps,
  pic: true,
)

libutil_dep = declare_dependency(
  link_with: libutil,
  dependencies: libutil_deps,
  include_directories: include_directories(libutil_includes),
)

# We split up the library code into two libraries.  One containing the
# minimum necessary to do profiling on a particular host (that might not
# have GTK+ installed), and the library containing reusable GTK
# components (for IDE integration).

libsysprof_api_version = '2'

libsysprof_version_conf = configuration_data()
libsysprof_version = meson.project_version().split('.')
libsysprof_version_conf.set('MAJOR_VERSION', libsysprof_version[0])
libsysprof_version_conf.set('MINOR_VERSION', libsysprof_version[1])
libsysprof_version_conf.set('MICRO_VERSION', libsysprof_version[2])
libsysprof_version_conf.set('VERSION', meson.project_version())

configure_file(
  input: 'sysprof-version.h.in',
  output: 'sysprof-version.h',
  configuration: libsysprof_version_conf,
  install: true,
  install_dir: join_paths(get_option('includedir'),
                          'sysprof-' + libsysprof_api_version)
)

libsysprof_headers = [
  'sysprof.h',
  'sp-address.h',
  'sp-callgraph-profile.h',
  'sp-capture-condition.h',
  'sp-capture-cursor.h',
  'sp-capture-reader.h',
  'sp-capture-writer.h',
  'sp-capture-types.h',
  'sp-clock.h',
  'sp-elf-symbol-resolver.h',
  'sp-error.h',
  'sp-gjs-source.h',
  'sp-hostinfo-source.h',
  'sp-jitmap-symbol-resolver.h',
  'sp-kernel-symbol.h',
  'sp-kernel-symbol-resolver.h',
  'sp-local-profiler.h',
  'sp-map-lookaside.h',
  'sp-perf-source.h',
  'sp-proc-source.h',
  'sp-profile.h',
  'sp-profiler.h',
  'sp-selection.h',
  'sp-source.h',
  'sp-symbol-dirs.h',
  'sp-symbol-resolver.h',
]

libsysprof_sources = [
  'sp-address.c',
  'sp-callgraph-profile.c',
  'sp-callgraph-profile-private.h',
  'sp-capture-condition.c',
  'sp-capture-cursor.c',
  'sp-capture-reader.c',
  'sp-capture-writer.c',
  'sp-clock.c',
  'sp-elf-symbol-resolver.c',
  'sp-error.c',
  'sp-gjs-source.c',
  'sp-hostinfo-source.c',
  'sp-jitmap-symbol-resolver.c',
  'sp-kernel-symbol.c',
  'sp-kernel-symbol-resolver.c',
  'sp-line-reader.c',
  'sp-line-reader.h',
  'sp-local-profiler.c',
  'sp-map-lookaside.c',
  'sp-perf-counter.c',
  'sp-perf-counter.h',
  'sp-perf-source.c',
  'sp-platform.c',
  'sp-platform.h',
  'sp-proc-source.c',
  'sp-profile.c',
  'sp-profiler.c',
  'sp-selection.c',
  'sp-source.c',
  'sp-symbol-dirs.c',
  'sp-symbol-resolver.c',
]

cxx = meson.get_compiler('cpp')

libsysprof_deps = [
  libutil_dep,
  cxx.find_library('stdc++'),
]

version_link_arg = '-Wl,--version-script,' + join_paths(meson.current_source_dir(), 'sysprof.map')
libsysprof_c_args = []

if get_option('with_sysprofd') != 'none'
  libsysprof_deps += dependency('polkit-gobject-1')
  libsysprof_c_args += '-DENABLE_POLKIT'
endif

libsysprof = shared_library('sysprof-' + libsysprof_api_version,
  libsysprof_sources,
  dependencies: libsysprof_deps,
  c_args: libsysprof_c_args,
  link_args: version_link_arg,
  link_depends: 'sysprof.map',
  install: true,
)

install_headers(libsysprof_headers,
  subdir: 'sysprof-' + libsysprof_api_version,
)

libsysprof_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: libsysprof,
  dependencies: libsysprof_deps,
)

if get_option('enable_gtk')
  # This is our GTK library containing the widgets suitable for viewing
  # and manipulating the various profiler API in libsysprof.  This is
  # meant to be used by IDEs and the sysprof gui.

  libsysprof_ui_headers = [
    'sp-callgraph-view.h',
    'sp-cell-renderer-percent.h',
    'sp-cpu-visualizer-row.h',
    'sp-empty-state-view.h',
    'sp-failed-state-view.h',
    'sp-line-visualizer-row.h',
    'sp-model-filter.h',
    'sp-multi-paned.h',
    'sp-process-model.h',
    'sp-process-model-item.h',
    'sp-process-model-row.h',
    'sp-profiler-menu-button.h',
    'sp-recording-state-view.h',
    'sp-visualizer-row.h',
    'sp-visualizer-view.h',
    'sp-zoom-manager.h',
    'sysprof-ui.h',
  ]

  libsysprof_ui_sources = [
    'sp-callgraph-view.c',
    'sp-cell-renderer-percent.c',
    'sp-color-cycle.c',
    'sp-color-cycle.h',
    'sp-cpu-visualizer-row.c',
    'sp-empty-state-view.c',
    'sp-failed-state-view.c',
    'sp-line-visualizer-row.c',
    'sp-model-filter.c',
    'sp-multi-paned.c',
    'sp-process-model.c',
    'sp-process-model-item.c',
    'sp-process-model-row.c',
    'sp-profiler-menu-button.c',
    'sp-recording-state-view.c',
    'sp-theme-manager.c',
    'sp-theme-manager.h',
    'sp-visualizer-list.c',
    'sp-visualizer-list.h',
    'sp-visualizer-row.c',
    'sp-visualizer-row-private.h',
    'sp-visualizer-ticks.c',
    'sp-visualizer-ticks.h',
    'sp-visualizer-view.c',
    'sp-zoom-manager.c',
  ]

  libsysprof_ui_resources = gnome.compile_resources(
    'sp-ui-resources', 'resources/libsysprof.gresource.xml',
    source_dir: 'resources',
    c_name: 'sp',
  )

  libsysprof_ui_deps = [
    libsysprof_dep,
    dependency('gtk+-4.0', version: '>=3.89.5'),
  ]

  libsysprof_ui = shared_library('sysprof-ui-' + libsysprof_api_version,
    libsysprof_ui_resources + libsysprof_ui_sources,
    dependencies: libsysprof_ui_deps,
    link_args: version_link_arg,
    link_depends: 'sysprof.map',
    install: true,
  )

  install_headers(libsysprof_ui_headers,
    subdir: 'sysprof-' + libsysprof_api_version,
  )

  libsysprof_ui_dep = declare_dependency(
    dependencies: libsysprof_ui_deps,
    link_with: libsysprof_ui,
    include_directories: include_directories('.'),
  )

endif

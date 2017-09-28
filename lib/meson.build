
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
    install_dir: join_paths(get_option('includedir'), 'sysprof-' + libsysprof_api_version)
)

libsysprof_headers = [
  'sysprof.h',
  'sp-address.h',
  'sp-clock.h',
  'sp-error.h',
  'callgraph/sp-callgraph-profile.h',
  'capture/sp-capture-condition.h',
  'capture/sp-capture-cursor.h',
  'capture/sp-capture-reader.h',
  'capture/sp-capture-types.h',
  'capture/sp-capture-writer.h',
  'profiler/sp-local-profiler.h',
  'profiler/sp-profile.h',
  'profiler/sp-profiler.h',
  'sources/sp-gjs-source.h',
  'sources/sp-hostinfo-source.h',
  'sources/sp-perf-source.h',
  'sources/sp-proc-source.h',
  'sources/sp-source.h',
  'symbols/sp-elf-symbol-resolver.h',
  'symbols/sp-jitmap-symbol-resolver.h',
  'symbols/sp-kernel-symbol-resolver.h',
  'symbols/sp-kernel-symbol.h',
  'symbols/sp-symbol-dirs.h',
  'symbols/sp-symbol-resolver.h',
  'util/sp-map-lookaside.h',
  'util/sp-selection.h',
]

libsysprof_sources = [
  'sp-address.c',
  'sp-clock.c',
  'sp-error.c',
  'callgraph/sp-callgraph-profile-private.h',
  'callgraph/sp-callgraph-profile.c',
  'capture/sp-capture-condition.c',
  'capture/sp-capture-cursor.c',
  'capture/sp-capture-reader.c',
  'capture/sp-capture-writer.c',
  'profiler/sp-local-profiler.c',
  'profiler/sp-profile.c',
  'profiler/sp-profiler.c',
  'sources/sp-gjs-source.c',
  'sources/sp-hostinfo-source.c',
  'sources/sp-perf-counter.c',
  'sources/sp-perf-counter.h',
  'sources/sp-perf-source.c',
  'sources/sp-proc-source.c',
  'sources/sp-source.c',
  'symbols/sp-elf-symbol-resolver.c',
  'symbols/sp-jitmap-symbol-resolver.c',
  'symbols/sp-kernel-symbol-resolver.c',
  'symbols/sp-kernel-symbol.c',
  'symbols/sp-symbol-dirs.c',
  'symbols/sp-symbol-resolver.c',
  'util/binfile.c',
  'util/binfile.h',
  'util/demangle.cpp',
  'util/demangle.h',
  'util/elfparser.c',
  'util/elfparser.h',
  'util/sp-line-reader.c',
  'util/sp-line-reader.h',
  'util/sp-map-lookaside.c',
  'util/sp-platform.c',
  'util/sp-platform.h',
  'util/sp-selection.c',
  'util/stackstash.c',
  'util/stackstash.h',
]

cxx = meson.get_compiler('cpp')

libsysprof_deps = [
  cxx.find_library('stdc++'),
  dependency('gio-unix-2.0'),
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
    'callgraph/sp-callgraph-view.h',
    'util/sp-model-filter.h',
    'util/sp-process-model-item.h',
    'util/sp-process-model.h',
    'util/sp-zoom-manager.h',
    'visualizers/sp-cpu-visualizer-row.h',
    'visualizers/sp-line-visualizer-row.h',
    'visualizers/sp-visualizer-row.h',
    'visualizers/sp-visualizer-view.h',
    'widgets/sp-cell-renderer-percent.h',
    'widgets/sp-empty-state-view.h',
    'widgets/sp-failed-state-view.h',
    'widgets/sp-multi-paned.h',
    'widgets/sp-process-model-row.h',
    'widgets/sp-profiler-menu-button.h',
    'widgets/sp-recording-state-view.h',
    'sysprof-ui.h',
  ]

  libsysprof_ui_sources = [
    'callgraph/sp-callgraph-view.c',
    'util/pointcache.c',
    'util/pointcache.h',
    'util/sp-color-cycle.c',
    'util/sp-color-cycle.h',
    'util/sp-model-filter.c',
    'util/sp-process-model-item.c',
    'util/sp-process-model.c',
    'util/sp-theme-manager.c',
    'util/sp-theme-manager.h',
    'util/sp-zoom-manager.c',
    'util/stackstash.c',
    'util/stackstash.h',
    'visualizers/sp-cpu-visualizer-row.c',
    'visualizers/sp-line-visualizer-row.c',
    'visualizers/sp-visualizer-list.c',
    'visualizers/sp-visualizer-list.h',
    'visualizers/sp-visualizer-row-private.h',
    'visualizers/sp-visualizer-row.c',
    'visualizers/sp-visualizer-ticks.c',
    'visualizers/sp-visualizer-ticks.h',
    'visualizers/sp-visualizer-view.c',
    'widgets/sp-cell-renderer-percent.c',
    'widgets/sp-empty-state-view.c',
    'widgets/sp-failed-state-view.c',
    'widgets/sp-multi-paned.c',
    'widgets/sp-process-model-row.c',
    'widgets/sp-profiler-menu-button.c',
    'widgets/sp-recording-state-view.c',
  ]

  libsysprof_ui_resources = gnome.compile_resources(
    'sp-ui-resources', 'resources/libsysprof.gresource.xml',
    source_dir: 'resources',
        c_name: 'sp',
  )

  libsysprof_ui_deps = [
    libsysprof_dep,
    dependency('gtk+-3.0', version: '>=3.22.0'),
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

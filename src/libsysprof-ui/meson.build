libsysprof_ui_public_sources = [
  'sysprof-callgraph-view.c',
  'sysprof-color-cycle.c',
  'sysprof-cpu-visualizer-row.c',
  'sysprof-empty-state-view.c',
  'sysprof-failed-state-view.c',
  'sysprof-line-visualizer-row.c',
  'sysprof-marks-model.c',
  'sysprof-marks-view.c',
  'sysprof-mark-visualizer-row.c',
  'sysprof-model-filter.c',
  'sysprof-multi-paned.c',
  'sysprof-process-model-row.c',
  'sysprof-profiler-menu-button.c',
  'sysprof-recording-state-view.c',
  'sysprof-visualizer-list.c',
  'sysprof-visualizer-row.c',
  'sysprof-visualizer-ticks.c',
  'sysprof-visualizer-view.c',
  'sysprof-zoom-manager.c',
]

libsysprof_ui_private_sources = [
  'pointcache.c',
  'rectangles.c',
  'sysprof-cell-renderer-duration.c',
  'sysprof-cell-renderer-percent.c',
  'sysprof-theme-manager.c',
  '../stackstash.c',
]

libsysprof_ui_public_headers = [
  'sysprof-callgraph-view.h',
  'sysprof-cell-renderer-percent.h',
  'sysprof-cpu-visualizer-row.h',
  'sysprof-empty-state-view.h',
  'sysprof-failed-state-view.h',
  'sysprof-line-visualizer-row.h',
  'sysprof-marks-model.h',
  'sysprof-marks-view.h',
  'sysprof-mark-visualizer-row.h',
  'sysprof-model-filter.h',
  'sysprof-multi-paned.h',
  'sysprof-process-model-row.h',
  'sysprof-profiler-menu-button.h',
  'sysprof-recording-state-view.h',
  'sysprof-visualizer-list.h',
  'sysprof-visualizer-row.h',
  'sysprof-visualizer-ticks.h',
  'sysprof-visualizer-view.h',
  'sysprof-zoom-manager.h',
  'sysprof-ui.h',
]

libsysprof_ui_resources = gnome.compile_resources(
  'libsysprof-ui-resources',
  'libsysprof-ui.gresource.xml',
  c_name: 'lisysprof_ui',
)

libsysprof_ui_deps = [
  gio_dep,
  gtk_dep,
  libsysprof_dep,
]

libsysprof_ui = shared_library(
  'sysprof-ui-@0@'.format(libsysprof_api_version),
  libsysprof_ui_public_sources + libsysprof_ui_private_sources + libsysprof_ui_resources,

           dependencies: libsysprof_ui_deps,
            install_dir: get_option('libdir'),
                install: true,
                 c_args: [ '-DSYSPROF_UI_COMPILATION' ],
  gnu_symbol_visibility: 'hidden',
)

libsysprof_ui_dep = declare_dependency(
               link_with: libsysprof_ui,
            dependencies: libsysprof_ui_deps,
     include_directories: include_directories('.'),
)

pkgconfig.generate(
      subdirs: [ sysprof_header_subdir ],
      version: meson.project_version(),
         name: 'sysprof-ui-@0@'.format(libsysprof_api_version),
     filebase: 'sysprof-ui-@0@'.format(libsysprof_api_version),
  description: 'The UI library for GTK applications embedding sysprof',
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
     requires: [ 'gio-2.0', 'gtk+-3.0' ],
)

install_headers(libsysprof_ui_public_headers, subdir: sysprof_header_subdir)

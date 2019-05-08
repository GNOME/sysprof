libsysprof_ui_public_sources = [
  'sp-callgraph-view.c',
  'sp-color-cycle.c',
  'sp-cpu-visualizer-row.c',
  'sp-empty-state-view.c',
  'sp-failed-state-view.c',
  'sp-line-visualizer-row.c',
  'sp-mark-visualizer-row.c',
  'sp-model-filter.c',
  'sp-multi-paned.c',
  'sp-process-model-row.c',
  'sp-profiler-menu-button.c',
  'sp-recording-state-view.c',
  'sp-visualizer-list.c',
  'sp-visualizer-row.c',
  'sp-visualizer-ticks.c',
  'sp-visualizer-view.c',
  'sp-zoom-manager.c',
]

libsysprof_ui_private_sources = [
  'pointcache.c',
  'rectangles.c',
  'sp-cell-renderer-percent.c',
  'sp-theme-manager.c',
]

libsysprof_ui_public_headers = [
  'sp-callgraph-view.h',
  'sp-cell-renderer-percent.h',
  'sp-cpu-visualizer-row.h',
  'sp-empty-state-view.h',
  'sp-failed-state-view.h',
  'sp-line-visualizer-row.h',
  'sp-mark-visualizer-row.h',
  'sp-model-filter.h',
  'sp-multi-paned.h',
  'sp-process-model-row.h',
  'sp-profiler-menu-button.h',
  'sp-recording-state-view.h',
  'sp-visualizer-list.h',
  'sp-visualizer-row.h',
  'sp-visualizer-ticks.h',
  'sp-visualizer-view.h',
  'sp-zoom-manager.h',
  'sysprof-ui.h',
]

libsysprof_ui_resources = gnome.compile_resources(
  'libsysprof-ui-resources',
  'libsysprof-ui.gresource.xml',
  c_name: 'lisysprof_ui',
)

libsysprof_ui_deps = [
  dependency('gio-2.0', version: glib_req_version),
  dependency('gtk+-3.0', version: gtk_req_version),
  libsysprof_dep,
]

libsysprof_ui = shared_library('sysprof-ui-@0@'.format(libsysprof_api_version),
  libsysprof_ui_public_sources + libsysprof_ui_private_sources + libsysprof_ui_resources,
         dependencies: libsysprof_ui_deps,
          install_dir: get_option('libdir'),
              install: true,
               c_args: [ '-DSYSPROF_UI_COMPILATION' ],
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

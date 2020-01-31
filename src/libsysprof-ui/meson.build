if get_option('enable_gtk') and get_option('libsysprof')

libsysprof_ui_public_sources = [
  'sysprof-check.c',
  'sysprof-display.c',
  'sysprof-model-filter.c',
  'sysprof-notebook.c',
  'sysprof-page.c',
  'sysprof-process-model-row.c',
  'sysprof-visualizer.c',
  'sysprof-visualizer-group.c',
  'sysprof-zoom-manager.c',
]

libsysprof_ui_private_sources = [
  'pointcache.c',
  'rectangles.c',
  'sysprof-aid.c',
  'sysprof-aid-icon.c',
  'sysprof-battery-aid.c',
  'sysprof-cairo.c',
  'sysprof-callgraph-aid.c',
  'sysprof-callgraph-page.c',
  'sysprof-cell-renderer-duration.c',
  'sysprof-cell-renderer-percent.c',
  'sysprof-color-cycle.c',
  'sysprof-counters-aid.c',
  'sysprof-cpu-aid.c',
  'sysprof-depth-visualizer.c',
  'sysprof-details-page.c',
  'sysprof-diskstat-aid.c',
  'sysprof-display.c',
  'sysprof-duplex-visualizer.c',
  'sysprof-environ.c',
  'sysprof-environ-editor.c',
  'sysprof-environ-editor-row.c',
  'sysprof-environ-variable.c',
  'sysprof-failed-state-view.c',
  'sysprof-line-visualizer.c',
  'sysprof-log-model.c',
  'sysprof-logs-aid.c',
  'sysprof-logs-page.c',
  'sysprof-marks-aid.c',
  'sysprof-marks-model.c',
  'sysprof-marks-page.c',
  'sysprof-mark-visualizer.c',
  'sysprof-memory-aid.c',
  'sysprof-memprof-aid.c',
  'sysprof-memprof-page.c',
  'sysprof-memprof-visualizer.c',
  'sysprof-netdev-aid.c',
  'sysprof-procs-visualizer.c',
  'sysprof-profiler-assistant.c',
  'sysprof-proxy-aid.c',
  'sysprof-rapl-aid.c',
  'sysprof-recording-state-view.c',
  'sysprof-scrollmap.c',
  'sysprof-tab.c',
  'sysprof-theme-manager.c',
  'sysprof-time-label.c',
  'sysprof-time-visualizer.c',
  'sysprof-visualizer-group-header.c',
  'sysprof-visualizers-frame.c',
  'sysprof-visualizer-ticks.c',
  '../stackstash.c',
]

libsysprof_ui_public_headers = [
  'sysprof-check.h',
  'sysprof-display.h',
  'sysprof-model-filter.h',
  'sysprof-notebook.h',
  'sysprof-page.h',
  'sysprof-process-model-row.h',
  'sysprof-visualizer.h',
  'sysprof-visualizer-group.h',
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
  dazzle_dep,
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
    libraries: [libsysprof_ui],
      subdirs: [ sysprof_header_subdir ],
      version: meson.project_version(),
         name: 'sysprof-ui-@0@'.format(libsysprof_api_version),
     filebase: 'sysprof-ui-@0@'.format(libsysprof_api_version),
  description: 'The UI library for GTK applications embedding sysprof',
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
     requires: [ 'gio-2.0', 'gtk+-3.0' ],
     variables: [
       'datadir=' + datadir_for_pc_file,
     ],
)

install_headers(libsysprof_ui_public_headers, subdir: sysprof_header_subdir)

endif

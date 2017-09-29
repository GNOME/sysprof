if get_option('enable_gtk')

widgets_ui_headers = [
  'sp-cell-renderer-percent.h',
  'sp-empty-state-view.h',
  'sp-failed-state-view.h',
  'sp-multi-paned.h',
  'sp-process-model-row.h',
  'sp-profiler-menu-button.h',
  'sp-recording-state-view.h',
]

widgets_ui_sources = [
  'sp-cell-renderer-percent.c',
  'sp-empty-state-view.c',
  'sp-failed-state-view.c',
  'sp-multi-paned.c',
  'sp-process-model-row.c',
  'sp-profiler-menu-button.c',
  'sp-recording-state-view.c',
]

libsysprof_ui_headers += files(widgets_ui_headers)
libsysprof_ui_sources += files(widgets_ui_sources)

install_headers(widgets_ui_headers,
  subdir: join_paths(libsysprof_header_subdir, 'widgets'))

endif

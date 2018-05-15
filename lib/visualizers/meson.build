if get_option('enable_gtk')

visualizer_ui_headers = [
  'sp-cpu-visualizer-row.h',
  'sp-line-visualizer-row.h',
  'sp-mark-visualizer-row.h',
  'sp-visualizer-row.h',
  'sp-visualizer-view.h',
]

visualizer_ui_sources = [
  'sp-cpu-visualizer-row.c',
  'sp-line-visualizer-row.c',
  'sp-mark-visualizer-row.c',
  'sp-visualizer-list.c',
  'sp-visualizer-list.h',
  'sp-visualizer-row-private.h',
  'sp-visualizer-row.c',
  'sp-visualizer-ticks.c',
  'sp-visualizer-ticks.h',
  'sp-visualizer-view.c',
]

libsysprof_ui_headers += files(visualizer_ui_headers)
libsysprof_ui_sources += files(visualizer_ui_sources)

install_headers(visualizer_ui_headers,
  subdir: join_paths(libsysprof_header_subdir, 'visualizers'))

endif

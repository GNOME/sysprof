if get_option('enable_gtk')

timechart_ui_headers = [
  'sp-timechart-view.h',
]

timechart_ui_sources = [
  'sp-timechart-view.c',
]

libsysprof_ui_headers += files(timechart_ui_headers)
libsysprof_ui_sources += files(timechart_ui_sources)

install_headers(timechart_ui_headers, subdir: join_paths(libsysprof_header_subdir, 'timechart'))

endif

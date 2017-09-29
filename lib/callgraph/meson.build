callgraph_headers = [
  'sp-callgraph-profile.h',
]

callgraph_sources = [
  'sp-callgraph-profile-private.h',
  'sp-callgraph-profile.c',
]

libsysprof_headers += files(callgraph_headers)
libsysprof_sources += files(callgraph_sources)

install_headers(callgraph_headers,
  subdir: join_paths(libsysprof_header_subdir, 'callgraph'))


if get_option('enable_gtk')

callgraph_ui_headers = [
  'sp-callgraph-view.h',
]

callgraph_ui_sources = [
  'sp-callgraph-view.c',
]

libsysprof_ui_headers += files(callgraph_ui_headers)
libsysprof_ui_sources += files(callgraph_ui_sources)

install_headers(callgraph_ui_headers,
  subdir: join_paths(libsysprof_header_subdir, 'callgraph'))

endif

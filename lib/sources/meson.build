sources_headers = [
  'sp-gjs-source.h',
  'sp-hostinfo-source.h',
  'sp-perf-source.h',
  'sp-proc-source.h',
  'sp-source.h',
]

sources_sources = [
  'sp-gjs-source.c',
  'sp-hostinfo-source.c',
  'sp-perf-counter.c',
  'sp-perf-counter.h',
  'sp-perf-source.c',
  'sp-proc-source.c',
  'sp-source.c',
]

libsysprof_headers += files(sources_headers)
libsysprof_sources += files(sources_sources)

install_headers(sources_headers,
  subdir: join_paths(libsysprof_header_subdir, 'sources'))

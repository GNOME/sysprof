profiler_headers = [
  'sp-local-profiler.h',
  'sp-profile.h',
  'sp-profiler.h',
]

profiler_sources = [
  'sp-local-profiler.c',
  'sp-profile.c',
  'sp-profiler.c',
]

libsysprof_headers += files(profiler_headers)
libsysprof_sources += files(profiler_sources)

install_headers(profiler_headers,
  subdir: join_paths(libsysprof_header_subdir, 'profiler'))

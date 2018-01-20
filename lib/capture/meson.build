capture_headers = [
  'sp-capture-condition.h',
  'sp-capture-cursor.h',
  'sp-capture-reader.h',
  'sp-capture-types.h',
  'sp-capture-writer.h',
]

capture_sources = [
  'sp-capture-condition.c',
  'sp-capture-cursor.c',
  'sp-capture-reader.c',
  'sp-capture-writer.c',
]

libsysprof_headers += files(capture_headers)
libsysprof_sources += files(capture_sources)

libsysprof_capture_sources += files(capture_sources)

install_headers(capture_headers,
  subdir: join_paths(libsysprof_header_subdir, 'capture'))

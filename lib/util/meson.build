util_headers = [
  'sp-map-lookaside.h',
  'sp-selection.h',
]

util_sources = [
  'binfile.c',
  'binfile.h',
  'demangle.cpp',
  'demangle.h',
  'elfparser.c',
  'elfparser.h',
  'sp-line-reader.c',
  'sp-line-reader.h',
  'sp-map-lookaside.c',
  'sp-platform.c',
  'sp-platform.h',
  'sp-selection.c',
  'stackstash.c',
  'stackstash.h',
]

libsysprof_headers += files(util_headers)
libsysprof_sources += files(util_sources)

install_headers(util_headers,
  subdir: join_paths(libsysprof_header_subdir, 'util'))


if get_option('enable_gtk')

util_ui_headers = [
  'sp-model-filter.h',
  'sp-process-model-item.h',
  'sp-process-model.h',
  'sp-zoom-manager.h',
]

util_ui_sources = [
  'pointcache.c',
  'pointcache.h',
  'sp-color-cycle.c',
  'sp-color-cycle.h',
  'sp-model-filter.c',
  'sp-process-model-item.c',
  'sp-process-model.c',
  'sp-theme-manager.c',
  'sp-theme-manager.h',
  'sp-zoom-manager.c',
  'stackstash.c',
  'stackstash.h',
]

libsysprof_ui_headers += files(util_ui_headers)
libsysprof_ui_sources += files(util_ui_sources)

install_headers(util_ui_headers,
  subdir: join_paths(libsysprof_header_subdir, 'util'))

endif

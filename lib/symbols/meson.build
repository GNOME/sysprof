symbols_headers = [
  'sp-elf-symbol-resolver.h',
  'sp-jitmap-symbol-resolver.h',
  'sp-kernel-symbol-resolver.h',
  'sp-kernel-symbol.h',
  'sp-symbol-dirs.h',
  'sp-symbol-resolver.h',
]

symbols_sources = [
  'sp-elf-symbol-resolver.c',
  'sp-jitmap-symbol-resolver.c',
  'sp-kernel-symbol-resolver.c',
  'sp-kernel-symbol.c',
  'sp-symbol-dirs.c',
  'sp-symbol-resolver.c',
]

libsysprof_headers += files(symbols_headers)
libsysprof_sources += files(symbols_sources)

install_headers(symbols_headers,
  subdir: join_paths(libsysprof_header_subdir, 'symbols'))

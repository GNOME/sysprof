shared_sources = [
  'sp-address.c',
  'sp-clock.c',
  'sp-error.c',
  'sp-line-reader.c',
  'sp-platform.c',
]

shared_public_headers = [
  'sp-address.h',
  'sp-clock.h',
  'sp-error.h',
  'sp-platform.h',
]

install_headers(shared_public_headers, subdir: sysprof_header_subdir)

if host_machine.system() == 'linux'
  shared_sources += ['sp-kallsyms.c']
endif

libshared_deps = [
  dependency('glib-2.0'),
]

libshared = static_library('shared', shared_sources,
  dependencies: libshared_deps,
)

libshared_dep = declare_dependency(
           link_whole: libshared,
  include_directories: include_directories('.'),
)

libelfparser_sources = [
  'demangle.cpp',
  'rust-demangle.c',
  'elfparser.c',
]

libelfparser_deps = [
  dependency('glib-2.0', version: glib_req_version),
]

libelfparser_static = static_library('elfparser', libelfparser_sources,
           dependencies: libelfparser_deps,
  gnu_symbol_visibility: 'hidden',
)

libelfparser_static_dep = declare_dependency(
  include_directories: include_directories('.'),
         dependencies: libelfparser_deps,
            link_with: libelfparser_static,
)

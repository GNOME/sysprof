libeggbitset_sources = [
  'eggbitset.c',
]

libeggbitset_deps = [
  dependency('gio-2.0', version: glib_req_version),
]

libeggbitset_static = static_library('eggbitset', libeggbitset_sources,
  gnu_symbol_visibility: 'hidden',
           dependencies: libeggbitset_deps,
)

libeggbitset_static_dep = declare_dependency(
  include_directories: include_directories('.'),
            link_with: libeggbitset_static,
)

if get_option('with_sysprofd') == 'bundled'

sysprofd_sources = [
  'sysprofd.c',
  'sd-bus-helper.c',
  'sd-bus-helper.h',
  '../lib/sp-kallsyms.c',
  '../lib/sp-kallsyms.h',
]

# NOTE: This is used in data/meson.build
pkglibexecdir = join_paths(get_option('prefix'), get_option('libexecdir'), 'sysprof')

sysprofd_deps = [
  dependency('libsystemd', version: '>=222'),
  dependency('glib-2.0'),
]

sysprofd = executable('sysprofd',
  sysprofd_sources,
  c_args: exe_c_args,
  link_args: exe_link_args,
  dependencies: sysprofd_deps,
  install: true,
  install_dir: pkglibexecdir,
)

endif

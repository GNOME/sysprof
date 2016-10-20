if get_option('with_sysprofd') == 'bundled'

sysprofd_sources = [
  'sysprofd.c',
  'sd-bus-helper.c',
  'sd-bus-helper.h',
]

# NOTE: This is used in data/meson.build
pkglibexecdir = join_paths(get_option('prefix'), get_option('libexecdir'), 'sysprof')

sysprofd = executable('sysprofd',
  sysprofd_sources,
  c_args: exe_c_args,
  link_args: exe_link_args,
  dependencies: dependency('libsystemd', version: '>=222'),
  install: true,
  install_dir: pkglibexecdir,
)

endif

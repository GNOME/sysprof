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

# Check for sd-bus support either by systemd or elogind
sdbus_dep = dependency('libsystemd', version: '>=222', required: false)
if not sdbus_dep.found()
  sdbus_dep = dependency('libelogind', version: '>=239.3', required: false)
  if not sdbus_dep.found()
    error('sysprofd requires sd-bus but neither systemd nor elogind were found')
  endif
endif

sysprofd_deps = [
  sdbus_dep,
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

sysprof_live_unwinder_deps = [
  libsysprof_static_dep,
  dependency('libdw'),
]

sysprof_live_unwinder_sources = [
  'sysprof-live-process.c',
  'sysprof-live-unwinder.c',
  'main.c',
]

sysprof_live_unwinder = executable('sysprof-live-unwinder', sysprof_live_unwinder_sources,
  dependencies: sysprof_live_unwinder_deps,
        c_args: release_flags,
       install: true,
   install_dir: pkglibexecdir,
)

subdir('tests')

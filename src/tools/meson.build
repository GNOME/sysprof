tools_deps = [
  libsysprof_capture_dep,
]

if host_machine.system() == 'linux'
  sysprof_cli = executable('sysprof-cli', 'sysprof-cli.c',
    dependencies: tools_deps + [libsysprof_dep],
     install_dir: get_option('bindir'),
         install: true,
  )
endif

sysprof_cat = executable('sysprof-cat', 'sysprof-cat.c',
  dependencies: tools_deps,
   install_dir: get_option('bindir'),
       install: true,
)

sysprof_dump = executable('sysprof-dump', 'sysprof-dump.c',
  dependencies: tools_deps,
   install_dir: get_option('bindir'),
       install: true,
)

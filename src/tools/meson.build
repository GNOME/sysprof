
sysprof_cli = executable('sysprof-cli', 'sysprof-cli.c',
  dependencies: libsysprof_dep,
   install_dir: get_option('bindir'),
       install: true,
)

sysprof_cat = executable('sysprof-cat', 'sysprof-cat.c',
  dependencies: libsysprof_capture_dep,
   install_dir: get_option('bindir'),
       install: true,
           pie: true,
)

sysprof_dump = executable('sysprof-dump', 'sysprof-dump.c',
  dependencies: libsysprof_capture_dep,
   install_dir: get_option('bindir'),
       install: true,
           pie: true,
)

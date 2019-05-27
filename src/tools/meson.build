tools_deps = [
  libsysprof_capture_dep,
]

if host_machine.system() == 'linux'
  sysprof_cli = executable('sysprof-cli', 'sysprof-cli.c',
    dependencies: tools_deps + [libsysprof_dep, polkit_dep, polkit_agent_dep],
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

sysprof_profiler_ctl = executable('sysprof-profiler-ctl',
  [ 'sysprof-profiler-ctl.c', ipc_profiler_src ],
  dependencies: [ tools_deps, gio_unix_dep ],
       install: false,
)

list_threads = executable('list-threads', ['list-threads.c'],
  dependencies: [ tools_deps ],
       install: false,
)
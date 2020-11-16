if get_option('enable_tools')

tools_deps = [
  dependency('glib-2.0'),
  libsysprof_capture_dep,
]

tools_cflags = [ '-DSYSPROF_COMPILATION ']

if get_option('libsysprof')
  sysprof_cli = executable('sysprof-cli', 'sysprof-cli.c',
    dependencies: tools_deps + [libsysprof_dep, polkit_dep, polkit_agent_dep],
          c_args: tools_cflags,
     install_dir: get_option('bindir'),
         install: true,
  )
endif

sysprof_cat = executable('sysprof-cat', 'sysprof-cat.c',
  dependencies: tools_deps,
        c_args: tools_cflags,
       install: false,
)

sysprof_dump = executable('sysprof-dump', 'sysprof-dump.c',
  dependencies: tools_deps,
        c_args: tools_cflags,
       install: false,
)

if get_option('with_sysprofd') == 'bundled' or get_option('libsysprof')
  sysprof_profiler_ctl = executable('sysprof-profiler-ctl',
    [ 'sysprof-profiler-ctl.c', ipc_profiler_src ],
    dependencies: [ tools_deps, dependency('gio-unix-2.0', version: glib_req_version) ],
          c_args: tools_cflags,
         install: false,
  )
endif

list_threads = executable('list-threads', ['list-threads.c'],
  dependencies: [ tools_deps ],
        c_args: tools_cflags,
       install: false,
)

endif

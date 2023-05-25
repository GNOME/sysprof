tools_deps = [
  dependency('glib-2.0'),
  libsysprof_capture_dep,
]

tools_cflags = [ '-DSYSPROF_COMPILATION ']

sysprof_cli = executable('sysprof-cli', 'sysprof-cli.c',
  dependencies: [libsysprof_static_dep, polkit_dep, polkit_agent_dep],
        c_args: tools_cflags,
   install_dir: get_option('bindir'),
       install: true,
)

sysprof_cat = executable('sysprof-cat', 'sysprof-cat.c',
  dependencies: tools_deps,
        c_args: tools_cflags,
       install: false,
)

if get_option('libsysprof')
  sysprof_dump = executable('sysprof-dump', 'sysprof-dump.c',
    dependencies: [libsysprof_dep],
          c_args: tools_cflags,
         install: false,
  )

  rewrite_pid = executable('rewrite-pid', ['rewrite-pid.c'],
    dependencies: [libsysprof_dep],
          c_args: tools_cflags,
         install: false,
  )
endif

if get_option('sysprofd') == 'bundled' or get_option('libsysprof')
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

if get_option('agent')
  sysprof_agent = executable('sysprof-agent',
    ['sysprof-agent.c', ipc_agent_src],
    dependencies: [libsysprof_static_dep],
          c_args: tools_cflags,
     install_dir: get_option('bindir'),
         install: true,
  )
endif

if get_option('gtk')
  callgraph = executable('callgraph', ['callgraph.c'],
    dependencies: [libsysprof_analyze_static_dep,
                   dependency('gtk4', version: gtk_req_version)],
          c_args: tools_cflags,
         install: false,
  )
endif

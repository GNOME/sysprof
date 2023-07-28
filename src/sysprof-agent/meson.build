ipc_agent_src = gnome.gdbus_codegen('ipc-agent',
           sources: 'org.gnome.Sysprof.Agent.xml',
  interface_prefix: 'org.gnome.Sysprof.',
         namespace: 'Ipc',
)

sysprof_agent_sources = [
  'sysprof-agent.c',
  ipc_agent_src,
]

sysprof_agent_c_args = [
]

sysprof_agent_deps = [
  libsysprof_static_dep,
]

sysprof_agent = executable('sysprof-agent', sysprof_agent_sources,
  dependencies: sysprof_agent_deps,
        c_args: release_flags + sysprof_agent_c_args,
       install: true,
)

install_data('org.gnome.Sysprof.Agent.xml',
  install_dir: join_paths(datadir, 'dbus-1/interfaces'),
)

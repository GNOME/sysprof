sysprof_cli_sources = [
  'sysprof-cli.c',
]

sysprof_cli_c_args = [
]

sysprof_cli_deps = [
  dependency('polkit-agent-1'),

  libsysprof_static_dep,
]

sysprof_cli = executable('sysprof-cli', sysprof_cli_sources,
  dependencies: sysprof_cli_deps,
        c_args: release_flags + sysprof_cli_c_args,
       install: true,
)

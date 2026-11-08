sysprof_cli_sources = [
  'sysprof-cli.c',
]

sysprof_cli_c_args = [
]

sysprof_cli_deps = [
  libsysprof_static_dep,
]

if polkit_agent_dep.found()
  sysprof_cli_deps += polkit_agent_dep
endif

if libsystemd_dep.found()
  sysprof_cli_deps += libsystemd_dep
endif

sysprof_cli = executable('sysprof-cli', sysprof_cli_sources,
  dependencies: sysprof_cli_deps,
        c_args: release_flags + sysprof_cli_c_args,
       install: true,
)

test('sysprof-cli-unprivileged',
  sysprof_cli,
  args: [
    '--force',
    '--no-sysprofd',
    '--no-perf',
    '--no-cpu',
    '--no-battery',
    '--no-memory',
    '--no-disk',
    '--no-network',
    '--use-trace-fd',
    join_paths(meson.current_build_dir(), 'unprivileged.syscap'),
    '--',
    '/usr/bin/true',
  ],
  env: ['G_DEBUG=fatal-criticals'],
)

if get_option('tests')
  test_no_dbus = executable('test-no-dbus', 'test-no-dbus.c',
    dependencies: [gio_dep, libsysprof_capture_dep],
          c_args: ['-UG_DISABLE_ASSERT'],
  )

  test('sysprof-cli-no-dbus', test_no_dbus,
    args: [sysprof_cli],
  )
endif

sysprof_live_unwinder_test_env = [
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

sysprof_live_unwinder_testsuite_c_args = [
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DBUILDDIR="@0@"'.format(meson.current_build_dir()),
]

sysprof_live_unwinder_testsuite = {}

if host_machine.cpu_family() in ['x86', 'x86_64']
  sysprof_live_unwinder_testsuite += {'test-live-unwinder' : {'skip': true}}
endif

sysprof_live_unwinder_testsuite_deps = [
  libsysprof_static_dep,
]

if polkit_agent_dep.found()
  sysprof_live_unwinder_testsuite_deps += polkit_agent_dep
endif

foreach test, params: sysprof_live_unwinder_testsuite
  test_exe = executable(test, '@0@.c'.format(test),
          c_args: sysprof_live_unwinder_testsuite_c_args,
    dependencies: sysprof_live_unwinder_testsuite_deps,
  )

  if not params.get('skip', false)
    test(test, test_exe, env: sysprof_live_unwinder_test_env)
  endif
endforeach

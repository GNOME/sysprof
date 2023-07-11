libsysprof_profile_test_env = [
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

libsysprof_profile_testsuite_c_args = [
  '-DG_LOG_DOMAIN="libsysprof-profile"',
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

libsysprof_profile_testsuite = {
  'test-maps-parser' : {'skip': true},
  'test-profiler' : {'skip': true},
}

libsysprof_profile_testsuite_deps = [
  libsysprof_profile_static_dep,
]

foreach test, params: libsysprof_profile_testsuite
  test_exe = executable(test, '@0@.c'.format(test),
          c_args: libsysprof_profile_testsuite_c_args,
    dependencies: libsysprof_profile_testsuite_deps,
  )
  if not params.get('skip', false)
    test(test, test_exe, env: libsysprof_profile_test_env)
  endif
endforeach
